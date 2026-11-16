#ifndef PANGOFLY_COMMON_FUTEX_H_
#define PANGOFLY_COMMON_FUTEX_H_

// ============================================================================
// 跨进程 futex 原语
//
// 段头的 notify_seq 位于共享内存，多个进程映射到同一物理页，因此这里使用
// 不带 FUTEX_PRIVATE_FLAG 的系统调用形式：一个进程 Publish 后的 wake 可以
// 唤醒另一个进程里 wait 的线程。
//
// 无 SYS_futex 的平台（部分 uclibc / 老内核头）退化为短睡，语义不变，
// 只是失去「空载零开销」。
// ============================================================================

#include <errno.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>

namespace pangofly {

inline uint64_t NowNs() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL +
         static_cast<uint64_t>(ts.tv_nsec);
}

#if defined(SYS_futex)

inline int FutexWait(std::atomic<uint32_t>* addr, uint32_t expected,
                     const struct timespec* timeout) {
  return static_cast<int>(::syscall(SYS_futex,
                                    reinterpret_cast<uint32_t*>(addr),
                                    FUTEX_WAIT, expected, timeout, nullptr, 0));
}

inline void FutexWake(std::atomic<uint32_t>* addr, int nwaiters) {
  ::syscall(SYS_futex, reinterpret_cast<uint32_t*>(addr), FUTEX_WAKE, nwaiters,
            nullptr, nullptr, 0);
}

#else

inline int FutexWait(std::atomic<uint32_t>* addr, uint32_t expected,
                     const struct timespec* timeout) {
  (void)addr;
  (void)expected;
  struct timespec req = (timeout != nullptr) ? *timeout : timespec{0, 1000000};
  const long ms_max = 5;
  if (req.tv_sec > 0 || req.tv_nsec > ms_max * 1000000L) {
    req.tv_sec = 0;
    req.tv_nsec = ms_max * 1000000L;
  }
  nanosleep(&req, nullptr);
  errno = EAGAIN;
  return -1;
}

inline void FutexWake(std::atomic<uint32_t>*, int) {}

#endif

}  // namespace pangofly

#endif  // PANGOFLY_COMMON_FUTEX_H_
