#!/bin/bash
set -e

ARCH="${1:?usage: bash build-arch.sh aarch64|x86_64}"
ZIG="${ZIG:-I:/tools/zig/zig.exe}"
CROSS="${CROSS:-I:/tools/i686-elf/bin/i686-elf-}"
HOSTCC="${HOSTCC:-I:/tools/mingw64/bin/gcc.exe}"
ROOT="build/$ARCH"
OUT="$ROOT/bin"
OBJ="$ROOT/obj"

rm -rf "$ROOT"
mkdir -p "$OUT" "$OBJ"

build_native_user() {
    local arch="$1" target extra rt=""
    case "$arch" in
    aarch64)
        target=aarch64-freestanding-none
        extra='-mcpu=cortex_a53 -mgeneral-regs-only'
        rt=/i/tools/posix/build/aarch64-compiler_rt/libcompiler_rt.a
        ;;
    x86_64)
        target=x86_64-freestanding-none
        extra='-mno-red-zone'
        rt=/i/tools/posix/build/x86_64-compiler_rt/libcompiler_rt.a
        ;;
    esac
    local ncflags="--target=$target $extra -std=gnu99 -O2 -fno-sanitize=undefined -ffreestanding -fno-stack-protector -fno-pic -Wall -Iinclude -Iarch/$arch/include"
    local libcflags="-Iuser/libc/include $ncflags"
    mkdir -p "$OBJ/native"
    "$ZIG" cc $ncflags -c user/postest.c -o "$OBJ/native/postest.o"
    "$ZIG" cc $ncflags -nostdlib -Wl,-T,arch/$arch/user_link.ld \
        "$OBJ/native/postest.o" -o "$OUT/postest.elf"
    local libc_objs=""
    for f in $(find user/libc -maxdepth 1 -name '*.c' | sort); do
        local o="$OBJ/native/${f//\//_}.o"
        "$ZIG" cc $libcflags -c "$f" -o "$o"
        libc_objs="$libc_objs $o"
    done
    "$ZIG" cc $libcflags -c user/libctest.c -o "$OBJ/native/libctest.o"
    "$ZIG" cc --target=$target $extra -c arch/$arch/user/crt0.s -o "$OBJ/native/crt0-entry.o"
    "$ZIG" cc --target=$target $extra -nostdlib -Wl,-T,arch/$arch/user_link.ld \
        "$OBJ/native/crt0-entry.o" "$OBJ/native/libctest.o" $libc_objs "$rt" \
        -o "$OUT/libctest.elf"
    "$ZIG" cc $libcflags -c user/userctl.c -o "$OBJ/native/userctl.o"
    "$ZIG" cc --target=$target $extra -nostdlib -Wl,-T,arch/$arch/user_link.ld \
        "$OBJ/native/crt0-entry.o" "$OBJ/native/userctl.o" $libc_objs "$rt" \
        -o "$OUT/userctl.elf"
}

case "$ARCH" in
aarch64)
    TARGET=aarch64-freestanding-none
    CPU=cortex_a53
    EXTRA=
    AARCH64_GENERAL_REGS='-mgeneral-regs-only -mno-unaligned-access -fno-vectorize -fno-slp-vectorize'
    NAME=AArch64
    CFLAGS="--target=$TARGET -mcpu=$CPU $EXTRA ${AARCH64_GENERAL_REGS:-} -std=gnu99 -O2 -fno-sanitize=undefined -ffreestanding -fno-stack-protector -fno-pic -Wall -Werror -Iinclude -Iarch/$ARCH/include ${EXTRA_CFLAGS:-} -DARCH_NAME=\"$NAME\""
    OBJS=""
    for f in $(find drivers fs kernel mm arch/aarch64/kernel arch/aarch64/mm arch/aarch64/drivers -name '*.c' | sort); do
        if [ "$f" = kernel/arch_entry.c ]; then continue; fi
        o="$OBJ/${f//\//_}.o"
        "$ZIG" cc $CFLAGS -c "$f" -o "$o"
        OBJS="$OBJS $o"
    done
    for f in $(find arch/aarch64/kernel -name '*.s' | sort); do
        o="$OBJ/${f//\//_}.o"
        "$ZIG" cc --target=$TARGET -mcpu=$CPU $EXTRA -c "$f" -o "$o"
        OBJS="$OBJS $o"
    done
    "$ZIG" cc --target=$TARGET -mcpu=$CPU $EXTRA -nostdlib "-Wl,-T,arch/$ARCH/kernel_link.ld" \
        $OBJS -o "$OUT/kernel.elf"
    build_native_user aarch64
    BASH_BIN="${BASH_BIN:-I:/tools/posix/build/aarch64-bash-native/bash}"
    COREUTILS_BIN="${COREUTILS_BIN:-I:/tools/posix/build/aarch64-coreutils-native/src/coreutils}"
    COREUTILS_BUILD="${COREUTILS_BUILD:-I:/tools/posix/build/aarch64-coreutils-native}"
    cp "$BASH_BIN" "$OUT/bash.elf"
    cp "$COREUTILS_BIN" "$OUT/coreutils.elf"
    for f in "$OUT/bash.elf" "$OUT/coreutils.elf"; do
        perl -e 'open my $f, "+<", $ARGV[0] or die $!; binmode $f; seek $f, 7, 0; print $f chr(0xff)' "$f"
    done
    "$HOSTCC" -Os -Iinclude -Iarch/aarch64/include tools/mkfs.c -o "$ROOT/mkfs.exe"
    mkfs_args=("$OUT/kernel.elf" kernel.elf "$OUT/bash.elf" bash.elf
        "$OUT/coreutils.elf" coreutils.elf "$OUT/postest.elf" postest.elf
        "$OUT/libctest.elf" libctest.elf "$OUT/userctl.elf" userctl.elf ./README.md README
        ./user/posix-test.sh posix-test.sh ./user/users-test.sh users-test.sh)
    mkdir -p "$ROOT/posix-bin"
    for name in $(awk -F'"' '/^SINGLE_BINARY_PROGRAM/ {print $2}' "$COREUTILS_BUILD/src/coreutils.h"); do
        wrapper="$ROOT/posix-bin/$name"
        entry="$name"
        if [ "$name" = ginstall ]; then entry=install; fi
        printf '#!/system/bin/coreutils --coreutils-prog-shebang=%s\n' "$name" > "$wrapper"
        mkfs_args+=("$wrapper" "$entry")
    done
    MKFS_OUT="$ROOT/kernel.img" "$ROOT/mkfs.exe" "${mkfs_args[@]}"
    ;;

x86_64)
    CFLAGS="--target=x86_64-freestanding-none -std=gnu99 -O2 -fno-sanitize=undefined -ffreestanding -fno-stack-protector -fno-pic -mno-red-zone -mno-sse -mno-sse2 -mno-mmx -mcmodel=large -Wall -Werror -Iinclude -Iarch/x86_64/include -Iarch/x86/include ${EXTRA_CFLAGS:-} -DARCH_NAME=\"x86_64\""
    OBJS=""
    for f in $(find drivers fs kernel mm arch/x86_64/kernel arch/x86_64/mm arch/x86_64/drivers -name '*.c' | sort); do
        if [ "$f" = kernel/arch_entry.c ]; then continue; fi
        if [ "$f" = arch/x86_64/drivers/keyboard.c ]; then continue; fi
        if [ "$f" = arch/x86_64/drivers/display.c ]; then continue; fi
        o="$OBJ/${f//\//_}.o"
        "$ZIG" cc $CFLAGS -c "$f" -o "$o"
        OBJS="$OBJS $o"
    done
    for f in arch/x86/drivers/hd.c arch/x86/drivers/timer.c arch/x86/drivers/display.c arch/x86/drivers/fbcon.c arch/x86/drivers/keyboard.c; do
        o="$OBJ/${f//\//_}.o"
        "$ZIG" cc $CFLAGS -c "$f" -o "$o"
        OBJS="$OBJS $o"
    done
    for f in $(find arch/x86_64/kernel -name '*.s' | sort); do
        o="$OBJ/${f//\//_}.o"
        "$ZIG" cc --target=x86_64-freestanding-none -c "$f" -o "$o"
        OBJS="$OBJS $o"
    done
    "$ZIG" cc --target=x86_64-freestanding-none -nostdlib -Wl,-T,arch/x86_64/kernel_link.ld \
        $OBJS -o "$OUT/kernel.elf"
    "$ZIG" objcopy -O binary "$OUT/kernel.elf" "$OUT/kernel.bin"
    build_native_user x86_64

    mkdir -p "$OBJ/boot" "$OBJ/drivers"
    "$CROSS"as --32 arch/x86_64/boot/boot.s -o "$OBJ/boot/boot.o"
    "$CROSS"ld -Ttext 0x7c00 --oformat=binary "$OBJ/boot/boot.o" -o "$OUT/boot.bin"
    "$CROSS"as --32 arch/x86_64/boot/loaderasm.s -o "$OBJ/boot/loaderasm.o"
    "$CROSS"as --32 arch/x86_64/boot/longmode.s -o "$OBJ/boot/longmode.o"
    LCFLAGS="-c -Os -ffreestanding -fno-stack-protector -Wall -Werror -Iinclude -Iarch/x86/include"
    "$CROSS"gcc $LCFLAGS arch/x86_64/boot/loadermain.c -o "$OBJ/boot/loadermain.o"
    "$CROSS"gcc $LCFLAGS arch/x86/drivers/hd.c -o "$OBJ/drivers/hd.o"
    "$CROSS"gcc $LCFLAGS kernel/string.c -o "$OBJ/string.o"
    "$CROSS"gcc $LCFLAGS kernel/elf.c -o "$OBJ/elf.o"
    "$CROSS"ld -Tarch/x86_64/loader_link.ld -static -nostdlib --nmagic --oformat=binary -melf_i386 \
        "$OBJ/boot/loaderasm.o" "$OBJ/boot/loadermain.o" "$OBJ/boot/longmode.o" \
        "$OBJ/drivers/hd.o" "$OBJ/string.o" "$OBJ/elf.o" -o "$OUT/loader.bin"
    "$HOSTCC" -Os -Iinclude -Iarch/x86/include tools/mkfs.c -o "$ROOT/mkfs.exe"
    BASH_BIN="${BASH_BIN:-I:/tools/posix/build/x86_64-bash-native/bash}"
    COREUTILS_BIN="${COREUTILS_BIN:-I:/tools/posix/build/x86_64-coreutils-native/src/coreutils}"
    COREUTILS_BUILD="${COREUTILS_BUILD:-I:/tools/posix/build/x86_64-coreutils-native}"
    cp "$BASH_BIN" "$OUT/bash.elf"
    cp "$COREUTILS_BIN" "$OUT/coreutils.elf"
    for f in "$OUT/bash.elf" "$OUT/coreutils.elf"; do
        perl -e 'open my $f, "+<", $ARGV[0] or die $!; binmode $f; seek $f, 7, 0; print $f chr(0xff)' "$f"
    done
    mkfs_args=("$OUT/loader.bin" loader.bin "$OUT/kernel.bin" kernel.bin
        "$OUT/bash.elf" bash.elf "$OUT/coreutils.elf" coreutils.elf
        "$OUT/postest.elf" postest.elf "$OUT/libctest.elf" libctest.elf
        "$OUT/userctl.elf" userctl.elf
        ./README.md README ./user/posix-test.sh posix-test.sh \
        ./user/users-test.sh users-test.sh)
    mkdir -p "$ROOT/posix-bin"
    for name in $(awk -F'"' '/^SINGLE_BINARY_PROGRAM/ {print $2}' "$COREUTILS_BUILD/src/coreutils.h"); do
        wrapper="$ROOT/posix-bin/$name"
        entry="$name"
        if [ "$name" = ginstall ]; then entry=install; fi
        printf '#!/system/bin/coreutils --coreutils-prog-shebang=%s\n' "$name" > "$wrapper"
        mkfs_args+=("$wrapper" "$entry")
    done
    MKFS_OUT="$ROOT/kernel.img" "$ROOT/mkfs.exe" "${mkfs_args[@]}"
    dd if=/dev/zero of="$ROOT/80m.img" bs=1M count=80
    dd if="$OUT/boot.bin" of="$ROOT/80m.img" obs=512 count=1 conv=notrunc
    dd if="$OUT/loader.bin" of="$ROOT/80m.img" obs=512 seek=1 conv=notrunc
    dd if="$ROOT/kernel.img" of="$ROOT/80m.img" obs=512 seek=10 conv=notrunc
    ;;
*)
    echo "unknown architecture: $ARCH" >&2
    exit 1
    ;;
esac

echo "built $OUT/kernel.elf"
