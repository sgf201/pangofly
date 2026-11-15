/* 协程上下文切换汇编（x86-64 / ARM32 / AArch64）
 *
 * 约定：只保存调用约定里的「被调用者保存」寄存器，调用方的其余寄存器由
 * 编译器在调用点自行处理，因此这里是完整的上下文切换。
 *
 * 不使用 ucontext：rv1106 的 uClibc-1.0.31 上 getcontext 直接段错误。
 */

#if defined(__x86_64__)

/* void PfContextSwitch(Context* from /rdi/, Context* to /rsi/)
 * 偏移见 context.h
 */
    .text
    .globl PfContextSwitch
    .type PfContextSwitch, @function
PfContextSwitch:
    movq %rsp,   0(%rdi)
    movq %rbx,   8(%rdi)
    movq %rbp,  16(%rdi)
    movq %r12,  24(%rdi)
    movq %r13,  32(%rdi)
    movq %r14,  40(%rdi)
    movq %r15,  48(%rdi)
    stmxcsr    56(%rdi)
    fnstcw     60(%rdi)

    movq   8(%rsi), %rbx
    movq  16(%rsi), %rbp
    movq  24(%rsi), %r12
    movq  32(%rsi), %r13
    movq  40(%rsi), %r14
    movq  48(%rsi), %r15
    ldmxcsr    56(%rsi)
    fldcw      60(%rsi)
    movq   0(%rsi), %rsp
    ret
    .size PfContextSwitch, .-PfContextSwitch

/* 新协程入口：r12 预置 Coroutine*，转成第一个参数交给 C++ 入口 */
    .globl PfContextTrampoline
    .type PfContextTrampoline, @function
PfContextTrampoline:
    movq %r12, %rdi
    call PfCoroutineEntry
    ud2
    .size PfContextTrampoline, .-PfContextTrampoline

#elif defined(__arm__)

/* void PfContextSwitch(Context* from /r0/, Context* to /r1/)
 *   0    d8-d15   (64)
 *   64   r4-r11   (32)
 *   96   sp
 *   100  lr
 */
    .text
    .arm
    .fpu vfpv3-d16
    .globl PfContextSwitch
    .type PfContextSwitch, %function
PfContextSwitch:
    vstmia r0!, {d8-d15}
    stmia  r0!, {r4-r11}
    mov    r2, sp
    str    r2, [r0], #4
    str    lr, [r0], #4

    vldmia r1!, {d8-d15}
    ldmia  r1!, {r4-r11}
    ldr    r2, [r1], #4
    mov    sp, r2
    ldr    lr, [r1], #4
    bx     lr
    .size PfContextSwitch, .-PfContextSwitch

    .globl PfContextTrampoline
    .type PfContextTrampoline, %function
PfContextTrampoline:
    mov r0, r4
    bl  PfCoroutineEntry
    @ 入口函数返回说明协程已结束，回到调度器
    b   PfContextTrampoline
    .size PfContextTrampoline, .-PfContextTrampoline

#elif defined(__aarch64__)

/* void PfContextSwitch(Context* from /x0/, Context* to /x1/)
 *   0    x19-x28  (80)
 *   80   x29
 *   88   x30
 *   96   sp
 *   104  d8-d15   (64)
 *   168  fpcr
 *   172  fpsr
 */
    .text
    .globl PfContextSwitch
    .type PfContextSwitch, %function
PfContextSwitch:
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]
    mov x2, sp
    str x2, [x0, #96]
    stp d8,  d9,  [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    mrs x2, fpcr
    str x2, [x0, #168]
    mrs x2, fpsr
    str x2, [x0, #172]

    ldp x19, x20, [x1, #0]
    ldp x21, x22, [x1, #16]
    ldp x23, x24, [x1, #32]
    ldp x25, x26, [x1, #48]
    ldp x27, x28, [x1, #64]
    ldp x29, x30, [x1, #80]
    ldr x2, [x1, #96]
    mov sp, x2
    ldp d8,  d9,  [x1, #104]
    ldp d10, d11, [x1, #120]
    ldp d12, d13, [x1, #136]
    ldp d14, d15, [x1, #152]
    ldr x2, [x1, #168]
    msr fpcr, x2
    ldr x2, [x1, #172]
    msr fpsr, x2
    ret
    .size PfContextSwitch, .-PfContextSwitch

    .globl PfContextTrampoline
    .type PfContextTrampoline, %function
PfContextTrampoline:
    mov x0, x19
    bl  PfCoroutineEntry
    b   PfContextTrampoline
    .size PfContextTrampoline, .-PfContextTrampoline

#else
#error "pangofly coroutine: unsupported architecture"
#endif

    .section .note.GNU-stack,"",%progbits
