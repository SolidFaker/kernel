#ifndef _LIBC_POLL_H
#define _LIBC_POLL_H
#include <posix_abi.h>

int poll(struct pollfd *fds, unsigned int nfds, int timeout);

#endif
