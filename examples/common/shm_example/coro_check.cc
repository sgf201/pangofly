// ============================================================================
// 协程机制自测
//
// 覆盖：
//   1. 单协程 Resume/Yield 交替
//   2. 多协程让出与恢复（栈隔离）
//   3. 深栈使用与 guard page（栈溢出应 SIGSEGV 而非静默踩内存）
//   4. Waitable 事件唤醒
//   5. 事件先于等待（不丢通知）
//   6. Runtime 订阅：消息到达驱动回调，无 while 循环
// ============================================================================

#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/coroutine/coroutine.h"
#include "core/coroutine/runtime.h"
#include "core/coroutine/scheduler.h"
#include "core/shm/message.h"
#include "core/pangofly.h"

namespace corotest {

struct PingMsg {
  int value = 0;
  explicit PingMsg(::pangofly::shm::Arena&) {}
};

}  // namespace corotest

PANGOFLY_SHM_MESSAGE(::corotest::PingMsg)

namespace {

int g_fail = 0;

void Check(bool ok, const char* what) {
  if (ok) {
    std::printf("  [PASS] %s\n", what);
  } else {
    std::printf("  [FAIL] %s\n", what);
    ++g_fail;
  }
}

// ---------------------------------------------------------------- 1. 单协程
void TestSingleCoroutine() {
  std::printf("#### 1. 单协程 Resume/Yield\n");
  int steps = 0;
  std::vector<int> order;

  pangofly::coro::Coroutine co([&]() {
    for (int i = 0; i < 3; ++i) {
      order.push_back(i);
      ++steps;
      pangofly::coro::Yield();
    }
    order.push_back(99);
  });

  int resumes = 0;
  while (co.Resume()) ++resumes;

  Check(steps == 3, "协程执行了 3 步");
  Check(co.done(), "协程已结束");
  Check(order.size() == 4 && order.back() == 99, "顺序与收尾正确");
  Check(resumes >= 3, "Resume 次数 >= 3");
}

// ---------------------------------------------------------------- 2. 多协程
void TestMultiCoroutine() {
  std::printf("#### 2. 多协程栈隔离\n");
  constexpr int kN = 4;
  constexpr int kRounds = 5;

  std::vector<int> hits(kN, 0);
  // 每个协程在自己的栈上放大数组，验证栈不互相踩
  // 注意：Coroutine 既不可拷贝也不可移动（自身持有栈映射），因此用 unique_ptr
  std::vector<std::unique_ptr<pangofly::coro::Coroutine>> cos;
  for (int i = 0; i < kN; ++i) {
    cos.push_back(std::make_unique<pangofly::coro::Coroutine>([&hits, i]() {
      char buf[2048];
      std::memset(buf, i + 1, sizeof(buf));
      for (int r = 0; r < kRounds; ++r) {
        // 校验缓冲区未被别的协程改写
        bool intact = true;
        for (char c : buf) {
          if (static_cast<unsigned char>(c) != i + 1) { intact = false; break; }
        }
        if (!intact) {
          std::printf("  [FAIL] 协程 %d 栈内容被破坏\n", i);
          return;
        }
        hits[i]++;
        pangofly::coro::Yield();
      }
    }));
  }

  bool any = true;
  while (any) {
    any = false;
    for (auto& c : cos) {
      if (!c->done()) {
        c->Resume();
        if (!c->done()) any = true;
      }
    }
  }

  bool all = true;
  for (int i = 0; i < kN; ++i) {
    if (hits[i] != kRounds) all = false;
  }
  Check(all, "4 个协程各完成 5 轮，栈互不干扰");
}

// ---------------------------------------------------------------- 3. guard page
void TestGuardPage() {
  std::printf("#### 3. 栈溢出落在 guard page\n");

  pid_t pid = ::fork();
  if (pid == 0) {
    // 子进程：故意把栈写穿，期望 SIGSEGV
    pangofly::coro::Coroutine co([]() {
      volatile char buf[512 * 1024];
      for (size_t i = 0; i < sizeof(buf); i += 4096) buf[i] = 1;
      (void)buf;
    }, "overflow", 64 * 1024);
    co.Resume();
    ::_exit(0);  // 若走到这里说明没被 guard page 拦住
  }
  int status = 0;
  ::waitpid(pid, &status, 0);
  const bool segv = WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
  Check(segv, "栈溢出触发 SIGSEGV（guard page 生效）");
}

// ---------------------------------------------------------------- 4. 事件唤醒
// 更直接的事件测试：先创建 Waitable，再派协程等它
void TestWaitable2() {
  std::printf("#### 4b. Waitable 唤醒（含先发后等）\n");
  pangofly::coro::Scheduler sched;
  auto sig = std::make_shared<pangofly::coro::Waitable>(&sched);

  int received = 0;
  sched.Spawn("waiter", [&]() {
    for (int i = 0; i < 3; ++i) {
      if (sig->Wait(2000ULL * 1000 * 1000)) ++received;
    }
  });

  for (int i = 0; i < 3; ++i) {
    sched.RunOnce();
    sig->Notify();
    // 让调度器把被唤醒的协程跑起来
    for (int k = 0; k < 10 && received <= i; ++k) sched.RunOnce();
  }
  Check(received == 3, "3 次 Notify 全部被消费");

  // 先 Notify 再 Wait：不应丢
  auto sig2 = std::make_shared<pangofly::coro::Waitable>(&sched);
  sig2->Notify();
  sig2->Notify();
  int got = 0;
  sched.Spawn("waiter2", [&]() {
    while (sig2->Wait(100ULL * 1000 * 1000)) ++got;
  });
  for (int k = 0; k < 20; ++k) sched.RunOnce();
  Check(got == 2, "Notify 早于 Wait 不丢事件");
}

// ---------------------------------------------------------------- 5. 超时
void TestTimeout() {
  std::printf("#### 5. Wait 超时\n");
  pangofly::coro::Scheduler sched;
  auto sig = std::make_shared<pangofly::coro::Waitable>(&sched);

  bool timed_out = false;
  sched.Spawn("waiter", [&]() {
    // 50ms 超时，无人 Notify
    timed_out = !sig->Wait(50ULL * 1000 * 1000);
  });

  const auto t0 = pangofly::NowNs();
  for (int i = 0; i < 200; ++i) {
    sched.RunOnce();
    if (timed_out) break;
    sched.WaitForWork();
  }
  const uint64_t elapsed_ms = (pangofly::NowNs() - t0) / 1000000ULL;
  Check(timed_out, "无事件时 Wait 超时返回 false");
  Check(elapsed_ms >= 45 && elapsed_ms < 500, "超时时长大致准确（~50ms）");
}

// ---------------------------------------------------------------- 6. Runtime
void TestRuntimeSubscribe() {
  std::printf("#### 6. Runtime 订阅驱动（无 while 循环）\n");

  auto node = ::pangofly::CreateNode("coro_test");
  if (!node) {
    Check(false, "CreateNode 失败");
    return;
  }
  const std::string ch = "coro_ping_chan";
  ::pangofly::shm::SegmentConfig cfg;
  cfg.block_num = 16;
  cfg.block_capacity = 4096;

  auto w = node->CreateWriter<::corotest::PingMsg>(ch, cfg);
  if (!w) {
    Check(false, "CreateWriter 失败");
    return;
  }

  ::pangofly::coro::Runtime rt;
  std::atomic<int> handled{0};
  std::atomic<int> sum{0};

  auto seg = node->GetSegment(
      ch, cfg, ::pangofly::detail::MessageTypeName<::corotest::PingMsg>());
  if (!seg) {
    Check(false, "GetSegment 失败");
    return;
  }

  // 关键：这里没有 while，只有「每来一条消息调用一次」的回调
  bool ok = rt.Subscribe<::corotest::PingMsg>(
      seg, [&](::pangofly::shm::View<::corotest::PingMsg> v) {
        handled.fetch_add(1);
        sum.fetch_add(v->value);
      });
  Check(ok, "Subscribe 成功");

  constexpr int kN = 10;
  for (int i = 1; i <= kN; ++i) {
    w->Write([i](::corotest::PingMsg& m) { m.value = i; });
  }

  const auto t0 = ::pangofly::NowNs();
  while (handled.load() < kN &&
         (::pangofly::NowNs() - t0) < 2000ULL * 1000 * 1000) {
    rt.RunOnce();
    rt.scheduler()->WaitForWork();
  }

  Check(handled.load() == kN, "10 条消息全部被回调处理");
  Check(sum.load() == kN * (kN + 1) / 2, "数据内容正确（累加和）");
  rt.Stop();
}

}  // namespace

int main() {
  std::printf("==== pangofly 协程自测 ====\n\n");
  if (!pangofly::Init()) {
    std::printf("pangofly::Init 失败\n");
    return 1;
  }

  TestSingleCoroutine();
  TestMultiCoroutine();
  TestGuardPage();
  TestWaitable2();
  TestTimeout();
  TestRuntimeSubscribe();

  std::printf("\n==== %s (失败 %d 项) ====\n",
              g_fail == 0 ? "ALL COROUTINE TESTS PASSED" : "TESTS FAILED", g_fail);
  pangofly::Shutdown();
  return g_fail == 0 ? 0 : 1;
}
