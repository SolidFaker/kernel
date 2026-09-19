#!/bin/bash
# Build the kernel on Windows using Git Bash + the i686-elf cross toolchain,
# then write the bootable 80m.img. Boot it afterwards with:
#   I:/tools/bochs/bochs.exe -q -f bochsrc.windows
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

# ---- host tool: filesystem image ----
"$HOSTCC" -Os -I./include tools/mkfs.c -o mkfs_kernel.exe
./mkfs_kernel.exe "$OUT/loader.bin" loader.bin "$OUT/kernel.elf" kernel.elf ./README.md README

# ---- disk image ----
[ -f 80m.img ] || dd if=/dev/zero of=80m.img bs=1M count=80
dd if="$OUT/boot.bin"   of=80m.img obs=512 count=1 conv=notrunc
dd if="$OUT/loader.bin" of=80m.img obs=512 seek=1 conv=notrunc
dd if=./kernel.img      of=80m.img obs=512 seek=10 conv=notrunc

echo "---- build output ----"
ls -l "$OUT/boot.bin" "$OUT/loader.bin" "$OUT/kernel.elf" kernel.img 80m.img
echo "note: loader.bin must stay <= 4608 bytes (9 sectors read by boot.s)"
