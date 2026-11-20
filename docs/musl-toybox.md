# musl, Bash, Coreutils and toybox

The default shell is **Bash 5.2**, statically linked against musl i386 with the
in-tree Readline/termcap libraries. Toybox `sh` and the native `shell.elf` are
fallbacks.

## Bash and Coreutils

- `bash.elf` starts with `--norc -i`; `AUTO_DEMO` builds run `/posix-test.sh` instead
- Coreutils 9.5 is linked as one `coreutils.elf`; 105 shebang entry points select
  utilities through `--coreutils-prog-shebang=...`
- `execve` supports `#!interpreter optional-arg`, preserves the original arguments,
  and handles `FD_CLOEXEC`, `dup3` and `pipe2`
- the Linux i386 shim additionally covers `pselect6`, `_llseek`, `faccessat*`,
  `utimensat`/`futimens`, `statx` on non-file descriptors, `sysinfo`, resource limits,
  `times`/`getrusage`, `sched_getaffinity`, `prctl` and sync/advice entry points
- `user/posix-test.sh` checks Bash command substitution, pipes, Coreutils file
  operations, sorting, permissions and timestamps

Timestamps are accepted but not stored per inode. Symbolic links are supported by
ramfs, VFS and JFS; hard links, full multi-user credentials and job-control stop/resume
remain unsupported, so affected Coreutils programs can still report errors. The single binary lists every built-in
utility with `coreutils.elf --help`, but this list is not a claim that every option is
implemented by the kernel.

## musl

- musl 1.2.5 is built for i386 with `i686-elf-gcc` and installed under
  `MUSL_DIR` (default `/tmp/musl-install`)
- `user/hello_musl.elf` is a minimal musl hello world
- musl binaries are marked with ELF OSABI `0xff`; the kernel uses this to select the
  Linux i386 syscall ABI

Kernel Linux ABI shim (`kernel/syscall.c`):

- `set_thread_area` (GDT TLS entries), `set_tid_address`, `mmap2`, `brk`
- `writev`/`readv`, `statx`, `getdents64`, `openat`, `mkdirat`, `unlinkat`,
  `renameat`, `fcntl64`
- `fork`/`vfork`/`clone` (process form), `execve`, `wait4`
- `rt_sigaction`/`rt_sigprocmask`/`sigreturn` for musl restorers
- basic futex, process groups, termios ioctls, `prlimit64`, `getrandom`
- canonical tty line editing for Linux ABI reads (echo, backspace, Enter)

## toybox

`TOYBOX_BIN` defaults to `/tmp/toybox-master/toybox`. It is built with musl headers and
the `__linux__` macro, then copied to `build/bin/toybox.elf` with OSABI patched to
`0xff`.

- toybox is tried as `toybox sh -i` only when Bash is unavailable
- compiled applets: `sh echo pwd ls cat true false mkdir rmdir rm touch chmod
  sleep env printf uname test`
- native `shell.elf` is kept as a fallback if toybox is unavailable
- `user/toyrun.c` can launch toybox manually

Limits: only the applets above are included; external toybox applets are not available
unless the toybox config is regenerated.
