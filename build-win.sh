#!/bin/bash
# Build the kernel on Windows using Git Bash + the i686-elf cross toolchain.
# All build products go to build/ (sources stay clean). Boot with:
#   qemu-system-i386 -m 64 -drive file=build/80m.img,format=raw,if=ide -no-reboot
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
AR="${CROSS}ar"
BUILD=build
OUT="$BUILD/bin"
OBJ="$BUILD/obj"
CFLAGS="-c -g -Os -ffreestanding -Wall -Werror -fno-stack-protector -I./include"

"$CC" --version >/dev/null 2>&1 || { echo "cross gcc not found: $CC"; exit 1; }
"$HOSTCC" --version >/dev/null 2>&1 || { echo "host gcc not found: $HOSTCC"; exit 1; }

rm -rf "$BUILD"
mkdir -p "$OUT" "$OBJ"

# ---- kernel ----
OBJS=""
for f in $(find kernel mm fs drivers init -name '*.c' | sort); do
    o="$OBJ/${f%.c}.o"
    mkdir -p "$(dirname "$o")"
    "$CC" $CFLAGS "$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in $(find kernel -name '*.s' | sort); do
    o="$OBJ/${f%.s}.o"
    mkdir -p "$(dirname "$o")"
    "$AS" --32 "$f" -o "$o"
    OBJS="$OBJS $o"
done
"$LD" -Ttools/kernel_link.ld -static -nostdlib --nmagic -melf_i386 $OBJS \
    -o "$OUT/kernel.elf" -Map "$BUILD/kernel.map"

# ---- boot sector + loader (hd.o/string.o/elf.o are shared with the kernel) ----
"$AS" --32 boot/boot.s -o "$OBJ/boot/boot.o"
"$LD" -Ttext 0x7c00 --oformat=binary "$OBJ/boot/boot.o" -o "$OUT/boot.bin"

"$AS" --32 boot/loaderasm.s -o "$OBJ/boot/loaderasm.o"
"$CC" $CFLAGS boot/loadermain.c -o "$OBJ/boot/loadermain.o"
"$LD" -Ttools/loader_link.ld -static -nostdlib --nmagic --oformat=binary -melf_i386 \
    "$OBJ/boot/loaderasm.o" "$OBJ/boot/loadermain.o" "$OBJ/drivers/hd.o" \
    "$OBJ/kernel/string.o" "$OBJ/kernel/elf.o" -o "$OUT/loader.bin"

# ---- user programs ----
UFLAGS="-c -Os -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -I./include"
"$CC" $UFLAGS user/hello.c -o "$OBJ/user/hello.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic "$OBJ/user/hello.o" -o "$OUT/hello.elf"
"$CC" $UFLAGS ${AUTO_DEMO:+-DAUTO_DEMO} user/shell.c -o "$OBJ/user/shell.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic "$OBJ/user/shell.o" -o "$OUT/shell.elf"
"$CC" $UFLAGS user/gfx.c -o "$OBJ/user/gfx.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic "$OBJ/user/gfx.o" -o "$OUT/gfx.elf"
"$CC" $UFLAGS user/postest.c -o "$OBJ/user/postest.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic "$OBJ/user/postest.o" -o "$OUT/postest.elf"
"$CC" $UFLAGS user/toyrun.c -o "$OBJ/user/toyrun.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic "$OBJ/user/toyrun.o" -o "$OUT/toyrun.elf"

# ---- libc + standard C demos ----
for f in user/libc/*.c; do
    o="$OBJ/user/libc/${f##*/}"
    o="${o%.c}.o"
    "$CC" $UFLAGS -Iuser/libc/include "$f" -o "$o"
done
"$AR" rcs "$BUILD/libc.a" "$OBJ"/user/libc/*.o
"$CC" $UFLAGS -Iuser/libc/include user/libctest.c -o "$OBJ/user/libctest.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic \
    "$OBJ/user/libc/crt0.o" "$OBJ/user/libctest.o" "$BUILD/libc.a" -o "$OUT/libctest.elf"
"$CC" $UFLAGS -Iuser/libc/include user/nettest.c -o "$OBJ/user/nettest.o"
"$LD" -Ttools/user_link.ld -static -nostdlib --nmagic \
    "$OBJ/user/libc/crt0.o" "$OBJ/user/nettest.o" "$BUILD/libc.a" -o "$OUT/nettest.elf"

# ---- host tool: filesystem image ----
"$HOSTCC" -Os -I./include tools/mkfs.c -o "$BUILD/mkfs_kernel.exe"
MKFS_OUT="$BUILD/kernel.img" ./"$BUILD/mkfs_kernel.exe" \
    "$OUT/loader.bin" loader.bin "$OUT/kernel.elf" kernel.elf \
    "$OUT/hello.elf" hello.elf "$OUT/shell.elf" shell.elf \
    "$OUT/gfx.elf" gfx.elf "$OUT/postest.elf" postest.elf \
    "$OUT/libctest.elf" libctest.elf "$OUT/nettest.elf" nettest.elf \
    "$OUT/toyrun.elf" toyrun.elf ./README.md README

# ---- disk image ----
[ -f "$BUILD/80m.img" ] || dd if=/dev/zero of="$BUILD/80m.img" bs=1M count=80
dd if="$OUT/boot.bin"   of="$BUILD/80m.img" obs=512 count=1 conv=notrunc
dd if="$OUT/loader.bin" of="$BUILD/80m.img" obs=512 seek=1 conv=notrunc
dd if="$BUILD/kernel.img" of="$BUILD/80m.img" obs=512 seek=10 conv=notrunc

echo "---- build output ----"
ls -l "$OUT/boot.bin" "$OUT/loader.bin" "$OUT/kernel.elf" "$OUT/hello.elf" "$OUT/shell.elf" "$BUILD/kernel.img" "$BUILD/80m.img"
echo "note: loader.bin must stay <= 4608 bytes (9 sectors read by boot.s)"
