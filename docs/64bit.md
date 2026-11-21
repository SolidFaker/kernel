# Native 64-bit ports

The 32-bit x86 kernel remains available and can also run on an x86_64 CPU in
compatibility mode:

```sh
make test64
```

The native ports are separate kernels, not compatibility-mode launches:

- `x86_64`: BIOS loader, PAE/long-mode transition, four-level paging, high-half
  kernel, syscall/sysret entry, ELF64 loading, process copies, signals and threads.
  The interactive console reuses the VBE framebuffer renderer and PS/2 keyboard;
  serial input/output remains available for headless runs.
- `aarch64`: EL1 vectors, SVC entry, four-level paging, independent process page
  tables, context switches, GIC/generic-timer interrupts and PL011 console.

Both ports run the lifecycle-separated VFS root, native postest/libc tests and the
static musl Bash/Coreutils userland. Their full POSIX scripts have produced
`POSIX_TEST_OK` under QEMU.

## Windows build and test

```sh
bash build-arch.sh x86_64
bash build-arch.sh aarch64

qemu-system-x86_64 -m 128M -display none -serial stdio -no-reboot \
  -drive file=build/x86_64/80m.img,format=raw,if=ide

qemu-system-aarch64 -M virt,gic-version=2 -cpu cortex-a53 -m 256M \
  -kernel build/aarch64/bin/kernel.elf \
  -device loader,file=build/aarch64/kernel.img,addr=0x48000000 \
  -display none -serial stdio -no-reboot
```

Use `EXTRA_CFLAGS=-DAUTO_DEMO` for the full POSIX script, `-DAUTO_NATIVE` for
`postest.elf` plus `libctest.elf`, or `-DAUTO_SMOKE` for a short Bash/Coreutils
check. `build-arch.sh` defaults to the independently linked binaries in
`I:/tools/posix/build/{x86_64,aarch64}-{bash,coreutils}-native`; override
`BASH_BIN`, `COREUTILS_BIN` and `COREUTILS_BUILD` for another build.
