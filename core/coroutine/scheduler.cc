#include "core/coroutine/scheduler.h"

#include <algorithm>
#include <climits>

#include "core/common/log.h"

namespace pangofly {
namespace coro {

static thread_local Scheduler* t_current_sched = nullptr;

Scheduler* Scheduler::Current() { return t_current_sched; }

// ---------------------------------------------------------------------------
// Waitable
// ---------------------------------------------------------------------------
void Waitable::Notify() {
  // 先累加计数，保证「Notify 早于 Wait」时 Wait 立即返回、不丢事件；
  // 再把等待者放进就绪队列（此时可能还没有等待者，那也没关系）。
  pending_.fetch_add(1, std::memory_order_acq_rel);

  std::vector<Task*> to_wake;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto& w : waiters_) {
      if (!w.woken) {
        w.woken = true;
        to_wake.push_back(w.task);
      }
    }
  }
  if (sched_ != nullptr) {
    for (Task* t : to_wake) sched_->MakeReady(t);
  }
}

bool Waitable::Wait(uint64_t timeout_ns) {
  if (sched_ == nullptr) return false;
  return sched_->ParkCurrent(this, timeout_ns);
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------
Task::Task(Scheduler* sched, std::string name, std::function<void()> fn,
           size_t stack_size)
    : sched_(sched), name_(std::move(name)) {
  coro_ = std::make_unique<Coroutine>(
      [this, fn = std::move(fn)]() {
        fn();
        done_.store(true, std::memory_order_release);
      },
      name_, stack_size);
}

Task::~Task() = default;

// ---------------------------------------------------------------------------
// Scheduler
// ---------------------------------------------------------------------------
Scheduler::Scheduler(size_t default_stack_size)
    : default_stack_size_(default_stack_size) {}

Scheduler::~Scheduler() {
  if (t_current_sched == this) t_current_sched = nullptr;
}

std::shared_ptr<Task> Scheduler::Spawn(std::string name,
                                       std::function<void()> fn,
                                       size_t stack_size) {
  if (stack_size == 0) stack_size = default_stack_size_;
  auto task = std::shared_ptr<Task>(
      new Task(this, std::move(name), std::move(fn), stack_size));
  {
    std::lock_guard<std::mutex> lk(mu_);
    tasks_.push_back(task);
    ready_.push_back(task.get());
  }
  WakeUp();
  return task;
}

void Scheduler::MakeReady(Task* task) {
  if (task == nullptr) return;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (std::find(ready_.begin(), ready_.end(), task) != ready_.end()) return;
    ready_.push_back(task);
  }
  WakeUp();
}

Task* Scheduler::TaskOf(Coroutine* c) {
  if (c == nullptr) return nullptr;
  std::lock_guard<std::mutex> lk(mu_);
  for (auto& t : tasks_) {
    if (t->coroutine() == c) return t.get();
  }
  return nullptr;
}

void Scheduler::WakeUp() {
  run_seq_.fetch_add(1, std::memory_order_release);
  FutexWake(&run_seq_, INT_MAX);
}

// 当前协程挂起等待 w。
//
// 次序：
//   1. 先看 pending_：已有未消费通知则直接成功，不睡
//   2. 登记等待者（登记后到真正 Yield 之前，Notify 仍可能发生）
//   3. Yield 让出 CPU
//   4. 醒来：清理登记；若 pending>0 消费一个并返回 true
bool Scheduler::ParkCurrent(Waitable* w, uint64_t timeout_ns) {
  if (t_current_coroutine == nullptr) {
    PANGOFLY_ERROR << "[sched] ParkCurrent() called outside coroutine"
                   << std::endl;
    return false;
  }
  if (w == nullptr) return false;

  if (w->pending() > 0) {
    w->pending_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
  }

  Task* self = TaskOf(t_current_coroutine);
  if (self == nullptr) return false;

  {
    std::lock_guard<std::mutex> lk(w->mu_);
    Waitable::Waiter waiter;
    waiter.task = self;
    waiter.woken = false;
    w->waiters_.push_back(waiter);
  }
  if (timeout_ns > 0) {
    std::lock_guard<std::mutex> lk(mu_);
    TimeoutEntry e;
    e.waitable = w;
    e.deadline_ns = NowNs() + timeout_ns;
    timeouts_[t_current_coroutine] = e;
  }

  Coroutine* self_coro = t_current_coroutine;
  self_coro->Yield();

  // 醒来：撤销登记（Notify 可能已经把它标记为 woken 并入了队，重复入队由
  // MakeReady 去重，因此这里无条件移除是安全的）
  {
    std::lock_guard<std::mutex> lk(w->mu_);
    for (auto it = w->waiters_.begin(); it != w->waiters_.end(); ++it) {
      if (it->task == self) {
        w->waiters_.erase(it);
        break;
      }
    }
  }
  {
    std::lock_guard<std::mutex> lk(mu_);
    timeouts_.erase(self_coro);
  }

  if (w->pending() > 0) {
    w->pending_.fetch_sub(1, std::memory_order_acq_rel);
    return true;
  }
  return false;
}

// 把到期的定时等待者放回就绪队列；返回最近的到期时刻，无则返回 0。
uint64_t Scheduler::CheckTimeouts() {
  const uint64_t now = NowNs();
  std::vector<Task*> expired;
  uint64_t next = 0;
  {
    std::lock_guard<std::mutex> lk(mu_);
    for (auto it = timeouts_.begin(); it != timeouts_.end();) {
      if (it->second.deadline_ns <= now) {
        // 直接线性查表，不能调用 TaskOf（它会再次加 mu_）
        for (auto& t : tasks_) {
          if (t->coroutine() == it->first) {
            expired.push_back(t.get());
            break;
          }
        }
        it = timeouts_.erase(it);
      } else {
        if (next == 0 || it->second.deadline_ns < next) {
          next = it->second.deadline_ns;
        }
        ++it;
      }
    }
  }
  for (Task* t : expired) MakeReady(t);
  return next;
}

size_t Scheduler::ReadyCount() const {
  std::lock_guard<std::mutex> lk(mu_);
  return ready_.size();
}

size_t Scheduler::RunOnce() {
  std::deque<Task*> batch;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (ready_.empty()) return 0;
    batch.swap(ready_);
  }

  size_t ran = 0;
  Scheduler* prev = t_current_sched;
  t_current_sched = this;

  for (Task* t : batch) {
    if (t == nullptr || t->done()) continue;
    if (stopped()) break;
    t->coroutine()->Resume();
    ++ran;
  }

  t_current_sched = prev;
  return ran;
}

// 睡到「有就绪任务」或「最近一个超时点」。返回 false 表示所有任务已结束
// 或调度器被 Stop，调用方可以退出。
bool Scheduler::WaitForWork() {
  for (;;) {
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (!ready_.empty()) return true;
    }
    if (stopped()) return false;

    bool all_done = true;
    {
      std::lock_guard<std::mutex> lk(mu_);
      for (const auto& t : tasks_) {
        if (!t->done()) {
          all_done = false;
          break;
        }
      }
    }
    if (all_done) return false;

    const uint64_t next_timeout = CheckTimeouts();
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (!ready_.empty()) return true;
    }

    // 计算 futex 超时：有定时等待就睡到最近到期点，否则最多 200ms 一轮，
    // 便于外部（Signals / 配置变化）有机会触发退出。
    uint64_t wait_ns = 200ULL * 1000 * 1000;
    if (next_timeout != 0) {
      const uint64_t now = NowNs();
      wait_ns = next_timeout > now ? (next_timeout - now) : 1;
      if (wait_ns > 200ULL * 1000 * 1000) wait_ns = 200ULL * 1000 * 1000;
    }

    struct timespec ts;
    ts.tv_sec = static_cast<time_t>(wait_ns / 1000000000ULL);
    ts.tv_nsec = static_cast<long>(wait_ns % 1000000000ULL);

    const uint32_t observed = run_seq_.load(std::memory_order_acquire);
    {
      std::lock_guard<std::mutex> lk(mu_);
      if (!ready_.empty()) return true;
    }
    FutexWait(&run_seq_, observed, &ts);
  }
}

void Scheduler::Run() {
  Scheduler* prev = t_current_sched;
  t_current_sched = this;

  while (!stopped()) {
    RunOnce();
    if (!WaitForWork()) break;
  }

  t_current_sched = prev;
}

void Scheduler::Stop() {
  stop_.store(true, std::memory_order_release);
  WakeUp();
}

}  // namespace coro
}  // namespace pangofly
