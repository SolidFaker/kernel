#ifndef _LIBC_SYS_WAIT_H
#define _LIBC_SYS_WAIT_H
#include <posix_abi.h>
#include <unistd.h>

pid_t waitpid(pid_t pid, int *status, int options);

#endif
