#include "core/shm/file_lock.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(__QNX__)
#include <sys/file.h>   // flock()
#endif

#include <cerrno>

#include "core/common/log.h"

namespace pangofly {
namespace shm {
namespace {

// flock 在 QNX 上不可用，退化为 fcntl 记录锁（语义等价，都是内核维护）
int ApplyLock(int fd) {
#if defined(__QNX__)
  struct flock fl{};
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;   // 0 = 到文件末尾
  return ::fcntl(fd, F_SETLKW, &fl);
#else
  return ::flock(fd, LOCK_EX);
#endif
}

int ApplyUnlock(int fd) {
#if defined(__QNX__)
  struct flock fl{};
  fl.l_type = F_UNLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0;
  return ::fcntl(fd, F_SETLK, &fl);
#else
  return ::flock(fd, LOCK_UN);
#endif
}

}  // namespace

std::string FileLockPath(const std::string& name) {
  // 优先 /dev/shm：与共享内存同介质，且不受 /tmp 清理策略影响
  if (::access("/dev/shm", W_OK) == 0) {
    return "/dev/shm/" + name;
  }
  return "/tmp/" + name;
}

FileLock::FileLock(const std::string& path) : path_(path) {
  // O_CLOEXEC：避免锁的 fd 被子进程继承后，父进程退出仍被 hold 住
  fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
  if (fd_ < 0) {
    PANGOFLY_ERROR << "[file_lock] open failed: " << path_ << std::endl;
  }
}

FileLock::~FileLock() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  // 锁文件本身不删除：删除会与正在等待的进程产生竞态
  // （等待者持有的是已 unlink 的 inode，新来者会创建新文件，两边不再互斥）
}

bool FileLock::Lock() {
  if (fd_ < 0) return false;
  for (;;) {
    if (ApplyLock(fd_) == 0) return true;
    if (errno == EINTR) continue;   // 被信号打断，重试
    PANGOFLY_ERROR << "[file_lock] lock failed: " << path_ << " errno=" << errno
                   << std::endl;
    return false;
  }
}

void FileLock::Unlock() {
  if (fd_ < 0) return;
  if (ApplyUnlock(fd_) != 0) {
    PANGOFLY_ERROR << "[file_lock] unlock failed: " << path_ << " errno=" << errno
                   << std::endl;
  }
}

}  // namespace shm
}  // namespace pangofly
