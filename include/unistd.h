#ifndef UNISTD_H
#define UNISTD_H

/* Syscall numbers for int 0x80 (eax), shared by the kernel and the
   user programs. Register convention follows the Linux i386 ABI:
   args in ebx, ecx, edx; return value in eax. */

#define NR_print       0   /* legacy: write a string to the tty */
#define NR_fork        1
#define NR_print_hex   2   /* retired */
#define NR_exec        3   /* execve(path, argv, envp); argv==0 -> {path}; envp ignored */
#define NR_exit        4   /* exit(status) */
#define NR_read        5   /* read(fd, buf, count) */
#define NR_ls          6   /* retired: open(".") + getdents */
#define NR_cat         7   /* retired: open() + read() */
#define NR_chtty       8
#define NR_ps          9
#define NR_open        10  /* open(path, flags) -> fd */
#define NR_close       11  /* close(fd) */
#define NR_write       12  /* write(fd, buf, count) */
#define NR_lseek       13  /* lseek(fd, offset, whence) */
#define NR_fstat       14  /* fstat(fd, struct stat *) */
#define NR_getpid      15
#define NR_getdents    16  /* getdents(fd, buf, max) */
#define NR_waitpid     17  /* waitpid(pid, &status, 0); pid==-1 = any child */
#define NR_brk         18  /* brk(addr); addr==0 returns the current break */
#define NR_nanosleep   19  /* nanosleep({sec,nsec}, NULL) */
#define NR_time        20  /* time(NULL): uptime in seconds */
#define NR_gfx         21  /* gfx(struct gfx_req *): see gfx.h */
#define NR_dup         22  /* dup(oldfd) */
#define NR_dup2        23  /* dup2(oldfd, newfd) */
#define NR_fcntl       24  /* fcntl(fd, cmd, arg) */
#define NR_pipe        25  /* pipe(int fds[2]) */
#define NR_getppid     26
#define NR_isatty      27
#define NR_uname       28  /* uname(struct utsname *) */
#define NR_clock_gettime 29 /* clock_gettime(clk, struct timespec *) */
#define NR_gettimeofday 30  /* gettimeofday(struct timeval *, void *) */
#define NR_chdir       31
#define NR_getcwd      32  /* getcwd(buf, size) */
#define NR_stat        33  /* stat(path, struct stat *) */
#define NR_access      34  /* access(path, mode) */
#define NR_mkdir       35  /* mkdir(path, mode) */
#define NR_rmdir       36  /* rmdir(path) */
#define NR_unlink      37  /* unlink(path) */
#define NR_rename      38  /* rename(old, new) */
#define NR_truncate    39  /* truncate(path, length) */
#define NR_ftruncate   40  /* ftruncate(fd, length) */
#define NR_mmap        41  /* mmap(addr, len, prot, flags, fd, off) */
#define NR_munmap      42  /* munmap(addr, len) */
#define NR_mprotect    43  /* mprotect(addr, len, prot) */
#define NR_msync       44  /* msync(addr, len, flags) */
#define NR_madvise     45  /* madvise(addr, len, advice) */
#define NR_ioctl       46  /* ioctl(fd, request, arg) */
#define NR_kill        47  /* kill(pid, sig) */
#define NR_raise       48  /* raise(sig) */
#define NR_sigaction   49  /* sigaction(sig, act, oldact) */
#define NR_sigprocmask 50  /* sigprocmask(how, set, oldset) */
#define NR_sigreturn   51  /* return from a signal handler */
#define NR_sigpending  52  /* sigpending(set) */
#define NR_alarm       53  /* alarm(seconds) */
#define NR_pause       54  /* pause() */
#define NR_setpgid     55
#define NR_getpgid     56
#define NR_getpgrp     57
#define NR_setsid      58
#define NR_tcgetpgrp   59
#define NR_tcsetpgrp   60
#define NR_socket      61
#define NR_bind        62
#define NR_listen      63
#define NR_accept      64
#define NR_connect     65
#define NR_socketpair  66
#define NR_shutdown    67
#define NR_getsockname 68
#define NR_getpeername 69
#define NR_poll        70
#define NR_setsockopt  71
#define NR_getsockopt  72
#define NR_sendto      73
#define NR_recvfrom    74
#define NR_thread_create 75
#define NR_thread_exit   76
#define NR_thread_wait   77
#define NR_futex         78
#define NR_ping          79

/* open() flags (only the access mode is honoured; the filesystem is
   read-only) */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0100
#define O_EXCL   0200
#define O_TRUNC  01000
#define O_APPEND 02000
#define O_NONBLOCK 04000

/* lseek() whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
