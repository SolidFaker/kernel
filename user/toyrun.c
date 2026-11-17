#include "unistd.h"

static int sc(int nr, int a, int b, int c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a" (r)
                     : "0" (nr), "b" (a), "c" (b), "d" (c)
                     : "memory");
    return r;
}

void _start(void)
{
    char *argv[] = { (char *)"toybox", (char *)"sh", (char *)"-c",
                     (char *)"echo toybox-sh-ok; pwd; ls /", 0 };
    char *envp[] = { (char *)"PATH=/", (char *)"HOME=/", 0 };
    sc(NR_exec, (int)"toybox.elf", (int)argv, (int)envp);
    sc(NR_write, 1, (int)"toybox exec failed\n", 19);
    sc(NR_exit, 1, 0, 0);
    for (;;) {}
}
