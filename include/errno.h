#ifndef ERRNO_H
#define ERRNO_H

extern int errno;

/* POSIX errno values. Kernel syscalls return the negated value; user
   wrappers should turn a negative return into errno = -ret, ret = -1. */

#define EPERM        1
#define ENOENT       2
#define ESRCH        3
#define EINTR        4
#define EIO          5
#define ENXIO        6
#define E2BIG        7
#define ENOEXEC      8
#define EBADF        9
#define ECHILD      10
#define EAGAIN      11
#define ENOMEM      12
#define EACCES      13
#define EFAULT      14
#define EBUSY       16
#define EEXIST      17
#define EXDEV       18
#define ENODEV      19
#define ENOTDIR     20
#define EISDIR      21
#define EINVAL      22
#define ENFILE      23
#define EMFILE      24
#define ENOTTY      25
#define EFBIG       27
#define ENOSPC      28
#define ESPIPE      29
#define EROFS       30
#define EPIPE       32
#define ERANGE      34
#define ENOSYS      38
#define ENOTEMPTY   39
#define ELOOP       40
#define EOVERFLOW   75
#define ENOTSOCK    88
#define ENETDOWN    100
#define ETIMEDOUT   110
#define EHOSTUNREACH 113
#define EPROTONOSUPPORT 93
#define EOPNOTSUPP  95
#define EAFNOSUPPORT 97
#define EADDRINUSE  98
#define EADDRNOTAVAIL 99
#define ENOTCONN   107
#define ECONNREFUSED 111

#endif
