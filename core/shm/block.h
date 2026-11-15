#ifndef PANGOFLY_V2_BLOCK_H_
#define PANGOFLY_V2_BLOCK_H_

// ============================================================================
// BlockHeader — 共享内存块头与状态机
//
// 旧实现只有一个 msg_size_ + sequence_，无法区分「正在写」与「已发布」，
// 也无法防止同一块被反复读到。新设计引入三态 + generation + refcount。
//
//   FREE --CAS--> WRITING --publish--> PUBLISHED --writer reclaim--> WRITING
//     ^                |
//     +---- abort -----+
//
// 所有状态迁移都在块内自旋锁 protect 下完成，避免「读者刚拿到块、写者就
// 复用」的竞态。锁只用几条原子指令，相对于跨进程通信本身可忽略。
//
// 崩溃恢复
//   自旋锁与 WRITING 状态都记录了持有者的进程身份（pid + 进程启动时刻）。
//   抢锁失败或扫描到 WRITING 块时，若判定持有者已退出（kill -9、崩溃），
//   可以强行接管，块不会永久卡死。
//   只记 pid 是不够的：pid 会被内核复用，必须配合 /proc/<pid>/stat 的
//   starttime 才能唯一标识一个进程实例。
//
// 布局（64 字节，单 cache line）：
//   +0  magic / +8 version / +12 index
//   +16 lock（持有者 token）/ +24 state / +28 readers
//   +32 generation / +36 msg_offset
//   +40 owner_pid / +44 owner_start
//   +48 seq / +56 timestamp_ns
// ============================================================================

#include <sched.h>
#include <unistd.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace pangofly {
namespace shm {

constexpr uint64_t kBlockMagic   = 0x5046424B30303033ULL;  // "PFBK0003"
constexpr uint32_t kBlockVersion = 3;

enum class BlockState : uint32_t {
  FREE = 0,
  WRITING = 1,
  PUBLISHED = 2,
};

// ---------------------------------------------------------------------------
// 进程身份：pid + 进程启动时刻
//
// 光有 pid 不足以判活——进程退出后 pid 会被复用，一个已死的写端可能被
// 「同号不同命」的新进程冒充。starttime 取自 /proc/<pid>/stat 第 22 字段
// （单位 clock tick），同一 pid 每次启动都不同，二者组合才唯一标识一个
// 进程实例。
// ---------------------------------------------------------------------------
struct ProcessToken {
  uint32_t pid = 0;
  uint32_t start = 0;

  uint64_t Encode() const noexcept {
    return (static_cast<uint64_t>(pid) << 32) | start;
  }
  static ProcessToken Decode(uint64_t v) noexcept {
    return ProcessToken{static_cast<uint32_t>(v >> 32), static_cast<uint32_t>(v)};
  }
};

// 读 /proc/<pid>/stat 的 starttime。进程不存在或 /proc 不可读时返回 false。
inline bool ReadProcessStart(uint32_t pid, uint32_t* out_start) noexcept {
  char path[64];
  std::snprintf(path, sizeof(path), "/proc/%u/stat", pid);
  std::FILE* f = std::fopen(path, "r");
  if (f == nullptr) return false;
  char buf[512];
  const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
  std::fclose(f);
  if (n == 0) return false;
  buf[n] = '\0';

  // 第 2 字段 comm 被括号包裹且可能含空格，先跳过它再按空格切分
  const char* p = std::strrchr(buf, ')');
  if (p == nullptr) return false;
  ++p;

  int field = 3;   // p 之后从第 3 个字段（state）开始
  const char* cur = p;
  while (*cur != '\0') {
    while (*cur == ' ') ++cur;
    if (*cur == '\0') break;
    const char* begin = cur;
    while (*cur != ' ' && *cur != '\0') ++cur;
    if (field == 22) {
      *out_start = static_cast<uint32_t>(std::strtoul(begin, nullptr, 10));
      return true;
    }
    ++field;
  }
  return false;
}

// 本进程身份，进程内只解析一次
inline ProcessToken SelfToken() noexcept {
  static const ProcessToken token = [] {
    ProcessToken t;
    t.pid = static_cast<uint32_t>(::getpid());
    if (!ReadProcessStart(t.pid, &t.start)) t.start = 0;
    return t;
  }();
  return token;
}

// token 对应的进程是否仍存活。
// start == 0 表示该 token 无法比对（例如 /proc 不可用），保守返回 true：
// 宁可让块等到写端下次正常 Abort，也不要误抢一个还活着的写端。
inline bool IsProcessAlive(const ProcessToken& t) noexcept {
  if (t.pid == 0) return false;
  if (t.start == 0) return true;
  uint32_t start = 0;
  if (!ReadProcessStart(t.pid, &start)) return false;
  return start == t.start;
}

struct alignas(64) BlockHeader {
  uint64_t magic;
  uint32_t version;
  uint32_t index;

  // 0 = 未加锁；否则为持有者 token（pid << 32 | start）。
  // 存 token 而非 0/1，是为了在抢锁失败时判断持有者是否还活着。
  std::atomic<uint64_t> lock;
  std::atomic<uint32_t> state;        // BlockState
  std::atomic<uint32_t> readers;      // 写端回收扫描会无锁读，必须原子

  uint32_t generation;                // 每次复用 +1（仅在 BlockGuard 内读写）
  uint32_t msg_offset;                // T 对象相对块 payload 的偏移

  // WRITING 状态的持有者。写端在 Publish / Abort 前崩溃时，后来者据此
  // 判定该块是否已成僵尸（仅在 BlockGuard 内读写）。
  uint32_t owner_pid;
  uint32_t owner_start;

  std::atomic<uint64_t> seq;          // 写端回收扫描会无锁读，必须原子
  uint64_t timestamp_ns;

  // 加锁；若持有者已退出则强行接管，保证崩溃不会留下永久死锁
  void Lock() noexcept {
    const uint64_t me = SelfToken().Encode();
    for (;;) {
      uint64_t observed = 0;
      if (lock.compare_exchange_strong(observed, me, std::memory_order_acquire,
                                       std::memory_order_relaxed)) {
        return;
      }
      const ProcessToken holder = ProcessToken::Decode(observed);
      if (!IsProcessAlive(holder)) {
        // 持有者已退出，接管它留下的锁
        uint64_t expect = observed;
        if (lock.compare_exchange_strong(expect, me, std::memory_order_acquire,
                                         std::memory_order_relaxed)) {
          return;
        }
      } else {
        ::sched_yield();
      }
    }
  }

  void Unlock() noexcept { lock.store(0, std::memory_order_release); }

  // WRITING 持有者是否还活着。未记录持有者时保守返回 true。
  bool OwnerAlive() const noexcept {
    if (owner_pid == 0) return true;
    return IsProcessAlive(ProcessToken{owner_pid, owner_start});
  }
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
