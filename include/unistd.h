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

/* open() flags (only the access mode is honoured; the filesystem is
   read-only) */
#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2

/* lseek() whence */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#endif
