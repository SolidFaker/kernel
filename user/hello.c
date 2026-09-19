// A freestanding user program loaded by exec()/run from the shell.
// It talks to the kernel exclusively through the int 0x80 interface.

typedef unsigned int u32;

static int print_str(const char *s)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (0), "b" (s) : "memory");
    return a;
}

static int fork(void)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (1) : "memory");
    return a;
}

static void exit(void)
{
    __asm__ volatile("int $0x80" : : "a" (4) : "memory");
}

void _start(void)
{
    print_str("hello from user elf\n");
    int pid = fork();
    if (pid == 0) {
        print_str("user elf child\n");
    } else {
        print_str("user elf parent\n");
    }
    exit();
}
