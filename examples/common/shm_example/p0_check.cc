// 崩溃恢复与类型校验的自测用例
//
// 这些行为单进程的自测覆盖不到，必须用真实的多进程场景验证：
//   * 写端在 WRITING 被 kill -9 后，块必须能被后来者回收
//   * 两端声明不同类型时必须拒绝 attach
//   * 读端等待新消息时必须被写端唤醒，而不是靠固定轮询
//   * 多进程同时申请窗口时不得分配到重叠地址
//   * 同一 Node 内同一 channel 不得创建两个 Reader
//
// 用法（配合 scripts/p0_check.sh 使用，也可手工逐条跑）：
//   p0_check hold      <ch> [n]   借 n 个块并停在 WRITING，等外部 kill -9
//   p0_check write     <ch>       连续借/发布若干帧
//   p0_check type_a    <ch>       以 MsgA 建 writer
//   p0_check type_b    <ch>       以 MsgB 建 reader（应失败）
//   p0_check type_inproc <ch>     同进程内先用 MsgA 再用 MsgB（应失败）
//   p0_check locktakeover         验证死进程留下的锁可被接管
//   p0_check waitwake  <ch>       等待一帧，打印实际耗时（验证 futex 唤醒）
//   p0_check wakesend  <ch>       睡 300ms 后发布一帧（配合 waitwake）
//   p0_check slot      <tag>      申请一个窗口并打印地址（验证跨进程互斥）
//   p0_check dupreader <ch>       同 Node 内两次 CreateReader 同一 channel

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "core/pangofly.h"
#include "core/shm/allocator.h"
#include "core/shm/arena.h"
#include "core/shm/block.h"
#include "core/shm/file_lock.h"
#include "core/shm/region.h"

namespace p0 {

struct MsgA {
  static const char* GetTypeName() { return "p0::MsgA"; }
  int64_t seq;
  ::pangofly::shm::vector<uint8_t> payload;
  explicit MsgA(::pangofly::shm::Arena& a) : seq(0), payload(a) {}
};

struct MsgB {
  static const char* GetTypeName() { return "p0::MsgB"; }
  int32_t a;
  int32_t b;
  MsgB() : a(0), b(0) {}
  explicit MsgB(::pangofly::shm::Arena& ar) : MsgB() { (void)ar; }
};

}  // namespace p0

PANGOFLY_SHM_MESSAGE(::p0::MsgA)
PANGOFLY_SHM_MESSAGE(::p0::MsgB)

namespace {

// 小块通道：块数少，崩溃几次就能把可用块耗尽，最容易暴露问题
::pangofly::shm::SegmentConfig SmallCfg() {
  ::pangofly::shm::SegmentConfig cfg;
  cfg.block_num = 3;
  cfg.block_capacity = 64 * 1024;
  return cfg;
}

int Hold(const char* channel, int nloans) {
  auto node = ::pangofly::CreateNode("p0_holder");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  if (!w) {
    std::fprintf(stderr, "HOLD writer null\n");
    return 2;
  }
  std::vector<::pangofly::shm::Sample<p0::MsgA>> held;
  for (int i = 0; i < nloans; ++i) {
    auto s = w->Loan();
    if (!s.valid()) {
      std::fprintf(stderr, "HOLD loan %d/%d failed\n", i, nloans);
      return 3;
    }
    s->seq = 12345;
    s->payload.resize(1024);
    held.push_back(std::move(s));
  }
  std::fprintf(stderr, "HOLD_READY pid=%d loans=%d\n", static_cast<int>(::getpid()),
               static_cast<int>(held.size()));
  std::fflush(stderr);
  for (;;) ::sleep(1);   // 停在 WRITING，等外部 kill -9
}

int WriteN(const char* channel, int frames) {
  auto node = ::pangofly::CreateNode("p0_writer");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  if (!w) {
    std::fprintf(stderr, "WRITE writer null\n");
    return 2;
  }
  for (int i = 0; i < frames; ++i) {
    auto s = w->Loan();
    if (!s.valid()) {
      std::fprintf(stderr, "WRITE loan failed at i=%d\n", i);
      return 4;
    }
    s->seq = 1000 + i;
    s->payload.resize(60 * 1024);   // 顶到接近单块上限
    if (!w->Publish(std::move(s))) {
      std::fprintf(stderr, "WRITE publish failed at i=%d\n", i);
      return 5;
    }
  }
  std::printf("WRITE_OK frames=%d\n", frames);
  return 0;
}

int TypeA(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_type_a");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  std::printf("TYPE_A writer=%s\n", w ? "ok" : "null");
  return w ? 0 : 2;
}

int TypeB(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_type_b");
  auto r = node->CreateReader<p0::MsgB>(channel, SmallCfg());
  std::printf("TYPE_B reader=%s\n", r ? "ok（不应发生）" : "null（预期被拒绝）");
  return r ? 9 : 0;
}

// 同一 Node 内命中已缓存 Segment 的路径也必须校验类型
int TypeInProc(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_type_inproc");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  if (!w) {
    std::printf("TYPE_INPROC first=null\n");
    return 2;
  }
  auto r = node->CreateReader<p0::MsgB>(channel, SmallCfg());
  std::printf("TYPE_INPROC first=ok second=%s\n", r ? "ok（不应发生）" : "null（预期被拒绝）");
  return r ? 9 : 0;
}

// 死进程留下的锁必须能被接管，否则任何一次崩溃都会永久卡死该块
int LockTakeover() {
  using ::pangofly::shm::BlockHeader;
  using ::pangofly::shm::ProcessToken;

  const ::pid_t child = ::fork();
  if (child == 0) ::_exit(0);   // 制造一个已退出的 pid
  int status = 0;
  ::waitpid(child, &status, 0);

  static BlockHeader header;    // 本地内存，不涉及共享内存
  const ProcessToken dead{static_cast<uint32_t>(child), 1u};
  header.lock.store(dead.Encode(), std::memory_order_release);

  const auto t0 = std::chrono::steady_clock::now();
  header.Lock();                // 若接管逻辑失效，这里会永久自旋
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  const bool mine = header.lock.load() == ::pangofly::shm::SelfToken().Encode();
  header.Unlock();

  std::printf("LOCK_TAKEOVER held_by_me=%d elapsed_us=%lld\n", mine ? 1 : 0,
              static_cast<long long>(us));
  return mine ? 0 : 6;
}

// ---------------------------------------------------------------------------
// M1：futex 唤醒
//
// 读端在写端发布之前就进入 Wait(2s)。若唤醒机制有效，它应当在写端 Publish
// 后立刻返回（耗时 ≈ 写端延时）；若退化为 100us 轮询也能通过，但耗时分布
// 会明显更粗糙。这里只验证「被唤醒且拿到正确数据」，并打印耗时供观察。
// ---------------------------------------------------------------------------
int WaitWake(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_waitwake");
  auto r = node->CreateReader<p0::MsgA>(channel, SmallCfg());
  if (!r) {
    std::fprintf(stderr, "WAIT reader null\n");
    return 2;
  }
  const auto t0 = std::chrono::steady_clock::now();
  auto v = r->Wait(2000ULL * 1000 * 1000);   // 2s
  const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
  if (!v.valid()) {
    std::printf("WAIT_TIMEOUT elapsed_us=%lld\n", static_cast<long long>(us));
    return 7;
  }
  std::printf("WAIT_WAKE seq=%llu payload=%zu elapsed_us=%lld\n",
              static_cast<unsigned long long>(v.Seq()), v->payload.size(),
              static_cast<long long>(us));
  return 0;
}

int WakeSend(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_wakesend");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  if (!w) {
    std::fprintf(stderr, "WAKESEND writer null\n");
    return 2;
  }
  ::usleep(300 * 1000);   // 让读端先进入 futex 等待
  auto s = w->Loan();
  if (!s.valid()) {
    std::fprintf(stderr, "WAKESEND loan failed\n");
    return 3;
  }
  s->seq = 777;
  s->payload.resize(2048);
  if (!w->Publish(std::move(s))) {
    std::fprintf(stderr, "WAKESEND publish failed\n");
    return 5;
  }
  std::printf("WAKESEND_OK\n");
  return 0;
}

// ---------------------------------------------------------------------------
// M3：跨进程互斥
//
// 多个进程同时 Region::Acquire 不同 channel，打印各自拿到的窗口地址。
// 若文件锁缺失，槽位分配的多步写会交错，可能让两个 channel 拿到同一段 VA。
// 调用方（脚本）负责检查所有输出地址互不相同。
// ---------------------------------------------------------------------------
int Slot(const char* tag) {
  // 用足够大的 need_bytes 强制跨多个窗口，放大交错窗口
  const size_t need = 2ULL * 64 * 1024 * 1024;
  ::pangofly::shm::Window w = ::pangofly::shm::Region::Acquire(tag, need);
  if (!w.valid()) {
    std::printf("SLOT_FAIL tag=%s\n", tag);
    return 8;
  }
  std::printf("SLOT_OK tag=%s addr=0x%lx index=%u size=%zu\n", tag,
              static_cast<unsigned long>(w.addr), w.index, w.size);
  // 不 Release：让窗口在整个子进程生命周期内保持占用，便于脚本横向比对
  return 0;
}

// ---------------------------------------------------------------------------
// M5：同 Node 内 reader 去重
// ---------------------------------------------------------------------------
int DupReader(const char* channel) {
  auto node = ::pangofly::CreateNode("p0_dupreader");
  auto w = node->CreateWriter<p0::MsgA>(channel, SmallCfg());
  if (!w) {
    std::printf("DUP first_writer=null\n");
    return 2;
  }
  auto r1 = node->CreateReader<p0::MsgA>(channel, SmallCfg());
  auto r2 = node->CreateReader<p0::MsgA>(channel, SmallCfg());
  const bool first_ok = (r1 != nullptr);
  const bool second_blocked = (r2 == nullptr);
  std::printf("DUP_READER first=%s second=%s\n", first_ok ? "ok" : "null",
              second_blocked ? "null（预期被拒绝）" : "ok（不应发生）");

  // 释放第一个 Reader 后必须允许重建，否则「销毁再重建」的用法会永久失败
  r1.reset();
  auto r3 = node->CreateReader<p0::MsgA>(channel, SmallCfg());
  const bool rebuild_ok = (r3 != nullptr);
  std::printf("DUP_REBUILD after_release=%s\n",
              rebuild_ok ? "ok" : "null（不应发生）");

  return (first_ok && second_blocked && rebuild_ok) ? 0 : 9;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <hold|write|type_a|type_b|type_inproc|locktakeover|"
                 "waitwake|wakesend|slot|dupreader> [channel] [n]\n",
                 argv[0]);
    return 1;
  }
  if (!::pangofly::Init()) {
    std::fprintf(stderr, "pangofly Init failed\n");
    return 1;
  }
  const char* mode = argv[1];
  const char* channel = (argc >= 3) ? argv[2] : "p0_channel";

  if (std::strcmp(mode, "hold") == 0) {
    return Hold(channel, (argc >= 4) ? std::atoi(argv[3]) : 1);
  }
  if (std::strcmp(mode, "write") == 0) return WriteN(channel, 6);
  if (std::strcmp(mode, "type_a") == 0) return TypeA(channel);
  if (std::strcmp(mode, "type_b") == 0) return TypeB(channel);
  if (std::strcmp(mode, "type_inproc") == 0) return TypeInProc(channel);
  if (std::strcmp(mode, "locktakeover") == 0) return LockTakeover();
  if (std::strcmp(mode, "waitwake") == 0) return WaitWake(channel);
  if (std::strcmp(mode, "wakesend") == 0) return WakeSend(channel);
  if (std::strcmp(mode, "slot") == 0) return Slot(channel);
  if (std::strcmp(mode, "dupreader") == 0) return DupReader(channel);

  std::fprintf(stderr, "unknown mode %s\n", mode);
  return 1;
}
