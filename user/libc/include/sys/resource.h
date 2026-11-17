#ifndef _LIBC_SYS_RESOURCE_H
#define _LIBC_SYS_RESOURCE_H
struct rlimit { unsigned int rlim_cur; unsigned int rlim_max; };
#define RLIMIT_CPU 0
#define RLIMIT_FSIZE 1
#define RLIMIT_DATA 2
#define RLIMIT_STACK 3
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9
int getrlimit(int res, struct rlimit *rl);
int setrlimit(int res, const struct rlimit *rl);
#endif
