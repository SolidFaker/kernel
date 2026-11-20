# POSIX layer and libc

The kernel exposes a POSIX-style syscall layer through `int 0x80`; user programs can
use the in-tree libc in `user/libc/` (`crt0.o + libc.a`).

## Processes and descriptors

- `fork`, `execve` (argv/envp copied to the new stack), `exit`, `waitpid` incl. `WNOHANG`
- `#!` script execution with one interpreter argument; Bash and Coreutils use this path
- shared open file descriptions: `dup`, `dup2`, `dup3`, `fcntl`, inherited offsets across fork
- `FD_CLOEXEC`/`O_CLOEXEC`, `F_DUPFD_CLOEXEC`, and `pipe2` flags
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
- canonical and Readline-style non-canonical tty reads; `poll`/`pselect` also see
  characters already buffered by the tty layer
- `ISIG` sends `SIGINT`/`SIGQUIT` to the foreground process group (`Ctrl+C`/`Ctrl+\`),
  including programs such as `yes` that never read stdin
- `VEOF`, `VERASE`, `VKILL` and ANSI arrow/Home/End/Delete sequences support normal
  shell editing and Readline history

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

On Windows, `AUTO_DEMO=1 EXTRA_CFLAGS=-DDEBUG_E9 bash build-win.sh` additionally runs
`user/posix-test.sh` through Bash and reports `POSIX_TEST_OK` over the E9 debug log.

Known gaps: real-time signals (`sigqueue`), IPv6, DNS resolver, full POSIX threads
attributes/cancellation, locale/regex and other optional libc areas.
