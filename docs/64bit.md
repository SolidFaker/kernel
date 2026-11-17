# 64-bit CPUs

The kernel is a 32-bit protected-mode kernel. It runs on 64-bit x86_64 CPUs in 32-bit
compatibility mode:

```sh
make test64   # qemu-system-x86_64
```

Verified: graphics terminal, toybox shell, networking and persistent filesystem work
under `qemu-system-x86_64`.

A true 64-bit long-mode kernel is not implemented. It would require:

- long-mode entry and 4-level page tables
- 64-bit GDT/TSS/IDT and interrupt entry/context switching
- `syscall`/`sysret` ABI instead of `int 0x80`
- ELF64 kernel and user loaders
- 64-bit drivers, pointers and physical addresses
- 64-bit musl/libc and all inline assembly
