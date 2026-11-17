# POSIX layer and libc

The kernel exposes a POSIX-style syscall layer through `int 0x80`; user programs can
use the in-tree libc in `user/libc/` (`crt0.o + libc.a`).

## Processes and descriptors

- `fork`, `execve` (argv/envp copied to the new stack), `exit`, `waitpid` incl. `WNOHANG`
- shared open file descriptions: `dup`, `dup2`, `fcntl`, inherited offsets across fork
- `pipe`, `O_NONBLOCK`, `poll`, `select`
- `getpid`, `getppid`, `uname`, `isatty`, `brk`
- errno is returned as a negative value; `include/errno.h`

## Files and paths

- `open/close/read/write/lseek/fstat`, `stat`, `access`
- `chdir/getcwd`, path resolution with `.`/`..`, absolute and relative paths
- `mkdir/rmdir/unlink/rename/truncate/ftruncate`
- `chmod`/`fchmod`/`fchmodat`; permission bits are stored per node and persisted
- `open` flags: `O_CREAT`, `O_EXCL`, `O_TRUNC`, `O_APPEND`, `O_NONBLOCK`
- creation modes and chmod bits are reported by `stat`/`statx` and journaled
- writable ramfs imported from the read-only myfs image at boot

## Memory and time

- anonymous and file-private `mmap`, `munmap`, `mprotect`, `msync`, `madvise`
- `clock_gettime`, `gettimeofday`, `time`, `nanosleep`, `alarm`

## Signals and job control

- `kill`, `raise`, `sigaction`, `sigprocmask`, `sigreturn`, `sigpending`, `pause`
- user handlers + restorer, `SIG_IGN`, blocked masks, default terminate
- `SIGCHLD` on child exit
- `setpgid/getpgid/getpgrp/setsid`, `tcgetpgrp/tcsetpgrp`
- `termios`/`ioctl`: `TCGETS`/`TCSETS`, `TIOCGWINSZ`, `FIOCLEX`/`FIONCLEX`

## Sockets and threads

- AF_UNIX stream sockets and `socketpair`
- AF_INET loopback TCP/UDP
- real e1000 UDP and a minimal TCP client (see `network.md`)
- `poll`/`select`, non-blocking I/O
- pthread-style `thread_create/exit/wait`, shared mm with refcounting, including
  libc mutex/condvar and futex

## libc

`user/libc/` contains crt0, syscall wrappers, errno, malloc, stdio, string, dirent,
time, signal, termios, pthread, socket/select/poll, ctype, stdint, limits, assert,
resource, qsort/bsearch/strtol/rand/sysconf and related headers.

Self-tests:

```sh
posix    # 63 POSIX checks
libc     # stdio, malloc, dirent, signals, sockets, pthreads, environment, qsort
```

Known gaps: real-time signals (`sigqueue`), IPv6, DNS resolver, full POSIX threads
attributes/cancellation, locale/regex and other optional libc areas.
