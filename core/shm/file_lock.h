#ifndef PANGOFLY_V2_FILE_LOCK_H_
#define PANGOFLY_V2_FILE_LOCK_H_

// ============================================================================
// FileLock — 基于 flock 的跨进程互斥
//
// 为什么不用共享内存里的原子变量做跨进程锁：
//   原子变量的锁值在持有者进程被 kill -9 后会永久保留，其他进程只能靠
//   「持有者是否存活」的额外推断来接管，逻辑复杂且容易误判。
//   flock 由内核维护，进程退出（包括崩溃、被信号杀死）时锁自动释放，
//   不存在「死锁残留」这一类问题。
//
// 语义与限制：
//   * 排他锁（LOCK_EX），不提供共享锁
//   * flock 绑定在「打开的文件描述」上，同一进程内两个 FileLock 实例
//     （两次 open）之间也会互斥，因此可以安全地在进程内嵌套使用
//   * NFS 上的 flock 语义依实现而定；本库只用于本机共享内存协调
//   * QNX 无 flock 时退化为 fcntl(F_SETLKW)，语义等价
//
// 用法：
//   FileLock lock("/dev/shm/pangofly_v2_registry.lock");
//   if (!lock.valid()) { ... }
//   { FileLockGuard g(lock); ...临界区... }
// ============================================================================

#include <string>

namespace pangofly {
namespace shm {

class FileLock {
 public:
  explicit FileLock(const std::string& path);
  ~FileLock();

  FileLock(const FileLock&) = delete;
  FileLock& operator=(const FileLock&) = delete;

  bool valid() const noexcept { return fd_ >= 0; }
  const std::string& path() const noexcept { return path_; }

  // 阻塞直到拿到排他锁。成功返回 true。
  bool Lock();
  void Unlock();

 private:
  std::string path_;
  int fd_ = -1;
};

// RAII：构造加锁，析构解锁。失败时不加锁，由调用方用 locked() 判断。
class FileLockGuard {
 public:
  explicit FileLockGuard(FileLock& lock) : lock_(&lock) { locked_ = lock_->Lock(); }
  ~FileLockGuard() {
    if (locked_) lock_->Unlock();
  }

  FileLockGuard(const FileLockGuard&) = delete;
  FileLockGuard& operator=(const FileLockGuard&) = delete;

  bool locked() const noexcept { return locked_; }

 private:
  FileLock* lock_;
  bool locked_ = false;
};

// 选择本机可用的锁文件目录：优先 /dev/shm（与共享内存同介质），
// 不可写时退回 /tmp。返回的路径已带前缀，可直接用于 FileLock。
std::string FileLockPath(const std::string& name);

}  // namespace shm
}  // namespace pangofly

#endif  // PANGOFLY_V2_FILE_LOCK_H_
