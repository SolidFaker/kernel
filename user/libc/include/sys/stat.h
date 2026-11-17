#ifndef _LIBC_SYS_STAT_H
#define _LIBC_SYS_STAT_H
#include <posix_abi.h>
#include <unistd.h>

int stat(const char *path, struct stat *st);
int fstat(int fd, struct stat *st);
int mkdir(const char *path, mode_t mode);
int chmod(const char *path, mode_t mode);
int fchmod(int fd, mode_t mode);

#endif
