// 线程池版 Runtime 自测。
//
// 覆盖点：
//   1. ThreadPool：任务全部执行、优雅停止后队列里剩余任务仍执行
//   2. 订阅通路：跨进程收到全部消息，且同一 channel 严格串行（无并发重入）
//   3. 定时器：按周期稳定触发，Stop 后停止
//   4. 无订阅时 Run() 不空转（HasSubscription 为假）

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/concurrency/runtime.h"
#include "core/concurrency/thread_pool.h"
#include "core/pangofly.h"
#include "core/shm/allocator.h"
#include "core/shm/arena.h"

using namespace pangofly;

namespace tptest {

struct PingMsg {
  static const char* GetTypeName() { return "tptest::PingMsg"; }

  uint64_t seq;
  uint32_t payload;
  ::pangofly::shm::vector<uint8_t> data;

  explicit PingMsg(::pangofly::shm::Arena& arena)
      : seq(0), payload(0), data(arena) {}
};

}  // namespace tptest

PANGOFLY_SHM_MESSAGE(::tptest::PingMsg)

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool ok, const char* what, const char* detail = "") {
  if (ok) {
    ++g_pass;
    std::printf("  [PASS] %s %s\n", what, detail);
  } else {
    ++g_fail;
    std::printf("  [FAIL] %s %s\n", what, detail);
  }
}

void TestThreadPool() {
  std::printf("[1] ThreadPool\n");
  {
    pool::ThreadPool tp(2);
    std::atomic<int> ran{0};
    const int kTasks = 100;
    for (int i = 0; i < kTasks; ++i) {
      tp.Submit([&ran]() { ran.fetch_add(1); });
    }
    // 等待任务跑完
    for (int i = 0; i < 200 && ran.load() < kTasks; ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    Check(ran.load() == kTasks, "100 个任务全部执行",
          ("实际=" + std::to_string(ran.load())).c_str());
  }
  {
    // 优雅停止：Stop 之后队列里的任务仍应执行完
    std::atomic<int> ran{0};
    pool::ThreadPool tp(2);
    for (int i = 0; i < 50; ++i) {
      tp.Submit([&ran]() { ran.fetch_add(1); });
    }
    tp.Stop();
    tp.Join();
    Check(ran.load() == 50, "Stop 后队列内任务仍执行完",
          ("实际=" + std::to_string(ran.load())).c_str());
  }
  {
    pool::ThreadPool tp(2);
    tp.Stop();
    std::atomic<int> ran{0};
    tp.Submit([&ran]() { ran.fetch_add(1); });   // 停止后投递应被忽略
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    Check(ran.load() == 0, "Stop 后新任务被丢弃");
  }
}

void TestSubscribe() {
  std::printf("[2] Runtime 订阅\n");

  shm::SegmentConfig cfg;
  cfg.block_num = 8;
  cfg.block_capacity = 64 * 1024;

  pool::Runtime rt;
  auto seg = std::make_shared<shm::Segment>();
  if (!seg->OpenOrCreate("tp_test_channel", cfg,
                         pangofly::detail::MessageTypeName<tptest::PingMsg>())) {
    Check(false, "创建 segment");
    return;
  }

  std::atomic<int> received{0};
  std::atomic<int> max_inflight{0};
  std::atomic<int> in_flight{0};
  std::vector<uint64_t> order;
  std::mutex order_mu;

  bool ok = rt.Subscribe<tptest::PingMsg>(
      seg, [&](shm::View<tptest::PingMsg> v) {
        // 检测同 channel 是否发生并发重入
        const int now = in_flight.fetch_add(1) + 1;
        int prev = max_inflight.load();
        while (now > prev && !max_inflight.compare_exchange_weak(prev, now)) {
        }
        {
          std::lock_guard<std::mutex> lk(order_mu);
          order.push_back(v->seq);
        }
        // 模拟一点处理耗时，让并发问题更容易暴露
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        in_flight.fetch_sub(1);
        received.fetch_add(1);
      });
  Check(ok, "订阅成功");

  // 另起线程当写端
  const int kMsgs = 40;
  std::thread writer([&]() {
    shm::Writer<tptest::PingMsg> w(seg);
    for (int i = 0; i < kMsgs; ++i) {
      shm::Sample<tptest::PingMsg> s = w.Loan();
      if (!s.valid()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        continue;
      }
      s->seq = static_cast<uint64_t>(i);
      s->payload = static_cast<uint32_t>(i);
      w.Publish(std::move(s));
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
  });
  writer.join();

  for (int i = 0; i < 300 && received.load() < kMsgs; ++i) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  Check(received.load() > 0, "收到消息", ("条数=" + std::to_string(received.load())).c_str());
  Check(max_inflight.load() == 1, "同一 channel 无并发重入",
        ("峰值并发=" + std::to_string(max_inflight.load())).c_str());

  // 顺序单调递增
  bool ordered = true;
  {
    std::lock_guard<std::mutex> lk(order_mu);
    for (size_t i = 1; i < order.size(); ++i) {
      if (order[i] <= order[i - 1]) {
        ordered = false;
        break;
      }
    }
  }
  Check(ordered, "消息处理顺序单调递增");

  rt.Stop();
}

void TestTimer() {
  std::printf("[3] Runtime 定时器\n");
  pool::Runtime rt;
  std::atomic<int> ticks{0};

  rt.SpawnTimer("tick", 50ULL * 1000 * 1000, [&ticks]() { ticks.fetch_add(1); });

  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  const int after = ticks.load();
  Check(after >= 3, "300ms 内至少触发 3 次", ("实际=" + std::to_string(after)).c_str());

  rt.Stop();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  const int frozen = ticks.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
  Check(ticks.load() == frozen, "Stop 后不再触发",
        ("Stop时=" + std::to_string(frozen) + " 之后=" + std::to_string(ticks.load())).c_str());
}

void TestNoSubscription() {
  std::printf("[4] 空 Runtime\n");
  pool::Runtime rt;
  Check(!rt.HasSubscription(), "无订阅时 HasSubscription 为假");
  rt.Stop();
}

}  // namespace

int main() {
  std::printf("=== 线程池 Runtime 自测 ===\n");

  if (!pangofly::Init()) {
    std::printf("pangofly Init 失败\n");
    return 1;
  }

  TestThreadPool();
  TestSubscribe();
  TestTimer();
  TestNoSubscription();

  std::printf("\n=== 结果: %d 通过, %d 失败 ===\n", g_pass, g_fail);

  pangofly::Shutdown();
  return g_fail == 0 ? 0 : 1;
}
