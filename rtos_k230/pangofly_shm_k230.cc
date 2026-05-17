#include "pangofly_shm_k230.h"

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

namespace pangofly {
namespace k230 {

int shm_open(const char* name, int oflag, mode_t mode) {
    return ::shm_open(name, oflag, mode);
}

int shm_unlink(const char* name) {
    return ::shm_unlink(name);
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return ::mmap(addr, length, prot, flags, fd, offset);
}

int munmap(void* addr, size_t length) {
    return ::munmap(addr, length);
}

int close(int fd) {
    return ::close(fd);
}

} // namespace k230
} // namespace pangofly