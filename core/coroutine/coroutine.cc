#include "core/coroutine/coroutine.h"

#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "core/common/log.h"

namespace pangofly {
namespace coro {

thread_local Coroutine* t_current_coroutine = nullptr;

// 每个线程一份调度器上下文：Resume 时保存到这里，Yield 时从这里恢复。
static thread_local Context t_scheduler_ctx;

Context*& Coroutine::SchedulerContext() {
  static thread_local Context* p = &t_scheduler_ctx;
  return p;
}

// ---------------------------------------------------------------------------
// 栈分配：mmap + guard page
//
// 栈向下生长，所以在低地址端留一页 PROT_NONE。协程栈溢出会立刻 SIGSEGV
// 落在 guard page 上，而不是踩坏堆上其他对象（那种 bug 极难定位）。
// ---------------------------------------------------------------------------
void Coroutine::PrepareStack() {
  const size_t page = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
  stack_size_ = (stack_size_ + page - 1) / page * page;

  void* mem = ::mmap(nullptr, stack_size_ + page, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (mem == MAP_FAILED) {
    PANGOFLY_ERROR << "[coro] mmap stack failed: size=" << stack_size_
                   << " errno=" << errno << std::endl;
    std::abort();
  }
  // 低地址一页设为不可访问
  ::mprotect(mem, page, PROT_NONE);

  stack_ = mem;
  stack_size_ += page;  // 记录映射总长度，析构时用于 munmap

  // 栈顶：高地址端，16 字节对齐（AAPCS / x86-64 都要求）
  uintptr_t top =
      reinterpret_cast<uintptr_t>(static_cast<char*>(mem) + stack_size_);
  top &= ~static_cast<uintptr_t>(15);

#if defined(__x86_64__)
  // trampoline 通过 ret 进入：栈顶需先放返回地址
  top -= sizeof(void*);
  *reinterpret_cast<uint64_t*>(top) =
      reinterpret_cast<uint64_t>(&PfContextTrampoline);
  ctx_.rsp = reinterpret_cast<void*>(top);
  ctx_.r12 = this;  // trampoline: movq %r12,%rdi
  ctx_.mxcsr = 0x1F80;
  ctx_.fpucw = 0x037F;
#elif defined(__arm__)
  ctx_.sp = static_cast<uint32_t>(top);
  ctx_.lr = reinterpret_cast<uint32_t>(&PfContextTrampoline);
  ctx_.r4_r11[0] = reinterpret_cast<uint32_t>(this);  // trampoline: mov r0,r4
#elif defined(__aarch64__)
  ctx_.sp = top;
  ctx_.x30 = reinterpret_cast<uint64_t>(&PfContextTrampoline);
  ctx_.x19_x28[0] = reinterpret_cast<uint64_t>(this);  // trampoline: mov x0,x19
#endif
}

Coroutine::Coroutine(std::function<void()> fn, std::string name,
                     size_t stack_size)
    : fn_(std::move(fn)), name_(std::move(name)), stack_size_(stack_size) {
  if (stack_size_ < 8 * 1024) stack_size_ = kDefaultStackSize;
  PrepareStack();
}

Coroutine::~Coroutine() {
  if (stack_ != nullptr) {
    ::munmap(stack_, stack_size_);
    stack_ = nullptr;
  }
}

bool Coroutine::Resume() {
  if (state_ == State::kDone) return false;

  Coroutine* prev = t_current_coroutine;
  t_current_coroutine = this;
  state_ = State::kRunning;

  PfContextSwitch(SchedulerContext(), &ctx_);

  t_current_coroutine = prev;
  if (state_ != State::kDone) state_ = State::kReady;
  return state_ != State::kDone;
}

void Coroutine::Yield() {
  if (t_current_coroutine == nullptr) {
    PANGOFLY_ERROR << "[coro] Yield() called outside coroutine" << std::endl;
    return;
  }
  PfContextSwitch(&t_current_coroutine->ctx_, SchedulerContext());
}

// 协程函数体执行完毕后的收尾：标记 DONE 并切回调度器，永不返回。
void Coroutine::Finish() {
  state_ = State::kDone;
  PfContextSwitch(&ctx_, SchedulerContext());
  std::abort();  // 不会执行到这里
}

void Yield() {
  if (t_current_coroutine != nullptr) t_current_coroutine->Yield();
}

}  // namespace coro
}  // namespace pangofly

// 汇编 trampoline 的落点。C 链接，参数是被预置进寄存器的 this。
extern "C" void PfCoroutineEntry(pangofly::coro::Coroutine* self) {
  try {
    self->RunBody();
  } catch (const std::exception& e) {
    PANGOFLY_ERROR << "[coro] " << self->name() << " threw: " << e.what()
                   << std::endl;
  } catch (...) {
    PANGOFLY_ERROR << "[coro] " << self->name() << " threw unknown exception"
                   << std::endl;
  }
  self->Finish();
}
