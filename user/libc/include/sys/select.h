#ifndef _LIBC_SYS_SELECT_H
#define _LIBC_SYS_SELECT_H
#include <posix_abi.h>

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout);

#endif
