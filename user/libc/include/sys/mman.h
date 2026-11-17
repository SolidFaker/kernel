#ifndef _LIBC_SYS_MMAN_H
#define _LIBC_SYS_MMAN_H
#include <posix_abi.h>
#include <stddef.h>

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off);
int munmap(void *addr, size_t len);
int mprotect(void *addr, size_t len, int prot);
int msync(void *addr, size_t len, int flags);
int madvise(void *addr, size_t len, int advice);

#endif
