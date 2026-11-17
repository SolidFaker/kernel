AS = i686-elf-as
AR = i686-elf-ar
CC = i686-elf-gcc
LD = i686-elf-ld
HOSTCC ?= cc

INCLUDE = -I./include

DISKIMG = ./80m.img
OUTDIR = bin

EXCLUDE = -not -path "./boot/*" -not -path "./tools/*" -not -path "./user/*"
KNL_CSRC = $(shell find . -name "*.c" $(EXCLUDE))
KNL_SSRC = $(shell find . -name "*.s" $(EXCLUDE))

OBJS = $(shell find . -name "*.o")
KNL_COBJ = $(patsubst %.c, %.o, $(KNL_CSRC))
KNL_SOBJ = $(patsubst %.s, %.o, $(KNL_SSRC))

KNL_LD = tools/kernel_link.ld
BTL_LD = tools/loader_link.ld

GCFLAGS = -c -g -Os -m32 -ffreestanding -Wall -Werror -fno-pie
GCFLAGS += $(INCLUDE) -fno-stack-protector
UFLAGS  = -c -Os -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall $(INCLUDE)
LIBCFLAGS = -c -Os -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -Iuser/libc/include $(INCLUDE)
LIBC_CSRC = $(wildcard user/libc/*.c)
LIBC_OBJ = $(LIBC_CSRC:.c=.o)
MUSL_DIR ?= /tmp/musl-install
MUSL_LIBGCC := $(shell $(CC) -print-libgcc-file-name)
TOYBOX_BIN ?= /tmp/toybox-master/toybox
ASFLAGS = --32
MAPFLAGS = -Map kernel.map
KNL_LDFLAGS = -static -nostdlib --nmagic -melf_i386
BTL_LDFLAGS = -static -nostdlib --nmagic --oformat=binary -melf_i386

BTL_OBJ = ./boot/loaderasm.o ./boot/loadermain.o ./drivers/hd.o
BTL_OBJ += ./kernel/string.o ./kernel/elf.o

# `all` starts with `clean`, so never run make with -j here
.PHONY: all clean mkfs test
.NOTPARALLEL:

# QEMU boots the raw disk image; override with `make test QEMU=...`
QEMU ?= qemu-system-i386
QEMUFLAGS ?= -m 64 -drive file=$(DISKIMG),format=raw,if=ide -nic user,model=e1000 -no-reboot

# build everything and write the disk image; boot it with `make test`
all: clean $(OUTDIR)/boot.bin $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf dd

clean:
	rm -rf $(OUTDIR)/*
	rm -rf $(OBJS)
	rm -rf kernel.img libc.a
	mkdir -p $(OUTDIR)

$(OUTDIR)/kernel.elf: $(KNL_COBJ) $(KNL_SOBJ)
	$(LD) -T$(KNL_LD) $(KNL_LDFLAGS) $(KNL_COBJ) $(KNL_SOBJ) -o $(OUTDIR)/kernel.elf $(MAPFLAGS)

%.o: %.c
	$(CC) $(GCFLAGS) -c $< -o $@

$(OUTDIR)/loader.bin: $(BTL_OBJ)
	$(LD) -T$(BTL_LD) $(BTL_LDFLAGS) $(BTL_OBJ) -o $(OUTDIR)/loader.bin

$(OUTDIR)/boot.bin: ./boot/boot.o
	$(LD) -Ttext 0x7c00 --oformat=binary ./boot/boot.o -o $(OUTDIR)/boot.bin

mkfs_kernel: tools/mkfs.c include/fs/myfs.h include/common.h
	$(HOSTCC) $(INCLUDE) -Os tools/mkfs.c -o mkfs_kernel

mkfs: mkfs_kernel

user/hello.o: user/hello.c
	$(CC) $(UFLAGS) -c $< -o $@

user/hello.elf: user/hello.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 user/hello.o -o user/hello.elf

user/shell.o: user/shell.c
	$(CC) $(UFLAGS) -c $< -o $@

user/shell.elf: user/shell.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 user/shell.o -o user/shell.elf

user/gfx.o: user/gfx.c
	$(CC) $(UFLAGS) -c $< -o $@

user/gfx.elf: user/gfx.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 user/gfx.o -o user/gfx.elf

user/postest.o: user/postest.c
	$(CC) $(UFLAGS) -c $< -o $@

user/postest.elf: user/postest.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 user/postest.o -o user/postest.elf

$(LIBC_OBJ): user/libc/%.o: user/libc/%.c
	$(CC) $(LIBCFLAGS) -c $< -o $@

libc.a: $(LIBC_OBJ)
	$(AR) rcs $@ $^

user/libctest.o: user/libctest.c
	$(CC) $(LIBCFLAGS) -c $< -o $@

user/libctest.elf: user/libctest.o user/libc/crt0.o libc.a tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 \
		user/libc/crt0.o user/libctest.o libc.a -o user/libctest.elf

user/nettest.o: user/nettest.c
	$(CC) $(LIBCFLAGS) -c $< -o $@

user/nettest.elf: user/nettest.o user/libc/crt0.o libc.a tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 \
		user/libc/crt0.o user/nettest.o libc.a -o user/nettest.elf

user/hello_musl.elf: user/hello_musl.c $(MUSL_DIR)/lib/libc.a
	$(CC) -static -nostdlib -fno-pie -no-pie -I$(MUSL_DIR)/include $< \
		$(MUSL_DIR)/lib/crt1.o $(MUSL_DIR)/lib/libc.a $(MUSL_LIBGCC) -o $@
	python3 -c "p='$@';d=bytearray(open(p,'rb').read());d[7]=0xff;open(p,'wb').write(d)"

user/toybox.elf: $(TOYBOX_BIN)
	cp $(TOYBOX_BIN) $@
	chmod +w $@
	python3 -c "p='$@';d=bytearray(open(p,'rb').read());d[7]=0xff;open(p,'wb').write(d)"

user/toyrun.o: user/toyrun.c
	$(CC) $(UFLAGS) -c $< -o $@

user/toyrun.elf: user/toyrun.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 \
		user/toyrun.o -o user/toyrun.elf

kernel.img: mkfs_kernel $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf user/hello.elf user/shell.elf user/gfx.elf user/postest.elf user/libctest.elf user/nettest.elf user/hello_musl.elf user/toybox.elf user/toyrun.elf
	./mkfs_kernel $(OUTDIR)/loader.bin loader.bin $(OUTDIR)/kernel.elf kernel.elf user/hello.elf hello.elf user/shell.elf shell.elf user/gfx.elf gfx.elf user/postest.elf postest.elf user/libctest.elf libctest.elf user/nettest.elf nettest.elf user/hello_musl.elf hello_musl.elf user/toybox.elf toybox.elf user/toyrun.elf toyrun.elf ./README.md README

# create the target disk on a fresh clone
$(DISKIMG):
	dd if=/dev/zero of=$(DISKIMG) bs=1M count=80

dd: $(OUTDIR)/boot.bin $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf kernel.img $(DISKIMG)
	dd if=./$(OUTDIR)/boot.bin of=$(DISKIMG) obs=512 count=1 conv=notrunc
	dd if=./$(OUTDIR)/loader.bin of=$(DISKIMG) obs=512 seek=1 conv=notrunc
	dd if=./kernel.img of=$(DISKIMG) obs=512 seek=10 conv=notrunc

test: $(DISKIMG)
	$(QEMU) $(QEMUFLAGS)
