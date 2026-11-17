#ifndef _LIBC_FCNTL_H
#define _LIBC_FCNTL_H
#include <unistd.h>
#include <posix_abi.h>

int open(const char *path, int flags, ...);
int fcntl(int fd, int cmd, ...);

#endif
