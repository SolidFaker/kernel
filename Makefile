AS = as
CC = gcc
LD = ld

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
UFLAGS  = -c -Os -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall
ASFLAGS = --32
MAPFLAGS = -Map kernel.map
KNL_LDFLAGS = -static -nostdlib --nmagic -melf_i386
BTL_LDFLAGS = -static -nostdlib --nmagic --oformat=binary -melf_i386

BTL_OBJ = ./boot/loaderasm.o ./boot/loadermain.o ./drivers/hd.o
BTL_OBJ += ./kernel/string.o ./kernel/elf.o

# `all` starts with `clean`, so never run make with -j here
.PHONY: all clean mkfs floppy test-win
.NOTPARALLEL:

# Windows bochs (see bochsrc.windows); used by `make test-win`
BOCHS_WIN ?= I:/tools/bochs/bochs.exe

# build everything and write the disk image; boot it with `make test`
all: clean $(OUTDIR)/boot.bin $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf dd

clean:
	rm -rf $(OUTDIR)/*
	rm -rf $(OBJS)
	rm -rf kernel.img

$(OUTDIR)/kernel.elf: $(KNL_COBJ) $(KNL_SOBJ)
	$(LD) -T$(KNL_LD) $(KNL_LDFLAGS) $(KNL_COBJ) $(KNL_SOBJ) -o $(OUTDIR)/kernel.elf $(MAPFLAGS)

$(OUTDIR)/loader.bin: $(BTL_OBJ)
	$(LD) -T$(BTL_LD) $(BTL_LDFLAGS) $(BTL_OBJ) -o $(OUTDIR)/loader.bin

$(OUTDIR)/boot.bin: ./boot/boot.o
	$(LD) -Ttext 0x7c00 --oformat=binary ./boot/boot.o -o $(OUTDIR)/boot.bin

mkfs_kernel: tools/mkfs.c include/fs/myfs.h include/common.h
	$(CC) $(INCLUDE) -m32 -Os tools/mkfs.c -o mkfs_kernel

mkfs: mkfs_kernel

user/hello.o: user/hello.c
	$(CC) $(UFLAGS) -c $< -o $@

user/hello.elf: user/hello.o tools/user_link.ld
	$(LD) -Ttools/user_link.ld -static -nostdlib --nmagic -melf_i386 user/hello.o -o user/hello.elf

kernel.img: mkfs_kernel $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf user/hello.elf
	./mkfs_kernel $(OUTDIR)/loader.bin loader.bin $(OUTDIR)/kernel.elf kernel.elf user/hello.elf hello.elf ./README.md README

# create the target disk on a fresh clone
$(DISKIMG):
	dd if=/dev/zero of=$(DISKIMG) bs=1M count=80

dd: $(OUTDIR)/boot.bin $(OUTDIR)/loader.bin $(OUTDIR)/kernel.elf kernel.img $(DISKIMG)
	dd if=./$(OUTDIR)/boot.bin of=$(DISKIMG) obs=512 count=1 conv=notrunc
	dd if=./$(OUTDIR)/loader.bin of=$(DISKIMG) obs=512 seek=1 conv=notrunc
	dd if=./kernel.img of=$(DISKIMG) obs=512 seek=10 conv=notrunc

test:
	bochs -f bochsrc

test-win:
	$(BOCHS_WIN) -q -f bochsrc.windows

floppy:
	dd if=/dev/zero of=./floppy.img bs=512 count=2880
