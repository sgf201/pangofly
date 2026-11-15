#include "core/coroutine/runtime.h"

#include <chrono>
#include <cstdlib>

#include "core/common/log.h"

namespace pangofly {
namespace coro {

Runtime::Runtime(size_t default_stack_size) : sched_(default_stack_size) {}

Runtime::~Runtime() {
  Stop();
  if (poller_.joinable()) poller_.join();
}

void Runtime::Stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lk(subs_mu_);
    for (auto& s : subs_) s->stopped.store(true, std::memory_order_release);
  }
  sched_.Stop();
  stop_cv_.notify_all();
}

std::shared_ptr<Task> Runtime::SpawnTimer(std::string name, uint64_t period_ns,
                                          std::function<void()> fn) {
  auto w = std::make_shared<Waitable>(&sched_);
  return sched_.Spawn(std::move(name), [this, period_ns, fn, w]() {
    while (running_.load(std::memory_order_acquire)) {
      // 定时任务不需要别的事件源主动 Notify，靠 Wait 超时驱动下一轮
      w->Wait(period_ns);
      if (!running_.load(std::memory_order_acquire)) break;
      fn();
    }
  });
}

std::shared_ptr<Task> Runtime::Spawn(std::string name, std::function<void()> fn) {
  return sched_.Spawn(std::move(name), std::move(fn));
}

// 通知线程：为每个订阅等 segment 的 notify_seq 变化，然后唤醒订阅协程。
//
// 这里是唯一做 futex 等待的地方，业务协程完全不碰系统调用。
// 超时设得比较短（50ms）以便及时响应 Stop，同时空载时也不占 CPU。
void Runtime::PollLoop() {
  const uint64_t kWaitNs = 50ULL * 1000 * 1000;
  const bool debug = ::getenv("PF_CORO_DEBUG") != nullptr;

  std::map<std::string, uint32_t> observed;
  uint64_t notify_count = 0;
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
        ++notify_count;
        if (debug && (notify_count % 20) == 0) {
          PANGOFLY_INFO << "[poller] notify#" << notify_count
                        << " chan=" << s->channel << " seq=" << seq
                        << " pending=" << s->signal->pending() << std::endl;
        }
        s->signal->Notify();
      }
    }

    if (!any) {
      // 没有订阅时轻微让出 CPU，避免忙等
      std::unique_lock<std::mutex> lk(stop_mu_);
      stop_cv_.wait_for(lk, std::chrono::milliseconds(20), [this]() {
        return !running_.load(std::memory_order_acquire);
      });
    }
  }
}

}  // namespace coro
}  // namespace pangofly
