#ifndef PANGOFLY_V2_SEGMENT_H_
#define PANGOFLY_V2_SEGMENT_H_

// ============================================================================
// Segment — 基于固定虚拟地址的共享内存通道
//
// 布局（整段映射到 Region 分配的独占窗口）：
//   ┌──────────────────────────────────────────────┐
//   │ SegmentHeader（含地址握手 self_base）         │
//   ├──────────────────────────────────────────────┤
//   │ BlockHeader[0..n-1]（64B 对齐，各一 cache line）│
//   ├──────────────────────────────────────────────┤
//   │ chunk 0: Arena 元数据 + 数据区                 │
//   │ chunk 1: ...                                  │
//   └──────────────────────────────────────────────┘
//
// 关键约束
//   * 映射必须落在 Region 指定的固定地址，失败即返回 false，绝不回退
//   * 打开已存在的段时校验 header.self_base 与本进程实际地址一致
//   * 每个块内的 arena 是唯一的分配入口，消息对象 T 由该 arena 分配，
//     其相对 payload 的偏移记在 BlockHeader::msg_offset
//   * 消息按 seq 单调递增；读端只认 seq > 上次读到的 seq，天然去重
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/shm/block.h"
#include "core/shm/region.h"

namespace pangofly {
namespace shm {

class Arena;

constexpr uint64_t kSegmentMagic   = 0x50465345474D4E32ULL;  // "PFSEGMN2"
constexpr uint32_t kSegmentVersion = 2;

struct SegmentConfig {
  uint32_t block_num = 4;
  uint32_t block_capacity = 1u << 20;   // 每块 arena 可用字节

  // keep_last(history)：收端最多保留的最新消息块数。
  //   * 0（默认）= 不限，等价于 block_num，写端只在借不到空块时才回收最旧块
  //   * N > 0      = 已发布块超过 N 时，写端在 Publish 里立刻丢弃最旧的，
  //                  收端因此最多看到 N 条未消费消息（超出部分被覆盖）
  // 取值会被 clamp 到 [1, block_num]。
  uint32_t history = 0;
};

struct alignas(64) SegmentHeader {
  uint64_t magic;
  uint32_t version;
  uint32_t block_num;
  uint32_t block_capacity;
  uint32_t chunk_size;        // 每块 chunk 总字节（arena 元数据 + 数据区）
  uint64_t total_size;
  uint64_t self_base;         // 创建者映射到的虚拟地址（地址握手）
  std::atomic<uint64_t> next_seq;
  uint32_t payload_base;      // 第一个 chunk 相对段首的偏移
  uint32_t history;           // keep_last 生效值（0 = 不限）
  uint32_t reserved;
};

class Segment {
 public:
  Segment() = default;
  ~Segment();

  Segment(const Segment&) = delete;
  Segment& operator=(const Segment&) = delete;

  // 优先以创建者身份建立；已存在则以打开者身份接入。两条路径都要求映射到
  // 同一固定地址，否则失败。
  bool OpenOrCreate(const std::string& channel, const SegmentConfig& cfg);

  // ---------------------------------------------------------------- 写端
  // 取一个可写块（FREE -> WRITING），arena 已 Reset。无可用块返回 nullptr。
  BlockHeader* TryAcquireWritable();

  // 记录消息对象偏移并发布（WRITING -> PUBLISHED）
  void Publish(BlockHeader* b, uint32_t msg_offset, uint64_t ts_ns);

  // 放弃写入（WRITING -> FREE）
  void AbortWrite(BlockHeader* b);

  // ---------------------------------------------------------------- 读端
  // 取 seq 最小且 > *since_seq 的已发布消息；成功后 readers 已 +1，
  // 调用方必须调 ReleaseRead。*since_seq 被推进到该消息的 seq。
  BlockHeader* AcquireRead(uint64_t* since_seq);

  void ReleaseRead(BlockHeader* b);

  // 等待新消息，最多 timeout_ns；超时返回 nullptr
  BlockHeader* WaitRead(uint64_t* since_seq, uint64_t timeout_ns);

  // ---------------------------------------------------------------- 布局
  void* BlockPayload(BlockHeader* b);
  const void* BlockPayload(const BlockHeader* b) const;

  Arena* BlockArena(BlockHeader* b);
  const Arena* BlockArena(const BlockHeader* b) const;

  // 块内消息对象；msg_offset 为 0 表示尚未写入
  template <typename T>
  T* MessageOf(BlockHeader* b) {
    if (b == nullptr || b->msg_offset == 0) return nullptr;
    return reinterpret_cast<T*>(static_cast<uint8_t*>(BlockPayload(b)) + b->msg_offset);
  }

  template <typename T>
  const T* MessageOf(const BlockHeader* b) const {
    if (b == nullptr || b->msg_offset == 0) return nullptr;
    return reinterpret_cast<const T*>(
        static_cast<const uint8_t*>(BlockPayload(b)) + b->msg_offset);
  }

  bool valid() const noexcept { return header_ != nullptr; }
  uint32_t BlockNum() const noexcept { return header_ ? header_->block_num : 0; }
  uint32_t BlockCapacity() const noexcept { return header_ ? header_->block_capacity : 0; }
  // keep_last 生效值：收端最多保留的未消费消息块数（0 表示不限）
  uint32_t History() const noexcept { return header_ ? header_->history : 0; }
  uintptr_t BaseAddress() const noexcept { return reinterpret_cast<uintptr_t>(base_); }
  const std::string& channel() const noexcept { return channel_; }

  void Dump() const;

 private:
  BlockHeader* HeaderAt(uint32_t i);
  const BlockHeader* HeaderAt(uint32_t i) const;
  static size_t CalcArenaBytes(const SegmentConfig& cfg);
  static size_t CalcTotalSize(const SegmentConfig& cfg);

  // keep_last：已发布块超过 history 时丢弃最旧的（不含刚发布的块）
  void EvictBeyondHistory();

  // 把块标记为可复用并回收其 arena。调用方必须已持有该块的 BlockGuard，
  // 且已确认 state == PUBLISHED、readers == 0。
  void RecycleLocked(BlockHeader* b, uint32_t new_state);

  void* base_ = nullptr;
  size_t total_size_ = 0;
  SegmentHeader* header_ = nullptr;
  Window window_;
  std::string channel_;
  bool is_creator_ = false;
};

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_SEGMENT_H_
