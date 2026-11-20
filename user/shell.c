// user/shell.c -- a small user-mode shell.
// Talks to the kernel through POSIX-style syscalls (int 0x80):
// read/write/open/close/getdents + fork/exec/exit/chtty/ps.

#include "unistd.h"

typedef unsigned int u32;

static int read(int fd, void *buf, u32 len)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_read), "b" (fd), "c" (buf), "d" (len)
                     : "memory");
    return a;
}

static int write(int fd, const void *buf, u32 len)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_write), "b" (fd), "c" (buf), "d" (len)
                     : "memory");
    return a;
}

static int open(const char *path, int flags)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_open), "b" (path), "c" (flags) : "memory");
    return a;
}

static int close(int fd)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_close), "b" (fd));
    return a;
}

static int getdents(int fd, char *buf, u32 max)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_getdents), "b" (fd), "c" (buf), "d" (max)
                     : "memory");
    return a;
}

static int fork(void)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_fork) : "memory");
    return a;
}

static int execve(const char *path, char *const argv[], char *const envp[])
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_exec), "b" (path), "c" (argv), "d" (envp)
                     : "memory");
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
                     : "0" (NR_waitpid), "b" (pid), "c" (status), "d" (0)
                     : "memory");
    return a;
}

static int chtty(int n)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (NR_chtty), "b" (n));
    return a;
}

static int ps(char *buf, u32 max)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_ps), "b" (buf), "c" (max) : "memory");
    return a;
}

static int strlen(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void print(const char *s)
{
    write(1, s, strlen(s));
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
    if (v < 0) {
        put_char('-');
        v = -v;
    }
    while (v > 0) {
        b[i++] = (char)('0' + v % 10);
        v /= 10;
    }
    while (i > 0) {
        put_char(b[--i]);
    }
}

static char line[128];
static char iobuf[1024];

static void echo_char(char c)
{
    write(1, &c, 1);
}

// read one line with basic editing (backspace), echoing as we go
static void read_line(void)
{
    int len = 0;
    for (;;) {
        char c;
        read(0, &c, 1);
        if (c == '\n') {
            line[len] = 0;
            echo_char('\n');
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                print("\b \b");
            }
            continue;
        }
        if (c >= ' ' && len < (int)sizeof(line) - 1) {
            line[len++] = c;
            echo_char(c);
        }
    }
}

// Fork + exec a program in the foreground and show its exit status.
static void run_program(const char *path)
{
    int pid = fork();
    if (pid == 0) {
        char *const argv2[2] = { (char *)path, 0 };
        if (execve(path, argv2, 0) < 0) {
            print("\x1b[31mexec failed: ");
            print(path);
            print("\x1b[0m\n");
        }
        exit(127);
    }

    int st = 0;
    int w = waitpid(pid, &st);
    if (w == pid) {
        print("\x1b[32mstatus ");
        print_dec(st);
        print("\x1b[0m\n");
    } else {
        print("\x1b[31mwaitpid failed\x1b[0m\n");
    }
}

static void run_cmd(const char *cmd, const char *arg)
{
    if (streq(cmd, "help")) {
        print("\x1b[36mcommands:\x1b[0m\n"
              "  help          this text\n"
              "  ps            list tasks\n"
              "  ls            list files\n"
              "  cat <file>    print a file\n"
              "  run <file>    fork + exec a user ELF\n"
              "  gfx           VGA 320x200x256 graphics demo\n"
              "  posix         run POSIX self tests\n"
              "  libc          run the standard C/libc demo\n"
              "  ping          ping 10.0.2.2 through the e1000 NIC\n"
              "  net           UDP echo test to 10.0.2.2:9999\n"
              "  musl          run the musl static hello world\n"
              "  toybox        launch toybox sh\n"
              "  chtty <0-3>   move the shell to tty n\n"
              "  exit          quit the shell\n");
        return;
    }
    if (streq(cmd, "ps")) {
        print("pid state tty prio name\n");
        int n = ps(iobuf, sizeof(iobuf) - 1);
        if (n < 0) {
            print("ps failed\n");
        } else {
            iobuf[n] = 0;
            print(iobuf);
        }
        return;
    }
    if (streq(cmd, "ls")) {
        int fd = open(".", O_RDONLY);
        if (fd < 0) {
            print("ls failed\n");
            return;
        }
        int n = getdents(fd, iobuf, sizeof(iobuf) - 1);
        close(fd);
        if (n < 0) {
            print("ls failed\n");
        } else {
            iobuf[n] = 0;
            print(iobuf);
        }
        return;
    }
    if (streq(cmd, "cat")) {
        if (!*arg) {
            print("usage: cat <file>\n");
            return;
        }
        int fd = open(arg, O_RDONLY);
        if (fd < 0) {
            print("\x1b[31mno such file: ");
            print(arg);
            print("\x1b[0m\n");
            return;
        }
        int n;
        int last = '\n';
        while ((n = read(fd, iobuf, sizeof(iobuf))) > 0) {
            write(1, iobuf, n);
            last = iobuf[n - 1];
        }
        close(fd);
        if (last != '\n') {
            echo_char('\n');
        }
        return;
    }
    if (streq(cmd, "chtty")) {
        if (arg[0] < '0' || arg[0] > '3' || arg[1]) {
            print("usage: chtty <0-3>\n");
            return;
        }
        if (chtty(arg[0] - '0') < 0) {
            print("chtty failed\n");
        }
        return;
    }
    if (streq(cmd, "gfx")) {
        run_program("gfx.elf");
        return;
    }
    if (streq(cmd, "posix")) {
        run_program("postest.elf");
        return;
    }
    if (streq(cmd, "libc")) {
        run_program("libctest.elf");
        return;
    }
    if (streq(cmd, "toybox")) {
        run_program("toyrun.elf");
        return;
    }
    if (streq(cmd, "musl")) {
        run_program("hello_musl.elf");
        return;
    }
    if (streq(cmd, "net")) {
        run_program("nettest.elf");
        return;
    }
    if (streq(cmd, "ping")) {
        int a;
        int r;
        __asm__ volatile("int $0x80" : "=a" (a)
                         : "0" (79), "b" (0x0A000202), "c" (1000)
                         : "memory");
        if (a >= 0) {
            print("ping 10.0.2.2: ");
            print_dec(a);
            print(" ms\n");
        } else {
            print("\x1b[31mping failed\x1b[0m\n");
        }
        (void)r;
        return;
    }
    if (streq(cmd, "run")) {
        if (!*arg) {
            print("usage: run <file>\n");
            return;
        }
        run_program(arg);
        return;
    }
    if (streq(cmd, "exit")) {
        print("bye\n");
        exit(0);
    }
    print("\x1b[31munknown command: ");
    print(cmd);
    print(" (try help)\x1b[0m\n");
}

void _start(void)
{
    print("\x1b[1;36ma-mini-kernel shell\x1b[0m -- type help\n");

#ifdef AUTO_DEMO
    /* headless self-test (E9 console): exercise every command */
    run_cmd("help", "");
    run_cmd("ps", "");
    run_cmd("ls", "");
    run_cmd("cat", "/system/share/doc/README");
    run_cmd("run", "hello.elf");
    run_cmd("ps", "");
#endif

    for (;;) {
        print("\x1b[1;36m>\x1b[0m ");
        read_line();

        /* split "cmd arg" */
        char *arg = line;
        while (*arg && *arg != ' ') {
            arg++;
        }
        if (*arg) {
            *arg++ = 0;
            while (*arg == ' ') {
                arg++;
            }
        }
        if (line[0]) {
            run_cmd(line, arg);
        }
    }
}
