#ifndef PANGOFLY_CORO_SCHEDULER_H_
#define PANGOFLY_CORO_SCHEDULER_H_

// ============================================================================
// Scheduler — 单线程协程调度器
//
// 模型：
//   · 所有协程都在「调度线程」上协作式运行，协程之间无需加锁。
//   · 协程等待外部事件时挂到 Waitable 上并让出 CPU；事件由任意线程
//     （通常是共享内存的通知线程）Notify，把等待者重新放进就绪队列。
//   · 调度线程无就绪任务时用 futex 睡在 run_seq_ 上，不空转。
//
// 由此业务代码不必再写
//     while (running_) { auto v = reader.Wait(...); if (v) Handle(v); }
// 而是注册一个回调，由 Runtime 在每条消息到达时调用一次。
// ============================================================================

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "core/common/futex.h"
#include "core/coroutine/coroutine.h"

namespace pangofly {
namespace coro {

class Scheduler;
class Task;

// ---------------------------------------------------------------------------
// Waitable：可被任意线程触发的事件源
//
// 语义是「计数信号量 + 协程唤醒」：
//   Notify 先累加 pending_，再把当前所有等待者放进就绪队列。
//   Wait   先看 pending_，大于 0 就直接返回，因此「Notify 早于 Wait」不丢事件。
// ---------------------------------------------------------------------------
class Waitable {
 public:
  explicit Waitable(Scheduler* sched) : sched_(sched) {}

  // 任意线程可调用
  void Notify();

  // 只能在协程内调用。返回 false 表示超时或调度器已停止。
  bool Wait(uint64_t timeout_ns);

  // 尚未被消费的通知数
  uint32_t pending() const noexcept {
    return pending_.load(std::memory_order_acquire);
  }

  // 清空未消费的通知（不唤醒等待者）
  void ClearPending() { pending_.store(0, std::memory_order_release); }

  Scheduler* scheduler() const noexcept { return sched_; }

 private:
  friend class Scheduler;

  struct Waiter {
    Task* task = nullptr;
    bool woken = false;
  };

  Scheduler* sched_;
  std::atomic<uint32_t> pending_{0};
  std::mutex mu_;                 // 保护 waiters_
  std::vector<Waiter> waiters_;
};

class Task {
 public:
  ~Task();

  const std::string& name() const noexcept { return name_; }
  bool done() const noexcept { return done_.load(std::memory_order_acquire); }
  Coroutine* coroutine() noexcept { return coro_.get(); }

 private:
  friend class Scheduler;
  Task(Scheduler* sched, std::string name, std::function<void()> fn,
       size_t stack_size);

  Scheduler* sched_;
  std::string name_;
  std::unique_ptr<Coroutine> coro_;
  std::atomic<bool> done_{false};
};

class Scheduler {
 public:
  explicit Scheduler(size_t default_stack_size = Coroutine::kDefaultStackSize);
  ~Scheduler();

  Scheduler(const Scheduler&) = delete;
  Scheduler& operator=(const Scheduler&) = delete;

  // 创建任务并入就绪队列（不会立刻运行）
  std::shared_ptr<Task> Spawn(std::string name, std::function<void()> fn,
                              size_t stack_size = 0);

  // 把任务放进就绪队列。可被任意线程调用。
  void MakeReady(Task* task);

  // 当前协程挂起等待 w。只能在协程内调用。
  bool ParkCurrent(Waitable* w, uint64_t timeout_ns);

  // 超时检查：把到期的等待者放回就绪队列。返回下一个最近的到期时刻（ns），
  // 没有定时等待时返回 0。
  uint64_t CheckTimeouts();

  // 推进一轮就绪协程，返回实际运行的数量
  size_t RunOnce();

  // 一直运行到所有任务结束或 Stop()
  void Run();

  // 睡到有就绪任务 / 下一个超时点 / 全部结束。返回 false 表示可以退出。
  bool WaitForWork();

  void Stop();
  bool stopped() const noexcept { return stop_.load(std::memory_order_acquire); }

  size_t TaskCount() const { return tasks_.size(); }
  size_t ReadyCount() const;

  size_t default_stack_size() const noexcept { return default_stack_size_; }

  static Scheduler* Current();

 private:
  friend class Waitable;
  void WakeUp();
  Task* TaskOf(Coroutine* c);

  size_t default_stack_size_;
  std::vector<std::shared_ptr<Task>> tasks_;
  std::deque<Task*> ready_;
  mutable std::mutex mu_;              // 保护 tasks_ / ready_
  std::atomic<bool> stop_{false};
  std::atomic<uint32_t> run_seq_{0};   // futex 字

  // 定时等待登记：协程 -> (等待对象, 到期时刻)
  struct TimeoutEntry {
    Waitable* waitable = nullptr;
    uint64_t deadline_ns = 0;
  };
  std::map<Coroutine*, TimeoutEntry> timeouts_;
};

}  // namespace coro
}  // namespace pangofly

#endif  // PANGOFLY_CORO_SCHEDULER_H_
