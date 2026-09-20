// A freestanding user program loaded by exec()/run from the shell.
// It talks to the kernel exclusively through POSIX-style syscalls.

#include "unistd.h"

static int write(int fd, const void *buf, unsigned int len)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_write), "b" (fd), "c" (buf), "d" (len)
                     : "memory");
    return a;
}

static int fork(void)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_fork) : "memory");
    return a;
}

static void exit(int status)
{
    __asm__ volatile("int $0x80" : : "a" (NR_exit), "b" (status) : "memory");
}

static int waitpid(int pid, int *status)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_waitpid), "b" (pid), "c" (status)
                     : "memory");
    return a;
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static void print(const char *s)
{
    write(1, s, slen(s));
}

void _start(void)
{
    print("hello from user elf\n");
    int pid = fork();
    if (pid == 0) {
        print("user elf child\n");
        exit(0);
    }
    int st = 0;
    waitpid(pid, &st);
    print("user elf parent\n");
    exit(7);
}
