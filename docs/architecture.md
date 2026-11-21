# Architecture ports

The tree separates startup, exception frames, address spaces, task switching and
platform devices from the VFS, filesystems, scheduler, syscall implementation and
user programs.

```text
arch/
  x86/        32-bit x86 BIOS boot, paging, interrupts, tasks and platform drivers
  aarch64/    AArch64 EL1 vectors, four-level MMU, SVC/IRQ and task frames
  x86_64/     native x86_64 BIOS loader, long mode, syscall entry and task frames
drivers/      shared devices such as the PL011 UART
include/      shared types, ABI definitions and portable interfaces
kernel/       architecture-independent kernel logic
fs/           VFS, ramfs, pipes, sockets and JFS
user/         native programs and libc
```

Each architecture supplies `include/arch/cpu.h`, `include/arch/user.h`, its linker
scripts and platform files. `common.h` defines `uaddr`, `reg_t`, `usize` and
fixed-width types; portable code must not cast pointers to `u32`.

Architecture user helpers cover process entry, fork return values, thread entry,
signal frames, TLS and syscall register conventions. Linux fcntl flags and stat
layouts are translated where the AArch64 and x86 ABI definitions differ.

## Build

```sh
make all                    # full 32-bit x86 image in build/
bash build-arch.sh aarch64  # build/aarch64/bin/kernel.elf and kernel.img
bash build-arch.sh x86_64   # build/x86_64/80m.img
```

On Windows, `build-arch.sh` uses `I:/tools/zig/zig.exe` for new targets and the
existing i686-elf toolchain for the x86_64 BIOS loader. Override `ZIG=`, `CROSS=`,
`HOSTCC=`, `BASH_BIN=`, `COREUTILS_BIN=` or `COREUTILS_BUILD=` when needed.

The original x86 image uses `build-win.sh` on Windows. `AUTO_DEMO=1` selects the
full POSIX script; the cross-architecture script accepts the same option through
`EXTRA_CFLAGS`. `AUTO_NATIVE` runs both native test programs through Bash, while
`AUTO_SMOKE` performs a short Bash/Coreutils check. `AUTO_POSTEST` directly starts
`postest.elf` for bring-up; because that mode has no Bash parent, its `getppid`
check is intentionally not a valid success criterion in this mode.

## QEMU

```sh
qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -m 256M \
  -kernel build/aarch64/bin/kernel.elf \
  -device loader,file=build/aarch64/kernel.img,addr=0x48000000 \
  -display none -serial stdio -no-reboot

qemu-system-x86_64 -m 128M -display none -serial stdio -no-reboot \
  -drive file=build/x86_64/80m.img,format=raw,if=ide
```

## Validation status

- `x86`: the standard image completes `user/posix-test.sh` with `POSIX_TEST_OK`.
- `x86_64`: native postest/libc, musl Bash/Coreutils and the full script pass.
- `aarch64`: native postest/libc, musl Bash/Coreutils and the full script pass.

The x86, x86_64 and AArch64 images also complete `user/users-test.sh` with
`MULTIUSER_TEST_OK`.

A boot marker alone is not treated as a completed port.
