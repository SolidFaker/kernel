#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/utsname.h>
#include <sys/time.h>
#include <posix_abi.h>
#include <stdarg.h>
#include <sys/socket.h>
#include <poll.h>
#include <sys/select.h>
#include <arch/user.h>

int errno;

static reg_t sys3(reg_t nr, reg_t a, reg_t b, reg_t c)
{
    return arch_syscall3(nr, a, b, c);
}

static reg_t sys4(reg_t nr, reg_t a, reg_t b, reg_t c, reg_t d)
{
    return arch_syscall4(nr, a, b, c, d);
}

static int chk(int r)
{
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return r;
}

ssize_t read(int fd, void *buf, size_t count)
{
    return chk(sys3(NR_read, fd, (uaddr)buf, (uaddr)count));
}

ssize_t write(int fd, const void *buf, size_t count)
{
    return chk(sys3(NR_write, fd, (uaddr)buf, (uaddr)count));
}

int close(int fd)
{
    return chk(sys3(NR_close, fd, 0, 0));
}

off_t lseek(int fd, off_t off, int whence)
{
    return chk(sys3(NR_lseek, fd, (uaddr)off, whence));
}

pid_t fork(void)
{
    return chk(sys3(NR_fork, 0, 0, 0));
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    return chk(sys3(NR_exec, (uaddr)path, (uaddr)argv, (uaddr)envp));
}

void _exit(int status)
{
    sys3(NR_exit, status, 0, 0);
    for (;;) {
    }
}

void exit(int status)
{
    _exit(status);
}

pid_t getpid(void)
{
    return chk(sys3(NR_getpid, 0, 0, 0));
}

pid_t getppid(void)
{
    return chk(sys3(NR_getppid, 0, 0, 0));
}

uid_t getuid(void) { return chk(sys3(NR_getuid, 0, 0, 0)); }
uid_t geteuid(void) { return chk(sys3(NR_geteuid, 0, 0, 0)); }
gid_t getgid(void) { return chk(sys3(NR_getgid, 0, 0, 0)); }
gid_t getegid(void) { return chk(sys3(NR_getegid, 0, 0, 0)); }
int setuid(uid_t uid) { return chk(sys3(NR_setuid, uid, 0, 0)); }
int setgid(gid_t gid) { return chk(sys3(NR_setgid, gid, 0, 0)); }
int seteuid(uid_t uid) { return chk(sys3(NR_seteuid, uid, 0, 0)); }
int setegid(gid_t gid) { return chk(sys3(NR_setegid, gid, 0, 0)); }
int setresuid(uid_t uid, uid_t euid, uid_t suid)
{
    return chk(sys3(NR_setresuid, uid, euid, suid));
}
int setresgid(gid_t gid, gid_t egid, gid_t sgid)
{
    return chk(sys3(NR_setresgid, gid, egid, sgid));
}
int getgroups(int size, gid_t groups[])
{
    return chk(sys3(NR_getgroups, size, (uaddr)groups, 0));
}
int setgroups(int size, const gid_t groups[])
{
    return chk(sys3(NR_setgroups, size, (uaddr)groups, 0));
}
int chown(const char *path, uid_t uid, gid_t gid)
{
    return chk(sys3(NR_chown, (uaddr)path, uid, gid));
}
int fchown(int fd, uid_t uid, gid_t gid)
{
    return chk(sys3(NR_fchown, fd, uid, gid));
}

int pipe(int fds[2])
{
    return chk(sys3(NR_pipe, (uaddr)fds, 0, 0));
}

int dup(int fd)
{
    return chk(sys3(NR_dup, fd, 0, 0));
}

int dup2(int oldfd, int newfd)
{
    return chk(sys3(NR_dup2, oldfd, newfd, 0));
}

int isatty(int fd)
{
    return chk(sys3(NR_isatty, fd, 0, 0));
}

int chdir(const char *path)
{
    return chk(sys3(NR_chdir, (uaddr)path, 0, 0));
}

char *getcwd(char *buf, size_t size)
{
    if (chk(sys3(NR_getcwd, (uaddr)buf, (uaddr)size, 0)) < 0) {
        return 0;
    }
    return buf;
}

int unlink(const char *path)
{
    return chk(sys3(NR_unlink, (uaddr)path, 0, 0));
}

int rmdir(const char *path)
{
    return chk(sys3(NR_rmdir, (uaddr)path, 0, 0));
}

int access(const char *path, int mode)
{
    return chk(sys3(NR_access, (uaddr)path, mode, 0));
}

int truncate(const char *path, off_t length)
{
    return chk(sys3(NR_truncate, (uaddr)path, (uaddr)length, 0));
}

int ftruncate(int fd, off_t length)
{
    return chk(sys3(NR_ftruncate, fd, (uaddr)length, 0));
}

int stat(const char *path, struct stat *st)
{
    return chk(sys3(NR_stat, (uaddr)path, (uaddr)st, 0));
}

int fstat(int fd, struct stat *st)
{
    return chk(sys3(NR_fstat, fd, (uaddr)st, 0));
}

int mkdir(const char *path, mode_t mode)
{
    return chk(sys3(NR_mkdir, (uaddr)path, (uaddr)mode, 0));
}

int chmod(const char *path, mode_t mode)
{
    return chk(sys3(NR_chmod, (uaddr)path, (uaddr)mode, 0));
}

int fchmod(int fd, mode_t mode)
{
    return chk(sys3(NR_fchmod, fd, (uaddr)mode, 0));
}

int rename(const char *oldp, const char *newp)
{
    return chk(sys3(NR_rename, (uaddr)oldp, (uaddr)newp, 0));
}

int open(const char *path, int flags, ...)
{
    return chk(sys3(NR_open, (uaddr)path, flags, 0));
}

int fcntl(int fd, int cmd, ...)
{
    va_list ap;
    int arg;
    va_start(ap, cmd);
    arg = va_arg(ap, int);
    va_end(ap);
    return chk(sys3(NR_fcntl, fd, cmd, arg));
}

pid_t waitpid(pid_t pid, int *status, int options)
{
    return chk(sys3(NR_waitpid, pid, (uaddr)status, options));
}

int getdents(int fd, char *buf, unsigned int max)
{
    return chk(sys3(NR_getdents, fd, (uaddr)buf, (uaddr)max));
}

int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;
    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);
    return chk(sys3(NR_ioctl, fd, (uaddr)request, (uaddr)arg));
}

int uname(struct utsname *u)
{
    return chk(sys3(NR_uname, (uaddr)u, 0, 0));
}

int clock_gettime(int clk, struct timespec *ts)
{
    return chk(sys3(NR_clock_gettime, clk, (uaddr)ts, 0));
}

int gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    return chk(sys3(NR_gettimeofday, (uaddr)tv, 0, 0));
}

int nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    return chk(sys3(NR_nanosleep, (uaddr)req, 0, 0));
}

unsigned int sleep(unsigned int seconds)
{
    struct timespec ts;
    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    nanosleep(&ts, 0);
    return 0;
}

int usleep(unsigned int usec)
{
    struct timespec ts;
    ts.tv_sec = usec / 1000000u;
    ts.tv_nsec = (usec % 1000000u) * 1000u;
    return nanosleep(&ts, 0);
}

time_t time(time_t *t)
{
    int r = sys3(NR_time, 0, 0, 0);
    if (r < 0) {
        errno = -r;
        return (time_t)-1;
    }
    if (t) {
        *t = (time_t)r;
    }
    return (time_t)r;
}

long brk(void *addr)
{
    int r = sys3(NR_brk, (uaddr)addr, 0, 0);
    if (r < 0) {
        errno = -r;
        return -1;
    }
    return 0;
}

void *sbrk(int incr)
{
    int cur = sys3(NR_brk, 0, 0, 0);
    if (cur < 0) {
        errno = -cur;
        return (void *)-1;
    }
    if (incr == 0) {
        return (void *)cur;
    }
    if (sys3(NR_brk, cur + incr, 0, 0) < 0) {
        errno = ENOMEM;
        return (void *)-1;
    }
    return (void *)cur;
}

void *mmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
    struct mmap_args ma;
    int r;
    ma.addr = (unsigned int)addr;
    ma.len = (unsigned int)len;
    ma.prot = (unsigned int)prot;
    ma.flags = (unsigned int)flags;
    ma.fd = fd;
    ma.offset = (unsigned int)off;
    r = sys3(NR_mmap, (uaddr)&ma, 0, 0);
    if (r < 0) {
        errno = -r;
        return MAP_FAILED;
    }
    return (void *)r;
}

int munmap(void *addr, size_t len)
{
    return chk(sys3(NR_munmap, (uaddr)addr, (uaddr)len, 0));
}

int mprotect(void *addr, size_t len, int prot)
{
    return chk(sys3(NR_mprotect, (uaddr)addr, (uaddr)len, prot));
}

int msync(void *addr, size_t len, int flags)
{
    return chk(sys3(NR_msync, (uaddr)addr, (uaddr)len, flags));
}

int madvise(void *addr, size_t len, int advice)
{
    return chk(sys3(NR_madvise, (uaddr)addr, (uaddr)len, advice));
}

int kill(int pid, int sig)
{
    return chk(sys3(NR_kill, pid, sig, 0));
}

int raise(int sig)
{
    return chk(sys3(NR_raise, sig, 0, 0));
}

int sigaction(int sig, const struct sigaction *act, struct sigaction *old)
{
    return chk(sys3(NR_sigaction, sig, (uaddr)act, (uaddr)old));
}

int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
    return chk(sys3(NR_sigprocmask, how, (uaddr)set, (uaddr)old));
}

int sigpending(sigset_t *set)
{
    return chk(sys3(NR_sigpending, (uaddr)set, 0, 0));
}

extern void __libc_sigrestorer(void);

sighandler_t signal(int sig, sighandler_t handler)
{
    struct sigaction sa, old;
    sa.sa_handler = handler;
    sa.sa_mask = 0;
    sa.sa_flags = 0;
    sa.sa_restorer = (handler == SIG_IGN || handler == SIG_DFL) ?
        0 : __libc_sigrestorer;
    if (sigaction(sig, &sa, &old) < 0) {
        return (sighandler_t)-1;
    }
    return old.sa_handler;
}

unsigned int alarm(unsigned int seconds)
{
    int r = sys3(NR_alarm, (uaddr)seconds, 0, 0);
    if (r < 0) {
        errno = -r;
        return 0;
    }
    return (unsigned int)r;
}

int pause(void)
{
    return chk(sys3(NR_pause, 0, 0, 0));
}

pid_t getpgrp(void)
{
    return chk(sys3(NR_getpgrp, 0, 0, 0));
}

int setpgid(pid_t pid, pid_t pgid)
{
    return chk(sys3(NR_setpgid, pid, pgid, 0));
}

pid_t getpgid(pid_t pid)
{
    return chk(sys3(NR_getpgid, pid, 0, 0));
}

pid_t setsid(void)
{
    return chk(sys3(NR_setsid, 0, 0, 0));
}

pid_t tcgetpgrp(int fd)
{
    return chk(sys3(NR_tcgetpgrp, fd, 0, 0));
}

int tcsetpgrp(int fd, pid_t pgrp)
{
    return chk(sys3(NR_tcsetpgrp, fd, pgrp, 0));
}

int socket(int domain, int type, int protocol)
{
    return chk(sys3(NR_socket, domain, type, protocol));
}

int bind(int fd, const struct sockaddr *addr, socklen_t len)
{
    return chk(sys3(NR_bind, fd, (uaddr)addr, (uaddr)len));
}

int listen(int fd, int backlog)
{
    return chk(sys3(NR_listen, fd, backlog, 0));
}

int accept(int fd, struct sockaddr *addr, socklen_t *len)
{
    return chk(sys3(NR_accept, fd, (uaddr)addr, (uaddr)len));
}

int connect(int fd, const struct sockaddr *addr, socklen_t len)
{
    return chk(sys3(NR_connect, fd, (uaddr)addr, (uaddr)len));
}

int socketpair(int domain, int type, int protocol, int sv[2])
{
    return chk(sys4(NR_socketpair, domain, type, protocol, (uaddr)sv));
}

int shutdown(int fd, int how)
{
    return chk(sys3(NR_shutdown, fd, how, 0));
}

int getsockname(int fd, struct sockaddr *addr, socklen_t *len)
{
    return chk(sys3(NR_getsockname, fd, (uaddr)addr, (uaddr)len));
}

int getpeername(int fd, struct sockaddr *addr, socklen_t *len)
{
    return chk(sys3(NR_getpeername, fd, (uaddr)addr, (uaddr)len));
}

long send(int fd, const void *buf, size_t len, int flags)
{
    (void)flags;
    return write(fd, buf, len);
}

long recv(int fd, void *buf, size_t len, int flags)
{
    (void)flags;
    return read(fd, buf, len);
}

long sendto(int fd, const void *buf, size_t len, int flags,
            const struct sockaddr *addr, socklen_t alen)
{
    struct sendto_args a;
    a.fd = fd;
    a.buf = buf;
    a.len = (unsigned int)len;
    a.flags = flags;
    a.addr = addr;
    a.alen = alen;
    return chk(sys3(NR_sendto, (uaddr)&a, 0, 0));
}

long recvfrom(int fd, void *buf, size_t len, int flags,
              struct sockaddr *addr, socklen_t *alen)
{
    struct recvfrom_args a;
    a.fd = fd;
    a.buf = buf;
    a.len = (unsigned int)len;
    a.flags = flags;
    a.addr = addr;
    a.alen = (unsigned int *)alen;
    return chk(sys3(NR_recvfrom, (uaddr)&a, 0, 0));
}

int setsockopt(int fd, int level, int name, const void *val, socklen_t len)
{
    (void)val;
    (void)len;
    return chk(sys3(NR_setsockopt, fd, level, name));
}

int getsockopt(int fd, int level, int name, void *val, socklen_t *len)
{
    (void)val;
    (void)len;
    return chk(sys3(NR_getsockopt, fd, level, name));
}

int poll(struct pollfd *fds, unsigned int nfds, int timeout)
{
    return chk(sys3(NR_poll, (uaddr)fds, (uaddr)nfds, timeout));
}

int select(int nfds, fd_set *readfds, fd_set *writefds,
           fd_set *exceptfds, struct timeval *timeout)
{
    struct pollfd pfds[FD_SETSIZE];
    fd_set rf = readfds ? *readfds : 0;
    fd_set wf = writefds ? *writefds : 0;
    fd_set ef = exceptfds ? *exceptfds : 0;
    int n = 0, i, r, tmo = -1;

    if (nfds > FD_SETSIZE) {
        nfds = FD_SETSIZE;
    }
    for (i = 0; i < nfds; i++) {
        short ev = 0;
        if (rf & (1u << i)) {
            ev |= POLLIN;
        }
        if (wf & (1u << i)) {
            ev |= POLLOUT;
        }
        if (ef & (1u << i)) {
            ev |= POLLPRI;
        }
        if (ev) {
            pfds[n].fd = i;
            pfds[n].events = ev;
            pfds[n].revents = 0;
            n++;
        }
    }
    if (timeout) {
        if (timeout->tv_sec > 0x7FFFFF) {
            tmo = 0x7FFFFFFF;
        } else {
            tmo = (uaddr)(timeout->tv_sec * 1000u + timeout->tv_usec / 1000u);
        }
    }
    r = poll(pfds, (unsigned int)n, tmo);
    if (r <= 0) {
        return r;
    }
    {
        fd_set nr = 0, nw = 0, ne = 0;
        for (i = 0; i < n; i++) {
            int fd = pfds[i].fd;
            short re = pfds[i].revents;
            if (re & (POLLIN | POLLHUP | POLLERR)) {
                nr |= 1u << fd;
            }
            if (re & POLLOUT) {
                nw |= 1u << fd;
            }
            if (re & POLLPRI) {
                ne |= 1u << fd;
            }
        }
        if (readfds) {
            *readfds = nr;
        }
        if (writefds) {
            *writefds = nw;
        }
        if (exceptfds) {
            *exceptfds = ne;
        }
    }
    return r;
}

int tcgetattr(int fd, struct termios *t)
{
    return ioctl(fd, TCGETS, t);
}

int tcsetattr(int fd, int actions, const struct termios *t)
{
    (void)actions;
    return ioctl(fd, TCSETS, (void *)t);
}
