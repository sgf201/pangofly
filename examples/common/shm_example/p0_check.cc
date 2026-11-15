// 崩溃恢复与类型校验的自测用例
//
// 这两项都是运行期行为，单进程的自测覆盖不到，必须用真实的多进程场景验证：
//   * 写端在 WRITING 被 kill -9 后，块必须能被后来者回收
//   * 两端声明不同类型时必须拒绝 attach
//
// 用法（配合 scripts/p0_check.sh 使用，也可手工逐条跑）：
//   p0_check hold      <ch> [n]   借 n 个块并停在 WRITING，等外部 kill -9
//   p0_check write     <ch>       连续借/发布若干帧
//   p0_check type_a    <ch>       以 MsgA 建 writer
//   p0_check type_b    <ch>       以 MsgB 建 reader（应失败）
//   p0_check type_inproc <ch>     同进程内先用 MsgA 再用 MsgB（应失败）
//   p0_check locktakeover         验证死进程留下的锁可被接管

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

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr,
                 "usage: %s <hold|write|type_a|type_b|type_inproc|locktakeover> [channel] [n]\n",
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

  std::fprintf(stderr, "unknown mode %s\n", mode);
  return 1;
}
