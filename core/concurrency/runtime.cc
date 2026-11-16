#include "core/concurrency/runtime.h"

#include <algorithm>

#include "core/common/futex.h"
#include "core/common/log.h"

namespace pangofly {
namespace pool {

Runtime::Runtime(size_t workers) : workers_(workers) {}

Runtime::~Runtime() {
  Stop();
  if (dispatcher_.joinable()) dispatcher_.join();
  if (timer_thread_.joinable()) timer_thread_.join();
}

bool Runtime::HasSubscription() const {
  std::lock_guard<std::mutex> lk(subs_mu_);
  return !subs_.empty();
}

void Runtime::Post(std::function<void()> fn) { workers_.Submit(std::move(fn)); }

void Runtime::SpawnTimer(std::string name, uint64_t period_ns,
                         std::function<void()> fn) {
  if (period_ns == 0) period_ns = 1;

  auto timer = std::make_shared<Timer>();
  timer->name = std::move(name);
  timer->period_ns = period_ns;
  timer->next_ns = NowNs() + period_ns;
  timer->fn = std::move(fn);

  {
    std::lock_guard<std::mutex> lk(timer_mu_);
    timers_.push_back(timer);
  }
  timer_cv_.notify_all();

  // 允许在没有订阅、只有定时器的情况下工作
  if (!timer_thread_.joinable()) {
    timer_thread_ = std::thread([this]() { TimerLoop(); });
  }
}

// 投递线程：阻塞在各个 segment 的 notify_seq 上。
//
// 只做一件事：发现有新消息，就安排一次「排空该 channel」。真正的业务
// 处理工作线程去做，所以这里即使有多个 channel 也不会被慢回调拖住。
void Runtime::DispatchLoop() {
  const uint64_t kWaitNs = 50ULL * 1000 * 1000;   // 50ms，兼顾退出响应

  std::map<std::string, uint32_t> observed;
  while (running_.load(std::memory_order_acquire)) {
    std::vector<std::shared_ptr<Subscription>> snapshot;
    {
      std::lock_guard<std::mutex> lk(subs_mu_);
      snapshot = subs_;
    }

    bool any = false;
    for (auto& s : snapshot) {
      if (!running_.load(std::memory_order_acquire)) break;
      if (s->stopped.load(std::memory_order_acquire)) continue;
      if (!s->seg || !s->seg->valid()) continue;
      any = true;

      uint32_t& seq = observed[s->channel];
      if (s->seg->WaitNotify(&seq, kWaitNs)) {
        s->notify_gen.fetch_add(1, std::memory_order_release);
        ScheduleDrain(s);
      }
    }

    if (!any) {
      std::unique_lock<std::mutex> lk(run_mu_);
      run_cv_.wait_for(lk, std::chrono::milliseconds(20), [this]() {
        return !running_.load(std::memory_order_acquire);
      });
    }
  }
}

// 安排一次排空，但同一 channel 在任一时刻最多只有一个任务在排队/运行。
//
// 这是本版本的关键约束：Reader::since_seq_ 不是线程安全的，且消息顺序
// 必须保持。用一个 CAS 标志做「合并投递」：处理期间又来了多个通知，
// 只会让当前任务多跑一轮（drain 每次取 64 条直到取空），不会堆任务。
void Runtime::ScheduleDrain(const std::shared_ptr<Subscription>& sub) {
  bool expected = false;
  if (!sub->scheduled.compare_exchange_strong(expected, true,
                                              std::memory_order_acq_rel)) {
    return;   // 已有任务在跑，它会把新消息一并取走
  }
  workers_.Submit([this, sub]() {
    bool more = false;
    if (running_.load(std::memory_order_acquire) &&
        !sub->stopped.load(std::memory_order_acquire)) {
      more = sub->drain();
    }
    // 必须在 drain() 之后才清标志：否则排空执行期间进来新通知会再投一个
    // 任务，两个任务并发用同一个 Reader::since_seq_，既乱序也不安全。
    sub->scheduled.store(false, std::memory_order_release);

    // 一轮只取 64 条，若还没取空就自己再排一次，不必等下一次通知
    // （通知可能已经全部在本次排空之前到达，不会再触发）。
    if (more) ScheduleDrain(sub);
  });
}

// 定时器线程：到点把回调投给线程池。
//
// 用「下一轮到点时刻」而不是固定 tick，避免定时器多时累积漂移；
// 周期任务如果执行超过一个周期，会跳过错过的轮次而不是补偿式连发，
// 这符合「统计打印」这类用途的直觉。
void Runtime::TimerLoop() {
  std::unique_lock<std::mutex> lk(timer_mu_);
  while (running_.load(std::memory_order_acquire)) {
    const uint64_t now = NowNs();

    uint64_t next_wake = now + 200ULL * 1000 * 1000;   // 无定时器时 200ms 一轮
    std::vector<std::shared_ptr<Timer>> due;

    for (auto it = timers_.begin(); it != timers_.end();) {
      auto& t = *it;
      if (t->cancelled.load(std::memory_order_acquire)) {
        it = timers_.erase(it);
        continue;
      }
      if (t->next_ns <= now) {
        due.push_back(t);
        // 跳过错过的轮次，避免积压后连发
        do {
          t->next_ns += t->period_ns;
        } while (t->next_ns <= now);
      }
      next_wake = std::min(next_wake, t->next_ns);
      ++it;
    }

    if (!due.empty()) {
      lk.unlock();
      for (auto& t : due) {
        auto fn = t->fn;
        workers_.Submit([fn]() {
          if (fn) fn();
        });
      }
      lk.lock();
      continue;
    }

    const uint64_t now2 = NowNs();
    uint64_t wait_ns = (next_wake > now2) ? (next_wake - now2) : 1;
    if (wait_ns > 200ULL * 1000 * 1000) wait_ns = 200ULL * 1000 * 1000;

    timer_cv_.wait_for(lk, std::chrono::nanoseconds(wait_ns), [this]() {
      return !running_.load(std::memory_order_acquire);
    });
  }
}

void Runtime::Run() {
  {
    std::unique_lock<std::mutex> lk(run_mu_);
    run_cv_.wait(lk, [this]() {
      return !running_.load(std::memory_order_acquire);
    });
  }
}

void Runtime::Stop() {
  if (!running_.exchange(false)) return;

  {
    std::lock_guard<std::mutex> lk(subs_mu_);
    for (auto& s : subs_) s->stopped.store(true, std::memory_order_release);
  }
  {
    std::lock_guard<std::mutex> lk(timer_mu_);
    for (auto& t : timers_) t->cancelled.store(true, std::memory_order_release);
  }

  run_cv_.notify_all();
  timer_cv_.notify_all();
  workers_.Stop();
}

}  // namespace pool
}  // namespace pangofly
