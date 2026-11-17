#ifndef _LIBC_SYS_IOCTL_H
#define _LIBC_SYS_IOCTL_H
#include <posix_abi.h>

int ioctl(int fd, unsigned long request, ...);

#endif
