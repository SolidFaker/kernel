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

static int brk(unsigned int addr)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_brk), "b" (addr));
    return a;
}

static int sleep(unsigned int sec)
{
    unsigned int req[2];
    req[0] = sec;
    req[1] = 0;
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_nanosleep), "b" (req) : "memory");
    return a;
}

static int time_now(void)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_time));
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

static void put_char(char c)
{
    write(1, &c, 1);
}

static void print_dec(int v)
{
    char b[12];
    int i = 0;
    if (v == 0) {
        put_char('0');
        return;
    }
    while (v > 0) {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0) {
        put_char(b[--i]);
    }
}

void _start(void)
{
    print("hello from user elf\n");

    // grow the user heap by one page and prove it is writable
    unsigned int heap = (unsigned int)brk(0);
    if (brk(heap + 4096) < 0) {
        print("brk failed\n");
        exit(1);
    }
    char *mem = (char *)heap;
    int i;
    for (i = 0; i < 16; i++) {
        mem[i] = (char)('A' + i);
    }
    mem[16] = 0;
    print("brk ok: ");
    print(mem);
    put_char('\n');

    int pid = fork();
    if (pid == 0) {
        print("user elf child sleeping 1s\n");
        sleep(1);
        print("user elf child awake\n");
        exit(0);
    }
    int st = 0;
    waitpid(pid, &st);
    print("user elf parent, uptime ");
    print_dec(time_now());
    put_char('s');
    put_char('\n');
    exit(7);
}
