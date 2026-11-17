#ifndef _LIBC_SYS_TIME_H
#define _LIBC_SYS_TIME_H
#include <posix_abi.h>

int gettimeofday(struct timeval *tv, void *tz);

#endif
