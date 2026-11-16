#ifndef PANGOFLY_CONCURRENCY_RUNTIME_H_
#define PANGOFLY_CONCURRENCY_RUNTIME_H_

// ============================================================================
// Runtime —— 用线程池驱动 Node 的订阅与定时器
//
// 目的与协程版一致：业务代码不再写
//     while (running_) { auto v = reader->Wait(100ms); if (v) OnImage(v); }
// 而是注册一个回调：
//     rt.Subscribe<ImageFrame>(seg, [this](shm::View<ImageFrame> v) { ... });
//     rt.Run();
//
// 线程模型（与协程版的区别就在这里）：
//   · 投递线程：唯一一条线程，阻塞在各个 segment 的 notify_seq 上；
//     收到通知后把「排空该 channel」的任务投给线程池。它不做业务。
//   · 工作线程池：真正执行回调。线程数默认按硬件并发数取 2..4。
//   · 定时器线程：按周期把任务投给线程池。
//
// 并发约定（重要）：
//   · 同一个 channel 的回调严格串行——排空任务在任一时刻只有一个在跑，
//     Reader::since_seq_ 才能安全使用，消息顺序也不会乱。
//   · 不同 channel 的回调可能并发执行。跨 channel 共享的状态需要调用方
//     自己加锁；单 channel 节点（如当前的识别/显示节点）不需要。
// ============================================================================

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/concurrency/thread_pool.h"
#include "core/shm/segment.h"

namespace pangofly {
namespace pool {

class Runtime {
 public:
  // workers == 0 表示自动（2..4）
  explicit Runtime(size_t workers = 0);
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  // 注册一个订阅：channel 上每来一条新消息，就在工作线程上调用一次 handler。
  // handler 的签名是 void(shm::View<MessageT>)；View 在回调返回后失效，
  // 需要留存数据时请在回调内拷贝。
  //
  // 返回 false 表示该 channel 已有订阅（同一 channel 只允许一个消费方，
  // 与 Node::CreateReader 的去重规则一致）。
  template <typename MessageT, typename Fn>
  bool Subscribe(std::shared_ptr<shm::Segment> seg, Fn&& handler);

  // 周期任务：每 period_ns 投递一次 fn
  void SpawnTimer(std::string name, uint64_t period_ns, std::function<void()> fn);

  // 一次性任务
  void Post(std::function<void()> fn);

  // 是否有活跃订阅。没有订阅时 Run() 会立刻返回，避免误用成空转。
  bool HasSubscription() const;

  // 阻塞直到 Stop()。所有订阅与定时器都跑在后台线程上。
  void Run();

  // 停止投递与定时器，并等待线程池把手上的任务做完
  void Stop();

 private:
  struct Subscription {
    std::string channel;
    std::shared_ptr<shm::Segment> seg;
    std::function<bool()> drain;          // 排空消息；返回 true 表示可能还有剩余
    std::atomic<uint64_t> notify_gen{0};  // 投递线程每次唤醒 +1
    std::atomic<bool> scheduled{false};   // 是否已有排空任务排队/在跑
    std::atomic<bool> stopped{false};
  };

  struct Timer {
    std::string name;
    uint64_t period_ns = 0;
    uint64_t next_ns = 0;
    std::function<void()> fn;
    std::atomic<bool> cancelled{false};
  };

  void DispatchLoop();
  void TimerLoop();
  void ScheduleDrain(const std::shared_ptr<Subscription>& sub);

  ThreadPool workers_;
  std::vector<std::shared_ptr<Subscription>> subs_;
  mutable std::mutex subs_mu_;

  std::thread dispatcher_;
  std::thread timer_thread_;
  std::atomic<bool> running_{true};

  std::mutex timer_mu_;
  std::condition_variable timer_cv_;
  std::vector<std::shared_ptr<Timer>> timers_;

  // Run() / Stop() 同步
  std::mutex run_mu_;
  std::condition_variable run_cv_;
};

// ---------------------------------------------------------------------------
// 模板实现
// ---------------------------------------------------------------------------
template <typename MessageT, typename Fn>
bool Runtime::Subscribe(std::shared_ptr<shm::Segment> seg, Fn&& handler) {
  if (!seg || !seg->valid()) return false;

  const std::string channel = seg->channel();
  {
    std::lock_guard<std::mutex> lk(subs_mu_);
    for (const auto& s : subs_) {
      if (s->channel == channel) return false;
    }
  }

  auto sub = std::make_shared<Subscription>();
  sub->channel = channel;
  sub->seg = std::move(seg);

  // Reader 持有 since_seq_，只能被「同一时刻的一个任务」使用。
  // ScheduleDrain 用 scheduled 标志保证了这一点。
  auto reader = std::make_shared<shm::Reader<MessageT>>(sub->seg);
  auto h = std::forward<Fn>(handler);

  sub->drain = [reader, h, sub]() -> bool {
    // 一次把当前所有可用消息取完，避免每条消息都投递一次任务。
    // 取满一批仍可能有余量，返回 true 让调用方继续排下一批。
    for (int i = 0; i < 64; ++i) {
      shm::View<MessageT> v = reader->Acquire();
      if (!v.valid()) return false;
      h(v);
      if (sub->stopped.load(std::memory_order_acquire)) return false;
    }
    return true;
  };

  {
    std::lock_guard<std::mutex> lk(subs_mu_);
    subs_.push_back(sub);
  }

  if (!dispatcher_.joinable()) {
    dispatcher_ = std::thread([this]() { DispatchLoop(); });
  }
  if (!timer_thread_.joinable()) {
    timer_thread_ = std::thread([this]() { TimerLoop(); });
  }
  return true;
}

}  // namespace pool
}  // namespace pangofly

#endif  // PANGOFLY_CONCURRENCY_RUNTIME_H_
