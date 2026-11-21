# Build and run

## Host prerequisites

macOS:

```sh
brew install i686-elf-gcc qemu
```

External userland build products used by `make all`:

- `MUSL_DIR=/tmp/musl-install` — static i386 musl 1.2.5 headers and libraries
- `TOYBOX_BIN=/tmp/toybox-master/toybox` — toybox multicall binary built against musl
- `BASH_BIN=/tmp/bash-build/bash` — static i386 Bash 5.2 with Readline
- `COREUTILS_BIN=/tmp/coreutils-build/src/coreutils` — Coreutils 9.5 single binary
- `COREUTILS_BUILD=/tmp/coreutils-build` — build tree containing `src/coreutils.h`

Windows: use Git Bash and `build-win.sh`; the script expects the cross toolchain under
`I:/tools` (override with `CROSS=` / `HOSTCC=`). Its default userland paths are
`I:/tools/posix/build/bash/bash` and `I:/tools/posix/build/coreutils/src/coreutils`;
`BASH_BIN=`, `COREUTILS_BIN=` and `COREUTILS_BUILD=` can override them.

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

The default shell is Bash. Toybox `sh` and the native `shell.elf` remain available
as fallbacks when their binaries are present. Coreutils uses one static binary plus
small shebang entry points, so the image does not contain a separate libc copy for
every utility.

## Other architectures

Use `bash build-arch.sh aarch64|x86_64` for the non-legacy images. Their
products are separated into `build/<arch>/`, so they do not overwrite the x86
`build/80m.img`. The script uses Zig for AArch64 and the x86_64 kernel, plus
i686-elf binutils/GCC for the x86_64 BIOS loader.

For a non-interactive regression, set `EXTRA_CFLAGS=-DAUTO_DEMO` (full POSIX
script), `-DAUTO_NATIVE` (postest and libctest through Bash),
`-DAUTO_SMOKE` (short Bash/Coreutils test) or `-DAUTO_MULTI` (multi-user test).
See `docs/architecture.md` for QEMU
loader arguments and the current validation status.

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

`build-win.sh` produces the same `build/` layout and packages Bash/Coreutils, but does
not build toybox or `hello_musl.elf`. Set `EXTRA_CFLAGS=-DDEBUG_E9` for headless
logging. `AUTO_DEMO=1` runs `user/posix-test.sh` through Bash instead of starting the
interactive shell.
