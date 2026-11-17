#ifndef _LIBC_SIGNAL_H
#define _LIBC_SIGNAL_H
#include <posix_abi.h>

typedef void (*sighandler_t)(int);

sighandler_t signal(int sig, sighandler_t handler);
int kill(int pid, int sig);
int raise(int sig);
int sigaction(int sig, const struct sigaction *act, struct sigaction *old);
int sigprocmask(int how, const sigset_t *set, sigset_t *old);
int sigpending(sigset_t *set);
int pause(void);
unsigned int alarm(unsigned int seconds);

#endif
