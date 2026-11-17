#ifndef _LIBC_TERMIOS_H
#define _LIBC_TERMIOS_H
#include <posix_abi.h>
#include <sys/ioctl.h>

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int actions, const struct termios *t);
#define TCSANOW 0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#endif
