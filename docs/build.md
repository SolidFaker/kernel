# Build and run

## Host prerequisites

macOS:

```sh
brew install i686-elf-gcc qemu
```

Optional for the musl/toybox userland:

- `MUSL_DIR=/tmp/musl-install` — static i386 musl 1.2.5 headers and libraries
- `TOYBOX_BIN=/tmp/toybox-master/toybox` — toybox multicall binary built against musl

Windows: use Git Bash and `build-win.sh`; the script expects the cross toolchain under
`I:/tools` (override with `CROSS=` / `HOSTCC=`).

## Targets

```sh
make all     # build kernel, user programs and build/80m.img
make test    # qemu-system-i386
make test64  # qemu-system-x86_64 (32-bit kernel on a 64-bit CPU)
make clean   # remove build/ and legacy in-tree artifacts
```

`make all` starts with `clean`, so do not run it with `-j`.

## Output layout

All build products live under `build/`; sources stay clean:

```
build/
  obj/            compiler objects
  bin/            boot.bin, loader.bin, kernel.elf, user ELF binaries
  kernel.img      mkfs filesystem image
  80m.img         bootable disk image
  libc.a          in-tree libc archive
  mkfs_kernel     host filesystem-image builder
  kernel.map      linker map
```

`tools/mkfs.c` writes to `MKFS_OUT` when set; the Makefile points it at
`build/kernel.img`.

## QEMU

```sh
qemu-system-i386 -m 64 -drive file=build/80m.img,format=raw,if=ide \
  -nic user,model=e1000 -no-reboot
```

The default shell is toybox (`sh`); the native `shell.elf` remains available as a
fallback when toybox is absent.

## Headless debug

Build with `-DDEBUG_E9` in the kernel CFLAGS and mirror printk output to QEMU's E9 port:

```sh
qemu-system-i386 -display none \
  -chardev stdio,id=e9 -device isa-debugcon,iobase=0xe9,chardev=e9 \
  -drive file=build/80m.img,format=raw,if=ide
```

## Windows

```sh
bash build-win.sh
qemu-system-i386 -m 64 -drive file=build/80m.img,format=raw,if=ide \
  -nic user,model=e1000 -no-reboot
```

`build-win.sh` produces the same `build/` layout, but does not build musl/toybox; the
kernel falls back to `shell.elf`.
