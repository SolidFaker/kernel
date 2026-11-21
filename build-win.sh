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
ARCH="${ARCH:-x86}"
ARCH_DIR="arch/$ARCH"
BUILD=build
OUT="$BUILD/bin"
OBJ="$BUILD/obj"
CFLAGS="-c -g -Os -ffreestanding -Wall -Werror -fno-stack-protector -I./include -I./$ARCH_DIR/include ${EXTRA_CFLAGS:-} ${AUTO_DEMO:+-DAUTO_DEMO}"

"$CC" --version >/dev/null 2>&1 || { echo "cross gcc not found: $CC"; exit 1; }
"$HOSTCC" --version >/dev/null 2>&1 || { echo "host gcc not found: $HOSTCC"; exit 1; }

rm -rf "$BUILD"
mkdir -p "$OUT" "$OBJ/boot" "$OBJ/user" "$OBJ/user/libc"

# ---- kernel ----
OBJS=""
for f in $(find kernel mm fs drivers init "$ARCH_DIR/kernel" "$ARCH_DIR/mm" "$ARCH_DIR/drivers" -name '*.c' | sort); do
    if [ "$f" = kernel/arch_entry.c ]; then continue; fi
    o="$OBJ/${f%.c}.o"
    mkdir -p "$(dirname "$o")"
    "$CC" $CFLAGS "$f" -o "$o"
    OBJS="$OBJS $o"
done
for f in $(find kernel "$ARCH_DIR/kernel" -name '*.s' | sort); do
    o="$OBJ/${f%.s}.o"
    mkdir -p "$(dirname "$o")"
    "$AS" --32 "$f" -o "$o"
    OBJS="$OBJS $o"
done
"$LD" -T"$ARCH_DIR/kernel_link.ld" -static -nostdlib --nmagic -melf_i386 $OBJS \
    -o "$OUT/kernel.elf" -Map "$BUILD/kernel.map"

# ---- boot sector + loader (hd.o/string.o/elf.o are shared with the kernel) ----
"$AS" --32 "$ARCH_DIR/boot/boot.s" -o "$OBJ/boot/boot.o"
"$LD" -Ttext 0x7c00 --oformat=binary "$OBJ/boot/boot.o" -o "$OUT/boot.bin"

"$AS" --32 "$ARCH_DIR/boot/loaderasm.s" -o "$OBJ/boot/loaderasm.o"
"$CC" $CFLAGS "$ARCH_DIR/boot/loadermain.c" -o "$OBJ/boot/loadermain.o"
"$LD" -T"$ARCH_DIR/loader_link.ld" -static -nostdlib --nmagic --oformat=binary -melf_i386 \
    "$OBJ/boot/loaderasm.o" "$OBJ/boot/loadermain.o" "$OBJ/$ARCH_DIR/drivers/hd.o" \
    "$OBJ/kernel/string.o" "$OBJ/kernel/elf.o" -o "$OUT/loader.bin"

# ---- user programs ----
UFLAGS="-c -Os -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -I./include -I./$ARCH_DIR/include"
"$CC" $UFLAGS user/hello.c -o "$OBJ/user/hello.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic "$OBJ/user/hello.o" -o "$OUT/hello.elf"
"$CC" $UFLAGS ${AUTO_DEMO:+-DAUTO_DEMO} user/shell.c -o "$OBJ/user/shell.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic "$OBJ/user/shell.o" -o "$OUT/shell.elf"
"$CC" $UFLAGS user/gfx.c -o "$OBJ/user/gfx.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic "$OBJ/user/gfx.o" -o "$OUT/gfx.elf"
"$CC" $UFLAGS user/postest.c -o "$OBJ/user/postest.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic "$OBJ/user/postest.o" -o "$OUT/postest.elf"
"$CC" $UFLAGS user/toyrun.c -o "$OBJ/user/toyrun.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic "$OBJ/user/toyrun.o" -o "$OUT/toyrun.elf"

# ---- libc + standard C demos ----
for f in user/libc/*.c; do
    o="$OBJ/user/libc/${f##*/}"
    o="${o%.c}.o"
    "$CC" -Iuser/libc/include $UFLAGS "$f" -o "$o"
done
"$AR" rcs "$BUILD/libc.a" "$OBJ"/user/libc/*.o
"$CC" -Iuser/libc/include $UFLAGS user/libctest.c -o "$OBJ/user/libctest.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic \
    "$OBJ/user/libc/crt0.o" "$OBJ/user/libctest.o" "$BUILD/libc.a" -o "$OUT/libctest.elf"
"$CC" -Iuser/libc/include $UFLAGS user/nettest.c -o "$OBJ/user/nettest.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic \
    "$OBJ/user/libc/crt0.o" "$OBJ/user/nettest.o" "$BUILD/libc.a" -o "$OUT/nettest.elf"
"$CC" -Iuser/libc/include $UFLAGS user/userctl.c -o "$OBJ/user/userctl.o"
"$LD" -T"$ARCH_DIR/user_link.ld" -static -nostdlib --nmagic \
    "$OBJ/user/libc/crt0.o" "$OBJ/user/userctl.o" "$BUILD/libc.a" -o "$OUT/userctl.elf"

# ---- musl Bash + single-binary Coreutils ----
BASH_BIN="${BASH_BIN:-I:/tools/posix/build/bash/bash}"
COREUTILS_BIN="${COREUTILS_BIN:-I:/tools/posix/build/coreutils/src/coreutils}"
COREUTILS_BUILD="${COREUTILS_BUILD:-I:/tools/posix/build/coreutils}"
[ -f "$BASH_BIN" ] || { echo "bash not found: $BASH_BIN"; exit 1; }
[ -f "$COREUTILS_BIN" ] || { echo "coreutils not found: $COREUTILS_BIN"; exit 1; }
cp "$BASH_BIN" "$OUT/bash.elf"
cp "$COREUTILS_BIN" "$OUT/coreutils.elf"
for f in "$OUT/bash.elf" "$OUT/coreutils.elf"; do
    perl -e 'open my $f, "+<", $ARGV[0] or die $!; binmode $f; seek $f, 7, 0; print $f chr(0xff)' "$f"
done

# ---- host tool: filesystem image ----
"$HOSTCC" -Os -I./include -I./$ARCH_DIR/include tools/mkfs.c -o "$BUILD/mkfs_kernel.exe"
mkfs_args=(
    "$OUT/loader.bin" loader.bin "$OUT/kernel.elf" kernel.elf
    "$OUT/hello.elf" hello.elf "$OUT/shell.elf" shell.elf
    "$OUT/gfx.elf" gfx.elf "$OUT/postest.elf" postest.elf
    "$OUT/libctest.elf" libctest.elf "$OUT/nettest.elf" nettest.elf
    "$OUT/userctl.elf" userctl.elf
    "$OUT/toyrun.elf" toyrun.elf "$OUT/bash.elf" bash.elf
    "$OUT/coreutils.elf" coreutils.elf ./README.md README
    ./user/posix-test.sh posix-test.sh ./user/users-test.sh users-test.sh
)
mkdir -p "$BUILD/posix-bin"
for name in $(awk -F'"' '/^SINGLE_BINARY_PROGRAM/ {print $2}' "$COREUTILS_BUILD/src/coreutils.h"); do
    wrapper="$BUILD/posix-bin/$name"
    entry="$name"
    if [ "$name" = ginstall ]; then entry=install; fi
    printf '#!/system/bin/coreutils --coreutils-prog-shebang=%s\n' "$name" > "$wrapper"
    mkfs_args+=("$wrapper" "$entry")
done
MKFS_OUT="$BUILD/kernel.img" ./"$BUILD/mkfs_kernel.exe" "${mkfs_args[@]}"

# ---- disk image ----
[ -f "$BUILD/80m.img" ] || dd if=/dev/zero of="$BUILD/80m.img" bs=1M count=80
dd if="$OUT/boot.bin"   of="$BUILD/80m.img" obs=512 count=1 conv=notrunc
dd if="$OUT/loader.bin" of="$BUILD/80m.img" obs=512 seek=1 conv=notrunc
dd if="$BUILD/kernel.img" of="$BUILD/80m.img" obs=512 seek=10 conv=notrunc

echo "---- build output ----"
ls -l "$OUT/boot.bin" "$OUT/loader.bin" "$OUT/kernel.elf" "$OUT/hello.elf" "$OUT/shell.elf" "$BUILD/kernel.img" "$BUILD/80m.img"
echo "note: loader.bin must stay <= 4608 bytes (9 sectors read by boot.s)"
