# musl and toybox

The default shell is **toybox sh**, statically linked against musl i386.

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

- default init execs `toybox sh -i`
- compiled applets: `sh echo pwd ls cat true false mkdir rmdir rm touch sleep env
  printf uname test`
- native `shell.elf` is kept as a fallback if toybox is unavailable
- `user/toyrun.c` can launch toybox manually

Limits: only the applets above are included; external toybox applets are not available
unless the toybox config is regenerated.
