#!/bin/bash
# Build the kernel on Windows using Git Bash + the i686-elf cross toolchain,
# then write the bootable 80m.img. Boot it afterwards with:
#   qemu-system-i386 -m 64 -drive file=80m.img,format=raw,if=ide -no-reboot
#
# Toolchain layout expected (override with CROSS= / HOSTCC=):
#   I:/tools/i686-elf/bin/i686-elf-gcc.exe ...   (cross compiler)
#   I:/tools/mingw64/bin/gcc.exe                 (host compiler for mkfs)
set -e

CROSS="${CROSS:-I:/tools/i686-elf/bin/i686-elf-}"
HOSTCC="${HOSTCC:-I:/tools/mingw64/bin/gcc.exe}"
CC="${CROSS}gcc"
AS="${CROSS}as"
LD="${CROSS}ld"
OUT=bin
CFLAGS="-c -g -Os -ffreestanding -Wall -Werror -fno-stack-protector -I./include"

"$CC" --version >/dev/null 2>&1 || { echo "cross gcc not found: $CC"; exit 1; }
"$HOSTCC" --version >/dev/null 2>&1 || { echo "host gcc not found: $HOSTCC"; exit 1; }

mkdir -p "$OUT"
rm -f "$OUT"/*

# ---- kernel ----
OBJS=""
for f in $(find kernel mm fs drivers init -name '*.c' | sort); do
    o="${f%.c}.o"
    "$CC" $CFLAGS "$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in $(find kernel -name '*.s' | sort); do
    o="${f%.s}.o"
    "$AS" --32 "$f" -o "$o"
    OBJS="$OBJS $o"
done
"$LD" -Ttools/kernel_link.ld -static -nostdlib --nmagic -melf_i386 $OBJS \
    -o "$OUT/kernel.elf" -Map kernel.map

# ---- boot sector + loader (hd.o/string.o/elf.o are shared with the kernel) ----
"$AS" --32 boot/boot.s -o boot/boot.o
"$LD" -Ttext 0x7c00 --oformat=binary boot/boot.o -o "$OUT/boot.bin"

"$AS" --32 boot/loaderasm.s -o boot/loaderasm.o
"$CC" $CFLAGS boot/loadermain.c -o boot/loadermain.o
"$LD" -Ttools/loader_link.ld -static -nostdlib --nmagic --oformat=binary -melf_i386 \
    boot/loaderasm.o boot/loadermain.o drivers/hd.o kernel/string.o kernel/elf.o \
    -o "$OUT/loader.bin"

# ---- user programs (run by the shell via exec) ----
UFLAGS="-c -Os -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -I./include"
"$CC" $UFLAGS user/hello.c -o user/hello.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic user/hello.o -o user/hello.elf
"$CC" $UFLAGS ${AUTO_DEMO:+-DAUTO_DEMO} user/shell.c -o user/shell.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic user/shell.o -o user/shell.elf
"$CC" $UFLAGS user/gfx.c -o user/gfx.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic user/gfx.o -o user/gfx.elf
"$CC" $UFLAGS user/postest.c -o user/postest.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic user/postest.o -o user/postest.elf

# ---- libc + a standard C demo ----
for f in user/libc/*.c; do
    "$CC" $UFLAGS -Iuser/libc/include "$f" -o "${f%.c}.o"
done
"${CROSS}ar" rcs libc.a user/libc/*.o
"$CC" $UFLAGS -Iuser/libc/include user/libctest.c -o user/libctest.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic \
    user/libc/crt0.o user/libctest.o libc.a -o user/libctest.elf
"$CC" $UFLAGS -Iuser/libc/include user/nettest.c -o user/nettest.o
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic \
    user/libc/crt0.o user/nettest.o libc.a -o user/nettest.elf

# ---- host tool: filesystem image ----
"$HOSTCC" -Os -I./include tools/mkfs.c -o mkfs_kernel.exe
./mkfs_kernel.exe "$OUT/loader.bin" loader.bin "$OUT/kernel.elf" kernel.elf \
    user/hello.elf hello.elf user/shell.elf shell.elf user/gfx.elf gfx.elf \
    user/postest.elf postest.elf user/libctest.elf libctest.elf \
    user/nettest.elf nettest.elf ./README.md README

# ---- disk image ----
[ -f 80m.img ] || dd if=/dev/zero of=80m.img bs=1M count=80
dd if="$OUT/boot.bin"   of=80m.img obs=512 count=1 conv=notrunc
dd if="$OUT/loader.bin" of=80m.img obs=512 seek=1 conv=notrunc
dd if=./kernel.img      of=80m.img obs=512 seek=10 conv=notrunc

echo "---- build output ----"
ls -l "$OUT/boot.bin" "$OUT/loader.bin" "$OUT/kernel.elf" user/hello.elf user/shell.elf kernel.img 80m.img
echo "note: loader.bin must stay <= 4608 bytes (9 sectors read by boot.s)"
