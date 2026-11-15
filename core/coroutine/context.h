#ifndef PANGOFLY_CORO_CONTEXT_H_
#define PANGOFLY_CORO_CONTEXT_H_

// ============================================================================
// 协程上下文切换
//
// 为什么不使用 ucontext / getcontext：
//   实测 rv1106 的 uClibc-1.0.31（__UCLIBC_HAS_CONTEXT_FUNCS__=1、符号齐全）
//   在调用 getcontext 时直接段错误。ucontext 在这类嵌入式 libc 上不可靠，
//   因此这里自己保存/恢复被调用者保存寄存器，语义与 boost.context 一致。
//
// 切换点只保存「调用约定要求被调用者保存」的寄存器，其余由编译器保证。
// ============================================================================

#include <cstddef>
#include <cstdint>

namespace pangofly {
namespace coro {

#if defined(__x86_64__)

// 布局必须与 context.S 中的偏移严格对应
struct alignas(16) Context {
  void* rsp;      // 0
  void* rbx;      // 8
  void* rbp;      // 16
  void* r12;      // 24
  void* r13;      // 32
  void* r14;      // 40
  void* r15;      // 48
  uint32_t mxcsr; // 56
  uint16_t fpucw; // 60
  uint16_t pad;   // 62
};

#elif defined(__arm__)

// ARM32（AAPCS + VFPv3-d16）：
//   d8-d15 为 VFP 被调用者保存寄存器；r4-r11 为整型被调用者保存寄存器。
struct alignas(8) Context {
  uint64_t d8_d15[8];  // 0   .. 63
  uint32_t r4_r11[8];  // 64  .. 95
  uint32_t sp;         // 96
  uint32_t lr;         // 100
};

#elif defined(__aarch64__)

// AArch64：x19-x28、x29(fp)、x30(lr)、sp 与 d8-d15 均为被调用者保存。
struct alignas(16) Context {
  uint64_t x19_x28[10];  // 0   .. 79
  uint64_t x29;          // 80
  uint64_t x30;          // 88
  uint64_t sp;           // 96
  uint64_t d8_d15[8];    // 104 .. 167
  uint32_t fpcr;         // 168
  uint32_t fpsr;         // 172
};

#else
#error "pangofly coroutine: unsupported architecture"
#endif

extern "C" {

// 保存 from 的寄存器到 from，从 to 恢复并跳转。返回时相当于 to 的那次切换完成。
void PfContextSwitch(Context* from, Context* to);

// 新协程的入口桩。返回地址（lr / 栈顶）指向这里，参数寄存器里预置了
// Coroutine*，由它转交给 C++ 入口函数。
void PfContextTrampoline();

}  // extern "C"

}  // namespace coro
}  // namespace pangofly

#endif  // PANGOFLY_CORO_CONTEXT_H_
