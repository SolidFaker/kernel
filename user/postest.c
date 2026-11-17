// user/postest.c -- POSIX phase-1 self test.
// Exercises: getcwd/chdir, stat/access, uname, clock_gettime,
// gettimeofday, getppid, isatty, pipe, dup/dup2, fork/exec/waitpid.

#include "unistd.h"
#include "posix_abi.h"

__asm__(
".globl sig_restorer\n"
"sig_restorer:\n"
"  movl $51, %eax\n"      /* NR_sigreturn */
"  int $0x80\n"
);
extern void sig_restorer(void);

static volatile int got_sig;

static int fails;

static int sys_call(int nr, int a, int b, int c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a" (r)
                     : "0" (nr), "b" (a), "c" (b), "d" (c)
                     : "memory");
    return r;
}

static int write(int fd, const void *buf, unsigned int len)
{
    return sys_call(NR_write, fd, (int)buf, (int)len);
}

static int read(int fd, void *buf, unsigned int len)
{
    return sys_call(NR_read, fd, (int)buf, (int)len);
}

static int open(const char *path, int flags)
{
    return sys_call(NR_open, (int)path, flags, 0);
}

static int close(int fd)
{
    return sys_call(NR_close, fd, 0, 0);
}

static int lseek(int fd, int off, int whence)
{
    return sys_call(NR_lseek, fd, off, whence);
}

static int pipe(int *fds)
{
    return sys_call(NR_pipe, (int)fds, 0, 0);
}

static int dup(int fd)
{
    return sys_call(NR_dup, fd, 0, 0);
}

static int dup2(int oldfd, int newfd)
{
    return sys_call(NR_dup2, oldfd, newfd, 0);
}

static int fork(void)
{
    return sys_call(NR_fork, 0, 0, 0);
}

static int waitpid(int pid, int *st, int options)
{
    return sys_call(NR_waitpid, pid, (int)st, options);
}

static int unlink(const char *path)
{
    return sys_call(NR_unlink, (int)path, 0, 0);
}

static int mkdir(const char *path, int mode)
{
    return sys_call(NR_mkdir, (int)path, mode, 0);
}

static int rmdir(const char *path)
{
    return sys_call(NR_rmdir, (int)path, 0, 0);
}

static int rename(const char *oldp, const char *newp)
{
    return sys_call(NR_rename, (int)oldp, (int)newp, 0);
}

static int ftruncate(int fd, unsigned int len)
{
    return sys_call(NR_ftruncate, fd, (int)len, 0);
}

static void *mmap(void *addr, unsigned int len, int prot, int flags,
                  int fd, unsigned int off)
{
    struct mmap_args ma;
    ma.addr = (unsigned int)addr;
    ma.len = len;
    ma.prot = (unsigned int)prot;
    ma.flags = (unsigned int)flags;
    ma.fd = fd;
    ma.offset = off;
    return (void *)sys_call(NR_mmap, (int)&ma, 0, 0);
}

static int munmap(void *addr, unsigned int len)
{
    return sys_call(NR_munmap, (int)addr, (int)len, 0);
}

static int mprotect(void *addr, unsigned int len, int prot)
{
    return sys_call(NR_mprotect, (int)addr, (int)len, prot);
}

static int ioctl(int fd, unsigned int req, void *arg)
{
    return sys_call(NR_ioctl, fd, (int)req, (int)arg);
}

static void exit(int st)
{
    sys_call(NR_exit, st, 0, 0);
    for (;;) {
    }
}

static int slen(const char *s)
{
    int n = 0;
    while (s[n]) {
        n++;
    }
    return n;
}

static int seq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void print(const char *s)
{
    write(1, s, (unsigned int)slen(s));
}

static void pass(const char *name, int ok)
{
    if (ok) {
        print("\x1b[32mPASS\x1b[0m ");
    } else {
        print("\x1b[31mFAIL\x1b[0m ");
        fails++;
    }
    print(name);
    print("\n");
}

static void getcwd_test(void)
{
    char buf[128];
    int r = sys_call(NR_getcwd, (int)buf, sizeof(buf), 0);
    pass("getcwd root", r == 0 && buf[0] == '/');

    r = sys_call(NR_chdir, (int)"/dev", 0, 0);
    sys_call(NR_getcwd, (int)buf, sizeof(buf), 0);
    pass("chdir /dev", r == 0 && seq(buf, "/dev"));

    sys_call(NR_chdir, (int)"/", 0, 0);
    sys_call(NR_getcwd, (int)buf, sizeof(buf), 0);
    pass("chdir /", seq(buf, "/"));
}

static void stat_test(void)
{
    struct stat st;
    int r = sys_call(NR_stat, (int)"README", (int)&st, 0);
    pass("stat README", r == 0 && S_ISREG(st.st_mode) && st.st_size > 0);

    pass("access R_OK", sys_call(NR_access, (int)"README", R_OK, 0) == 0);
    pass("access W_OK", sys_call(NR_access, (int)"README", W_OK, 0) == 0);
    pass("access ENOENT",
         sys_call(NR_access, (int)"no-such-file", F_OK, 0) < 0);
}

static void misc_test(void)
{
    struct utsname u;
    struct timespec ts;
    struct timeval tv;

    pass("uname", sys_call(NR_uname, (int)&u, 0, 0) == 0 &&
         seq(u.sysname, "a-mini-kernel"));

    pass("clock_gettime", sys_call(NR_clock_gettime, CLOCK_MONOTONIC,
         (int)&ts, 0) == 0 && ts.tv_nsec < 1000000000u);

    pass("gettimeofday", sys_call(NR_gettimeofday, (int)&tv, 0, 0) == 0 &&
         tv.tv_usec < 1000000u);

    pass("getppid", sys_call(NR_getppid, 0, 0, 0) > 0);
    pass("isatty(0)", sys_call(NR_isatty, 0, 0, 0) == 1);
}

static void fcntl_test(void)
{
    int fd = open("README", O_RDONLY);
    int dfd, off;
    char tmp[8];

    pass("open README", fd >= 0);
    dfd = dup(fd);
    pass("dup", dfd >= 0);
    pass("dup fd flags", sys_call(NR_fcntl, dfd, F_GETFD, 0) == 0);
    (void)read(fd, tmp, 4);
    off = lseek(dfd, 0, SEEK_CUR);
    pass("dup shares offset", off == 4);
    close(fd);
    close(dfd);
}

static void pipe_test(void)
{
    int fds[2];
    char buf[64];
    int pid, st = 0, n;

    pass("pipe", pipe(fds) == 0);
    pass("isatty(pipe)", sys_call(NR_isatty, fds[0], 0, 0) == 0);

    pid = fork();
    if (pid == 0) {
        close(fds[0]);
        dup2(fds[1], 1);
        close(fds[1]);
        write(1, "pipe data\n", 10);
        exit(0);
    }

    close(fds[1]);
    n = read(fds[0], buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = 0;
    }
    close(fds[0]);
    waitpid(pid, &st, 0);

    pass("pipe read", n == 10 && seq(buf, "pipe data\n"));
    pass("pipe child exit", st == 0);
}

static int kill(int pid, int sig)
{
    return sys_call(NR_kill, pid, sig, 0);
}

static int raise(int sig)
{
    return sys_call(NR_raise, sig, 0, 0);
}

static int sigaction(int sig, struct sigaction *act, struct sigaction *old)
{
    return sys_call(NR_sigaction, sig, (int)act, (int)old);
}

static int sigprocmask(int how, sigset_t *set, sigset_t *old)
{
    return sys_call(NR_sigprocmask, how, (int)set, (int)old);
}

static int sigpending(sigset_t *set)
{
    return sys_call(NR_sigpending, (int)set, 0, 0);
}

static int alarm(unsigned int sec)
{
    return sys_call(NR_alarm, (int)sec, 0, 0);
}

static int setpgid(int pid, int pgid)
{
    return sys_call(NR_setpgid, pid, pgid, 0);
}

static int getpgid(int pid)
{
    return sys_call(NR_getpgid, pid, 0, 0);
}

static int getpid(void)
{
    return sys_call(NR_getpid, 0, 0, 0);
}

static int tcgetpgrp(int fd)
{
    return sys_call(NR_tcgetpgrp, fd, 0, 0);
}

static int tcsetpgrp(int fd, int pgrp)
{
    return sys_call(NR_tcsetpgrp, fd, pgrp, 0);
}

static void on_usr1(int sig)
{
    got_sig = sig;
}

static void writable_test(void)
{
    struct stat st;
    char buf[16];
    int fd, n;

    unlink("/p2file");
    fd = open("/p2file", O_CREAT | O_RDWR | O_TRUNC);
    pass("creat writable file", fd >= 0);
    n = write(fd, "hello", 5);
    pass("write", n == 5);
    lseek(fd, 0, SEEK_SET);
    n = read(fd, buf, sizeof(buf));
    pass("read back", n == 5 && seq(buf, "hello"));
    ftruncate(fd, 2);
    sys_call(NR_stat, (int)"/p2file", (int)&st, 0);
    pass("ftruncate", st.st_size == 2);
    close(fd);

    pass("rename", rename("/p2file", "/p2renamed") == 0);
    pass("rename old gone",
         sys_call(NR_stat, (int)"/p2file", (int)&st, 0) < 0);
    pass("rename new exists",
         sys_call(NR_stat, (int)"/p2renamed", (int)&st, 0) == 0 &&
         st.st_size == 2);
    pass("unlink", unlink("/p2renamed") == 0);

    unlink("/p2dir/a");
    rmdir("/p2dir");
    pass("mkdir", mkdir("/p2dir", 0755) == 0);
    fd = open("/p2dir/a", O_CREAT | O_RDWR);
    pass("create in dir", fd >= 0);
    close(fd);
    pass("rmdir non-empty denied", rmdir("/p2dir") < 0);
    pass("unlink in dir", unlink("/p2dir/a") == 0);
    pass("rmdir", rmdir("/p2dir") == 0);

    unlink("/p2app");
    fd = open("/p2app", O_CREAT | O_RDWR | O_APPEND);
    write(fd, "ab", 2);
    lseek(fd, 0, SEEK_SET);
    write(fd, "cd", 2);
    close(fd);
    fd = open("/p2app", O_RDONLY);
    n = read(fd, buf, sizeof(buf));
    if (n > 0) {
        buf[n] = 0;
    }
    close(fd);
    pass("O_APPEND", n == 4 && seq(buf, "abcd"));
    unlink("/p2app");
}

static void mmap_test(void)
{
    unsigned char *p;
    int pid, st = 0, fd;
    void *m;

    p = (unsigned char *)mmap(0, 8192, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    pass("mmap anon", (int)p > 0);
    p[0] = 0x5a;
    p[8191] = 0xa5;
    pass("mmap read/write", p[0] == 0x5a && p[8191] == 0xa5);
    pass("mprotect", mprotect(p, 8192, PROT_READ) == 0);
    pass("msync", sys_call(NR_msync, (int)p, 8192, 0) == 0);

    fd = open("README", O_RDONLY);
    m = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
    pass("mmap file", (int)m > 0 && ((char *)m)[0] == '#' &&
         ((char *)m)[1] == ' ');
    munmap(m, 4096);
    close(fd);

    p = (unsigned char *)mmap(0, 4096, PROT_READ | PROT_WRITE,
                              MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    p[0] = 'A';
    pid = fork();
    if (pid == 0) {
        p[0] = 'B';
        exit(p[0] == 'B' ? 0 : 1);
    }
    waitpid(pid, &st, 0);
    pass("mmap fork copy", st == 0 && p[0] == 'A');
    pass("munmap", munmap(p, 4096) == 0);
}

static void ioctl_test(void)
{
    struct termios t;
    struct winsize ws;

    pass("tcgetattr", ioctl(0, TCGETS, &t) == 0 &&
         (t.c_lflag & ECHO) != 0);
    pass("winsize", ioctl(1, TIOCGWINSZ, &ws) == 0 &&
         ws.ws_row == 56 && ws.ws_col == 76);
}

static void signal_test(void)
{
    struct sigaction sa, old;
    sigset_t set, pend;
    struct timespec ts;
    int pid, st = 0, pg;

    sa.sa_handler = on_usr1;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = sig_restorer;
    pass("sigaction", sigaction(SIGUSR1, &sa, &old) == 0);
    got_sig = 0;
    raise(SIGUSR1);
    pass("handler runs", got_sig == SIGUSR1);

    sa.sa_handler = SIG_IGN;
    sa.sa_restorer = 0;
    sigaction(SIGUSR2, &sa, 0);
    raise(SIGUSR2);
    pass("SIG_IGN survives", 1);

    sa.sa_handler = on_usr1;
    sa.sa_restorer = sig_restorer;
    sigaction(SIGUSR1, &sa, 0);
    set = 1u << SIGUSR1;
    got_sig = 0;
    sigprocmask(SIG_BLOCK, &set, 0);
    raise(SIGUSR1);
    pend = 0;
    sigpending(&pend);
    pass("sigprocmask/sigpending", (pend & set) && got_sig == 0);
    sigprocmask(SIG_UNBLOCK, &set, 0);
    pass("blocked signal delivered", got_sig == SIGUSR1);

    pass("alarm", alarm(1) == 0 && alarm(0) == 1);

    pg = getpgid(0);
    pass("setpgid", setpgid(0, 0) == 0 && getpgid(0) == getpid());
    pass("tcsetpgrp", tcsetpgrp(1, pg) == 0 && tcgetpgrp(1) == pg);

    pid = fork();
    if (pid == 0) {
        for (;;) {
            getpid();
        }
    }
    ts.tv_sec = 0;
    ts.tv_nsec = 100000000;
    sys_call(NR_nanosleep, (int)&ts, 0, 0);
    kill(pid, SIGTERM);
    waitpid(pid, &st, 0);
    pass("kill default action", st == 128 + SIGTERM);
}

void _start(void)
{
    print("\x1b[1;36mPOSIX phase 1+2+3+4 tests\x1b[0m\n");
    getcwd_test();
    stat_test();
    misc_test();
    fcntl_test();
    pipe_test();
    writable_test();
    mmap_test();
    ioctl_test();
    signal_test();

    if (fails == 0) {
        print("\x1b[32mall tests passed\x1b[0m\n");
    } else {
        print("\x1b[31msome tests failed\x1b[0m\n");
    }
    exit(fails == 0 ? 0 : 1);
}
