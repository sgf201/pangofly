#ifndef PANGOFLY_RTOS_K230_SHM_H_
#define PANGOFLY_RTOS_K230_SHM_H_

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

namespace pangofly {
namespace k230 {

int shm_open(const char* name, int oflag, mode_t mode);
int shm_unlink(const char* name);
void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void* addr, size_t length);
int close(int fd);

} // namespace k230
} // namespace pangofly

#endif // PANGOFLY_RTOS_K230_SHM_H_