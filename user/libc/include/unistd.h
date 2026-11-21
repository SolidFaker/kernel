#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include_next <unistd.h>

typedef int ssize_t;
typedef int pid_t;
typedef int off_t;
typedef unsigned int mode_t;
typedef unsigned int uid_t;
typedef unsigned int gid_t;
typedef long ssize_t_compat;

extern char **environ;

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
off_t lseek(int fd, off_t off, int whence);
pid_t fork(void);
int execve(const char *path, char *const argv[], char *const envp[]);
void _exit(int status) __attribute__((noreturn));
void exit(int status) __attribute__((noreturn));
pid_t getpid(void);
pid_t getppid(void);
uid_t getuid(void);
uid_t geteuid(void);
gid_t getgid(void);
gid_t getegid(void);
int setuid(uid_t uid);
int setgid(gid_t gid);
int seteuid(uid_t uid);
int setegid(gid_t gid);
int setresuid(uid_t uid, uid_t euid, uid_t suid);
int setresgid(gid_t gid, gid_t egid, gid_t sgid);
int getgroups(int size, gid_t groups[]);
int setgroups(int size, const gid_t groups[]);
int chown(const char *path, uid_t uid, gid_t gid);
int fchown(int fd, uid_t uid, gid_t gid);
int pipe(int fds[2]);
int dup(int fd);
int dup2(int oldfd, int newfd);
int isatty(int fd);
int chdir(const char *path);
char *getcwd(char *buf, size_t size);
int unlink(const char *path);
int rmdir(const char *path);
int access(const char *path, int mode);
int truncate(const char *path, off_t length);
int ftruncate(int fd, off_t length);
long brk(void *addr);
void *sbrk(int incr);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);
unsigned int alarm(unsigned int seconds);
int pause(void);
pid_t getpgrp(void);
int setpgid(pid_t pid, pid_t pgid);
pid_t getpgid(pid_t pid);
pid_t setsid(void);
pid_t tcgetpgrp(int fd);
int tcsetpgrp(int fd, pid_t pgrp);
int getdents(int fd, char *buf, unsigned int max);
long sysconf(int name);
int getpagesize(void);

#endif
