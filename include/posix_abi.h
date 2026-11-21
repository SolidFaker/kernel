#ifndef POSIX_ABI_H
#define POSIX_ABI_H

/* Plain-C POSIX ABI shared by the kernel and freestanding user programs.
   Keep this header free of kernel-only types. */

struct timespec {
    unsigned int tv_sec;
    unsigned int tv_nsec;
};

struct timeval {
    unsigned int tv_sec;
    unsigned int tv_usec;
};

struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

#define S_IFMT   0170000
#define S_IFSOCK 0140000
#define S_IFLNK  0120000
#define S_IFREG  0100000
#define S_IFBLK  0060000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000
#define S_IRWXU  0000700
#define S_IRUSR  0000400
#define S_IWUSR  0000200
#define S_IXUSR  0000100
#define S_IRWXG  0000070
#define S_IRWXO  0000007

#define S_ISREG(m)  (((m) & S_IFMT) == S_IFREG)
#define S_ISDIR(m)  (((m) & S_IFMT) == S_IFDIR)
#define S_ISFIFO(m) (((m) & S_IFMT) == S_IFIFO)
#define S_ISCHR(m)  (((m) & S_IFMT) == S_IFCHR)

struct stat {
    unsigned int st_dev;
    unsigned int st_ino;
    unsigned int st_mode;
    unsigned int st_nlink;
    unsigned int st_uid;
    unsigned int st_gid;
    unsigned int st_rdev;
    unsigned int st_size;
    unsigned int st_blksize;
    unsigned int st_blocks;
    unsigned int st_atime;
    unsigned int st_mtime;
    unsigned int st_ctime;
};

#define CLOCK_REALTIME  0
#define CLOCK_MONOTONIC 1

#define WNOHANG 1
#define WUNTRACED 2

#define SIGHUP   1
#define SIGINT   2
#define SIGQUIT  3
#define SIGILL   4
#define SIGTRAP  5
#define SIGABRT  6
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGUSR2 12
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIGCHLD 17
#define SIGCONT 18
#define SIGSTOP 19
#define SIGTSTP 20
#define SIGTTIN 21
#define SIGTTOU 22
#define SIGURG  23
#define SIGWINCH 28

typedef unsigned int sigset_t;
#define NSIG 32
#define SIG_DFL ((void *)0)
#define SIG_IGN ((void *)1)

struct sigaction {
    void (*sa_handler)(int);
    sigset_t sa_mask;
    unsigned int sa_flags;
    void (*sa_restorer)(void);
};

#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define WIFEXITED(s)   (((s) & 0x7f) == 0)
#define WEXITSTATUS(s) (((s) >> 8) & 0xff)
#define WIFSIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)    ((s) & 0x7f)

#define F_DUPFD  0
#define F_GETFD  1
#define F_SETFD  2
#define F_GETFL  3
#define F_SETFL  4
#define F_DUPFD_CLOEXEC 9

#define FD_CLOEXEC 1

#define AF_UNIX 1
#define AF_INET 2
#define SOCK_STREAM 1
#define SOCK_DGRAM  2

#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2

#define MSG_NOSIGNAL 0x4000

struct sockaddr {
    unsigned short sa_family;
    char sa_data[14];
};

struct sockaddr_un {
    unsigned short sun_family;
    char sun_path[108];
};

struct in_addr {
    unsigned int s_addr;
};

struct sockaddr_in {
    unsigned short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

#define INADDR_ANY      0x00000000
#define INADDR_LOOPBACK 0x7f000001
#define INADDR_BROADCAST 0xffffffff

struct sendto_args {
    int fd;
    const void *buf;
    unsigned int len;
    int flags;
    const struct sockaddr *addr;
    unsigned int alen;
};

struct recvfrom_args {
    int fd;
    void *buf;
    unsigned int len;
    int flags;
    struct sockaddr *addr;
    unsigned int *alen;
};

struct pollfd {
    int fd;
    short events;
    short revents;
};

#define POLLIN   0x001
#define POLLPRI  0x002
#define POLLOUT  0x004
#define POLLERR  0x008
#define POLLHUP  0x010
#define POLLNVAL 0x020

#define FD_SETSIZE 32
typedef unsigned int fd_set;
#define FD_ZERO(s) (*(s) = 0)
#define FD_SET(fd, s) (*(s) |= (1u << (fd)))
#define FD_CLR(fd, s) (*(s) &= ~(1u << (fd)))
#define FD_ISSET(fd, s) ((*(s) & (1u << (fd))) != 0)

#define F_OK 0
#define X_OK 1
#define W_OK 2
#define R_OK 4

#define PROT_NONE  0
#define PROT_READ  1
#define PROT_WRITE 2
#define PROT_EXEC  4

#define MAP_SHARED    1
#define MAP_PRIVATE   2
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define MAP_FAILED    ((void *)-1)

struct mmap_args {
    __UINTPTR_TYPE__ addr;
    __UINTPTR_TYPE__ len;
    __UINTPTR_TYPE__ prot;
    __UINTPTR_TYPE__ flags;
    int fd;
    __UINTPTR_TYPE__ offset;
};

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

struct termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[32];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};

#define ISIG   0000001
#define ICANON 0000002
#define ECHO   0000010
#define ECHOE  0000020
#define ICRNL  0000400
#define OPOST  0000001
#define VINTR  0
#define VQUIT  1
#define VERASE 2
#define VKILL  3
#define VEOF   4
#define VTIME  5
#define VMIN   6
#define VSTART 8
#define VSTOP  9
#define VSUSP  10

#define TCGETS     0x5401
#define TCSETS     0x5402
#define TCSETSW    0x5403
#define TCSETSF    0x5404
#define TIOCGPGRP   0x540F
#define TIOCSPGRP   0x5410
#define TIOCGWINSZ 0x5413
#define TIOCSWINSZ 0x5414
#define FIONBIO    0x5421
#define FIONCLEX   0x5450
#define FIOCLEX    0x5451

#endif
