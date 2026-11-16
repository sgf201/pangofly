#include "core/concurrency/thread_pool.h"

#include <algorithm>

namespace pangofly {
namespace pool {

ThreadPool::ThreadPool(size_t workers) {
  if (workers == 0) {
    const unsigned hw = std::thread::hardware_concurrency();
    workers = (hw == 0) ? 2 : static_cast<size_t>(hw);
    // 开发板通常是单核；2 个线程足够让「一个阻塞」不影响「另一个推进」，
    // 再多也不会更快，反而多占内存。
    workers = std::min<size_t>(std::max<size_t>(workers, 2), 4);
  }
  worker_count_ = workers;
  workers_.reserve(workers);
  for (size_t i = 0; i < workers; ++i) {
    workers_.emplace_back([this]() { WorkerLoop(); });
  }
}

ThreadPool::~ThreadPool() {
  Stop();
  Join();
}

void ThreadPool::Submit(std::function<void()> fn) {
  if (!fn) return;
  {
    std::lock_guard<std::mutex> lk(mu_);
    if (stopping_) return;
    tasks_.push_back(std::move(fn));
  }
  cv_.notify_one();
}

void ThreadPool::Stop() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    stopping_ = true;
  }
  cv_.notify_all();
}

void ThreadPool::Join() {
  for (auto& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
}

size_t ThreadPool::pending() const {
  std::lock_guard<std::mutex> lk(mu_);
  return tasks_.size();
}

void ThreadPool::WorkerLoop() {
  for (;;) {
    std::function<void()> task;
    {
      std::unique_lock<std::mutex> lk(mu_);
      cv_.wait(lk, [this]() { return stopping_ || !tasks_.empty(); });
      // 优雅停止：先把手里的任务做完再退，避免半成品状态。
      if (tasks_.empty()) return;
      task = std::move(tasks_.front());
      tasks_.pop_front();
    }
    task();
  }
}

}  // namespace pool
}  // namespace pangofly
