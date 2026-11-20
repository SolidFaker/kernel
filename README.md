# A-MINI-KERNEL

A small 32-bit x86 operating system kernel written in C and assembly, primarily
targeting QEMU. It uses a custom bootloader, VBE graphics, a POSIX-style syscall
layer, an in-memory filesystem with a persistent journal, and an e1000 network
stack.

## Highlights

- VBE `1280x1024x32` graphical terminal with window chrome, ANSI colors and
  multi-tty support
- Log-structured on-disk filesystem (JFS2) with two-region checkpoints; files,
  directories, deletions and symbolic links survive reboots
- Content-addressed `/store`, atomic `/system` profile link and a lifecycle-separated
  root tree without legacy Unix directories
- POSIX-style processes, file descriptors, pipes, mmap, signals, termios,
  AF_UNIX/AF_INET sockets, `poll`/`select` and pthread-style threads
- In-tree libc, musl 1.2.5, **Bash 5.2** as the default shell, and a
  single-binary **Coreutils 9.5** userland with 105 command entry points
- QEMU e1000 driver: ARP, IPv4, ICMP, UDP and a minimal TCP client
- Runs on `qemu-system-i386`, and on `qemu-system-x86_64` in 32-bit
  compatibility mode

## Quick start

macOS prerequisites:

```sh
brew install i686-elf-gcc qemu
```

Build and boot:

```sh
make all     # all build products go to build/
make test    # qemu-system-i386
make test64  # qemu-system-x86_64
```

The default shell is Bash, with toybox and the native shell as fallbacks. Build
details, toolchain overrides and headless debugging are documented in
`docs/build.md`.

## Documentation

- [docs/build.md](docs/build.md) — build layout, QEMU, Windows, debugging
- [docs/graphics.md](docs/graphics.md) — framebuffer terminal and `gfx` demo
- [docs/posix.md](docs/posix.md) — POSIX syscalls, libc and tests
- [docs/filesystem.md](docs/filesystem.md) — myfs, ramfs and the JFS journal
- [docs/rootfs.md](docs/rootfs.md) — store, system profiles and lifecycle-separated root
- [docs/network.md](docs/network.md) — e1000, ARP/IP/ICMP, UDP/TCP
- [docs/musl-toybox.md](docs/musl-toybox.md) — musl, Bash, Coreutils and toybox
- [docs/64bit.md](docs/64bit.md) — 64-bit CPU compatibility status
- [docs/snapshots.md](docs/snapshots.md) — historical screenshots

## Repository layout

```
boot/       boot sector and 16-bit loader (VBE setup)
kernel/     kernel entry, interrupts, scheduler, syscalls, signals
drivers/    display, framebuffer console, keyboard, timer, disk, e1000, gfx
fs/         VFS, ramfs, pipes, sockets, JFS journal
mm/         page tables, heap, page allocator
include/    shared kernel/user ABI headers
user/       native programs and the in-tree libc
tools/      linker scripts and the host mkfs tool
docs/       project documentation
```
