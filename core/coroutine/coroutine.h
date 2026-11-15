#ifndef PANGOFLY_CORO_COROUTINE_H_
#define PANGOFLY_CORO_COROUTINE_H_

// ============================================================================
// Coroutine — 有栈协程
//
// 用法（一般不直接用，走 Runtime / Task）：
//   Coro c(fn);
//   c.Resume();      // 跑到下一次 Yield 或结束
//   c.Yield();       // 只能在协程内部调用
//
// 状态机：
//   READY ──Resume──> RUNNING ──Yield──> READY
//                        │
//                        └──函数返回──> DONE
//
// 栈由本类持有：默认 64KB，可在构造时指定。栈内存用 mmap 映射并加
// guard page，越界时立刻 SIGSEGV 而不是悄悄踩坏别的协程。
// ============================================================================

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "core/coroutine/context.h"

namespace pangofly {
namespace coro {

class Coroutine {
 public:
  enum class State { kReady, kRunning, kDone };

  // 默认栈大小：256KB。mmap + MAP_NORESERVE，未触碰的页不占物理内存。
  //
  // 这个值要能容下业务回调里的深层调用（OpenCV / RKNN 内部会有较大的
  // 临时对象）。注意回调里的「大数组」仍应放堆上：栈越界会直接撞
  // guard page 崩溃，而不会给出可读的错误。
  static constexpr size_t kDefaultStackSize = 256 * 1024;

  explicit Coroutine(std::function<void()> fn,
                     std::string name = {},
                     size_t stack_size = kDefaultStackSize);
  ~Coroutine();

  Coroutine(const Coroutine&) = delete;
  Coroutine& operator=(const Coroutine&) = delete;

  // 从调度器上下文切进来，跑到 Yield 或结束。
  // 返回 false 表示协程已结束（此后不可再 Resume）。
  bool Resume();

  // 只能在协程内部调用：切回调度器。
  void Yield();

  // 协程函数体的执行入口（由 trampoline 调用），以及结束时的收尾。
  void RunBody() { if (fn_) fn_(); }
  void Finish();

  State state() const noexcept { return state_; }
  bool done() const noexcept { return state_ == State::kDone; }
  const std::string& name() const noexcept { return name_; }
  void set_name(std::string n) { name_ = std::move(n); }

  // 调度器上下文（每个线程一份）。Resume/Yield 用它做切换目标。
  static coro::Context*& SchedulerContext();

  // 恢复调度器。协程函数返回时由 trampoline 调用。
  static void ResumeScheduler();

 private:
  void PrepareStack();

  std::function<void()> fn_;
  std::string name_;
  Context ctx_{};
  State state_ = State::kReady;
  void* stack_ = nullptr;
  size_t stack_size_ = 0;
};

// 汇编 trampoline 跳转到的 C++ 入口（C 链接，避免名字修饰）
extern "C" void PfCoroutineEntry(Coroutine* self);

// 当前线程正在运行的协程；调度器上下文里为 nullptr
extern thread_local Coroutine* t_current_coroutine;

// 便捷函数：当前是否在协程内
inline bool InCoroutine() { return t_current_coroutine != nullptr; }

// 主动让出 CPU。只能在协程内调用（在调度器线程里调用会 abort，因为
// 那会失去恢复点）。
void Yield();

}  // namespace coro
}  // namespace pangofly

#endif  // PANGOFLY_CORO_COROUTINE_H_
