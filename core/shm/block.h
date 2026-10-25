#ifndef PANGOFLY_V2_BLOCK_H_
#define PANGOFLY_V2_BLOCK_H_

// ============================================================================
// BlockHeader — 共享内存块头与状态机
//
// 旧实现只有一个 msg_size_ + sequence_，无法区分「正在写」与「已发布」，
// 也无法防止同一块被反复读到。新设计引入四态 + generation + refcount。
//
//   FREE --CAS--> WRITING --publish--> PUBLISHED --last reader--> FREE
//     ^                |
//     +---- abort -----+
//
// 所有状态迁移都在块内自旋锁 protect 下完成，避免「读者刚拿到块、写者就
// 复用」的竞态。锁只用几条原子指令，相对于跨进程通信本身可忽略。
//
// 布局（64 字节，单 cache line）：
//   +0  magic / +8 version / +12 index
//   +16 lock / +20 state / +24 generation / +28 readers
//   +32 capacity / +36 used / +40 arena_offset / +44 msg_offset
//   +48 seq / +56 timestamp_ns
// ============================================================================

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace pangofly {
namespace shm {

constexpr uint64_t kBlockMagic   = 0x5046424B30303032ULL;  // "PFBK0002"
constexpr uint32_t kBlockVersion = 2;

enum class BlockState : uint32_t {
  FREE = 0,
  WRITING = 1,
  PUBLISHED = 2,
};

struct alignas(64) BlockHeader {
  uint64_t magic;
  uint32_t version;
  uint32_t index;

  std::atomic<uint32_t> lock;         // 0 = 未加锁，1 = 已加锁（跨进程自旋锁）
  std::atomic<uint32_t> state;        // BlockState

  uint32_t generation;                // 每次复用 +1（仅在 BlockGuard 内读写）
  std::atomic<uint32_t> readers;      // 写端回收扫描会无锁读，必须原子

  uint32_t capacity;                  // 该块 arena 可用字节
  uint32_t used;                      // arena 实际使用字节
  uint32_t arena_offset;              // arena 元数据相对块 payload 的偏移（当前为 0）
  uint32_t msg_offset;                // T 对象相对块 payload 的偏移

  std::atomic<uint64_t> seq;          // 写端回收扫描会无锁读，必须原子
  uint64_t timestamp_ns;

  void Lock() noexcept {
    uint32_t expected = 0;
    while (!lock.compare_exchange_weak(expected, 1, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
      expected = 0;
    }
  }
  void Unlock() noexcept { lock.store(0, std::memory_order_release); }
};

static_assert(sizeof(BlockHeader) == 64, "BlockHeader must be exactly one cache line");

// RAII 保护块内自旋锁
class BlockGuard {
 public:
  explicit BlockGuard(BlockHeader* b) noexcept : b_(b) {
    if (b_ != nullptr) b_->Lock();
  }
  ~BlockGuard() {
    if (b_ != nullptr) b_->Unlock();
  }
  BlockGuard(const BlockGuard&) = delete;
  BlockGuard& operator=(const BlockGuard&) = delete;

 private:
  BlockHeader* b_;
};

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_BLOCK_H_
