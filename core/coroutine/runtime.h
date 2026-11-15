#ifndef PANGOFLY_CORO_RUNTIME_H_
#define PANGOFLY_CORO_RUNTIME_H_

// ============================================================================
// Runtime — 把 Node 的订阅变成协程任务
//
// 目的：业务代码不再写
//     while (running_) {
//       auto v = reader->Wait(100ms);
//       if (v.valid()) OnImage(v);
//     }
// 而是注册一个回调：
//     runtime.Spawn("recog", [](shm::View<ImageFrame> v) { ... });
// Runtime 内部用一条通知线程 + 协程调度器把「消息到达」翻译成「回调执行」。
//
// 线程模型：
//   · 通知线程：对每个订阅的 segment 调用 WaitNotify，醒来后 Notify 对应
//     的 Waitable。它不执行任何业务逻辑，只搬运事件。
//   · 调度线程：调用 Run() 的那个线程，所有业务回调都在这里顺序执行。
//     因此回调之间天然没有并发问题，可以放心使用非线程安全的数据结构。
//
// 生命周期：Runtime 析构时会停止通知线程并等待其退出。
// ============================================================================

#include <atomic>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/coroutine/scheduler.h"
#include "core/shm/node.h"
#include "core/shm/segment.h"

namespace pangofly {
namespace coro {

class Runtime {
 public:
  explicit Runtime(size_t default_stack_size = Coroutine::kDefaultStackSize);
  ~Runtime();

  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;

  Scheduler* scheduler() noexcept { return &sched_; }

  // 注册一个订阅：channel 上每来一条新消息，就在调度线程上调用一次 handler。
  //
  // handler 的签名是 void(shm::View<MessageT>)，View 在回调返回后失效，
  // 需要留存数据时请在回调内拷贝。
  //
  // 返回 false 表示该 channel 已有订阅（同一 channel 只允许一个消费方，
  // 与 Node::CreateReader 的去重规则一致）。
  template <typename MessageT, typename Fn>
  bool Subscribe(std::shared_ptr<shm::Segment> seg, Fn&& handler);

  // 起一个周期任务：每 period_ns 调用一次 fn（在调度线程上）
  std::shared_ptr<Task> SpawnTimer(std::string name, uint64_t period_ns,
                                   std::function<void()> fn);

  // 起一个一次性任务
  std::shared_ptr<Task> Spawn(std::string name, std::function<void()> fn);

  // 推进调度器直到所有任务结束或 Stop()
  void Run() { sched_.Run(); }
  void Stop();

  // 处理一轮就绪协程。供外部已有主循环的场景使用。
  size_t RunOnce() { return sched_.RunOnce(); }

 private:
  // 一个订阅的运行实体
  struct Subscription {
    std::string channel;
    std::shared_ptr<shm::Segment> seg;
    std::shared_ptr<Waitable> signal;
    std::function<void()> drain;  // 取一条消息并调用 handler
    std::atomic<bool> stopped{false};
  };

  void PollLoop();

  Scheduler sched_;
  std::vector<std::shared_ptr<Subscription>> subs_;
  std::mutex subs_mu_;

  std::thread poller_;
  std::atomic<bool> running_{true};

  // 停止通知线程用的条件变量（futex 等待有超时，这里只用于快速响应退出）
  std::mutex stop_mu_;
  std::condition_variable stop_cv_;
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
  sub->signal = std::make_shared<Waitable>(&sched_);

  // Reader 必须在调度线程上独占使用：它持有 since_seq_。
  // 这里把它放进 shared_ptr 由协程持有，drain() 只在调度线程上被调用。
  auto reader = std::make_shared<shm::Reader<MessageT>>(sub->seg);
  auto h = std::forward<Fn>(handler);

  sub->drain = [reader, h, sub]() {
    // 一次把当前所有可用消息取完，避免每条消息都唤醒一次调度器
    for (int i = 0; i < 64; ++i) {
      shm::View<MessageT> v = reader->Acquire();
      if (!v.valid()) return;
      h(v);
      if (sub->stopped.load(std::memory_order_acquire)) return;
    }
  };

  // 订阅协程：等信号 -> 排空 -> 继续等
  sched_.Spawn("sub:" + channel, [sub]() {
    while (!sub->stopped.load(std::memory_order_acquire)) {
      // 用有限超时而不是无限等待：Stop() 时协程能自己看到 stopped 标志退出，
      // 无需依赖外部打断 futex。
      sub->signal->Wait(50ULL * 1000 * 1000);
      sub->drain();
    }
  });

  {
    std::lock_guard<std::mutex> lk(subs_mu_);
    subs_.push_back(sub);
  }

  if (!poller_.joinable()) {
    poller_ = std::thread([this]() { PollLoop(); });
  }
  stop_cv_.notify_all();
  return true;
}

}  // namespace coro
}  // namespace pangofly

#endif  // PANGOFLY_CORO_RUNTIME_H_
