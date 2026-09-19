// user/shell.c -- a small user-mode shell, loaded by exec("shell.elf").
// Everything goes through int 0x80: print/fork/exec/exit/read/ls/cat/chtty.

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

static int exec(const char *path)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (3), "b" (path) : "memory");
    return a;
}

static void exit(void)
{
    __asm__ volatile("int $0x80" : : "a" (4) : "memory");
}

static int read_key(char *c)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (5), "b" (c) : "memory");
    return a;
}

static int list_dir(char *buf, u32 max)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (6), "b" (buf), "c" (max)
                     : "memory");
    return a;
}

static int read_file(const char *name, char *buf, u32 size)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (7),
                     "b" (name), "c" (buf), "d" (size) : "memory");
    return a;
}

static int chtty(int n)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (8), "b" (n) : "memory");
    return a;
}

static int ps(char *buf, u32 max)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a) : "0" (9), "b" (buf), "c" (max)
                     : "memory");
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

static char line[128];
static char iobuf[1024];

static void echo_char(char c)
{
    char s[2];
    s[0] = c;
    s[1] = 0;
    print_str(s);
}

// read one line with basic editing (backspace), echoing as we go
static void read_line(void)
{
    int len = 0;
    for (;;) {
        char c;
        read_key(&c);
        if (c == '\n') {
            line[len] = 0;
            echo_char('\n');
            return;
        }
        if (c == '\b') {
            if (len > 0) {
                len--;
                print_str("\b \b");
            }
            continue;
        }
        if (c >= ' ' && len < (int)sizeof(line) - 1) {
            line[len++] = c;
            echo_char(c);
        }
    }
}

static void run_cmd(const char *cmd, const char *arg)
{
    if (streq(cmd, "help")) {
        print_str("commands:\n"
                  "  help          this text\n"
                  "  ps            list tasks\n"
                  "  ls            list files\n"
                  "  cat <file>    print a file\n"
                  "  run <file>    fork + exec a user ELF\n"
                  "  chtty <0-3>   move the shell to tty n\n"
                  "  exit          quit the shell\n");
        return;
    }
    if (streq(cmd, "ps")) {
        print_str("pid state tty prio\n");
        int n = ps(iobuf, sizeof(iobuf) - 1);
        if (n < 0) {
            print_str("ps failed\n");
        } else {
            iobuf[n] = 0;
            print_str(iobuf);
        }
        return;
    }
    if (streq(cmd, "ls")) {
        int n = list_dir(iobuf, sizeof(iobuf) - 1);
        if (n < 0) {
            print_str("ls failed\n");
        } else {
            iobuf[n] = 0;
            print_str(iobuf);
        }
        return;
    }
    if (streq(cmd, "cat")) {
        if (!*arg) {
            print_str("usage: cat <file>\n");
            return;
        }
        int n = read_file(arg, iobuf, sizeof(iobuf) - 1);
        if (n < 0) {
            print_str("no such file: ");
            print_str(arg);
            echo_char('\n');
            return;
        }
        iobuf[n] = 0;
        print_str(iobuf);
        if (n > 0 && iobuf[n - 1] != '\n') {
            echo_char('\n');
        }
        return;
    }
    if (streq(cmd, "chtty")) {
        if (arg[0] < '0' || arg[0] > '3' || arg[1]) {
            print_str("usage: chtty <0-3>\n");
            return;
        }
        if (chtty(arg[0] - '0') < 0) {
            print_str("chtty failed\n");
        }
        return;
    }
    if (streq(cmd, "run")) {
        if (!*arg) {
            print_str("usage: run <file>\n");
            return;
        }
        int pid = fork();
        if (pid == 0) {
            if (exec(arg) < 0) {
                print_str("exec failed: ");
                print_str(arg);
                echo_char('\n');
            }
            exit();
        }
        print_str("started\n");
        return;
    }
    if (streq(cmd, "exit")) {
        print_str("bye\n");
        exit();
    }
    print_str("unknown command: ");
    print_str(cmd);
    print_str(" (try help)\n");
}

void _start(void)
{
    print_str("a-mini-kernel shell -- type help\n");

#ifdef AUTO_DEMO
    /* headless self-test (bochs E9 console): exercise every command */
    run_cmd("help", "");
    run_cmd("ps", "");
    run_cmd("ls", "");
    run_cmd("cat", "README");
    run_cmd("run", "hello.elf");
#endif

    for (;;) {
        print_str("> ");
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
