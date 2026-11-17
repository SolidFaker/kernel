#ifndef _LIBC_TIME_H
#define _LIBC_TIME_H
#include <posix_abi.h>

typedef unsigned int time_t;

time_t time(time_t *t);
int clock_gettime(int clk, struct timespec *ts);
int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);

#endif
