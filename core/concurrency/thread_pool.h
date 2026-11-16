#ifndef PANGOFLY_CONCURRENCY_THREAD_POOL_H_
#define PANGOFLY_CONCURRENCY_THREAD_POOL_H_

// ============================================================================
// ThreadPool —— 固定线程数的任务队列
//
// 用途：把「有新消息了，去处理」这类工作交给工作线程，避免业务主线程自己
// 写 while + Wait 的拉取循环。
//
// 语义：
//   · Submit 可在任意线程调用；Stop 之后调用会被忽略（不抛异常），
//     这样析构期间的竞态不会变成崩溃。
//   · Stop 是「优雅停止」：不再收新任务，但队列里已有的任务仍会被执行，
//     避免半完成的状态落地。
//   · 默认线程数取 hardware_concurrency()，下限 2、上限 4。开发板多为
//     单核，设 2 是为了「一个任务阻塞在 IO 时还有线程能跑别的」，并非
//     指望 CPU 并行。
// ============================================================================

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace pangofly {
namespace pool {

class ThreadPool {
 public:
  // workers == 0 表示按硬件并发数自动决定
  explicit ThreadPool(size_t workers = 0);
  ~ThreadPool();

  ThreadPool(const ThreadPool&) = delete;
  ThreadPool& operator=(const ThreadPool&) = delete;

  // 投递一个任务。已停止时直接丢弃。
  void Submit(std::function<void()> fn);

  // 停止接收新任务并唤醒所有工作线程（队列中已有任务仍会执行）
  void Stop();

  // 等待所有工作线程退出。必须与 Stop 配合使用。
  void Join();

  size_t worker_count() const noexcept { return worker_count_; }
  size_t pending() const;

 private:
  void WorkerLoop();

  mutable std::mutex mu_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> tasks_;
  std::vector<std::thread> workers_;
  bool stopping_ = false;
  size_t worker_count_ = 0;
};

}  // namespace pool
}  // namespace pangofly

#endif  // PANGOFLY_CONCURRENCY_THREAD_POOL_H_
