AS = i686-elf-as
AR = i686-elf-ar
CC = i686-elf-gcc
LD = i686-elf-ld
HOSTCC ?= cc
PYTHON ?= python3

INCLUDE = -I./include
BUILD   = build
OBJ     = $(BUILD)/obj
BIN     = $(BUILD)/bin

DISKIMG    = $(BUILD)/80m.img
KERNEL_IMG = $(BUILD)/kernel.img
KERNEL_ELF = $(BIN)/kernel.elf
LOADER_BIN = $(BIN)/loader.bin
BOOT_BIN   = $(BIN)/boot.bin
LIBC_A     = $(BUILD)/libc.a
MKFS       = $(BUILD)/mkfs_kernel

KNL_CSRC = $(shell find drivers fs init kernel mm -name "*.c")
KNL_SSRC = $(shell find kernel -name "*.s")
KNL_COBJ = $(patsubst %.c, $(OBJ)/%.o, $(KNL_CSRC))
KNL_SOBJ = $(patsubst %.s, $(OBJ)/%.o, $(KNL_SSRC))

KNL_LD = tools/kernel_link.ld
BTL_LD = tools/loader_link.ld
USER_LD = tools/user_link.ld

GCFLAGS = -c -g -Os -m32 -ffreestanding -Wall -Werror -fno-pie
GCFLAGS += $(INCLUDE) -fno-stack-protector
UFLAGS  = -c -Os -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall $(INCLUDE)
LIBCFLAGS = -c -Os -m32 -ffreestanding -fno-pie -fno-stack-protector -fno-builtin -Wall -Iuser/libc/include $(INCLUDE)
LIBC_CSRC = $(wildcard user/libc/*.c)
LIBC_OBJ = $(patsubst user/libc/%.c, $(OBJ)/user/libc/%.o, $(LIBC_CSRC))
MUSL_DIR ?= /tmp/musl-install
MUSL_LIBGCC := $(shell $(CC) -print-libgcc-file-name)
TOYBOX_BIN ?= /tmp/toybox-master/toybox
ASFLAGS = --32
MAPFLAGS = -Map $(BUILD)/kernel.map
KNL_LDFLAGS = -static -nostdlib --nmagic -melf_i386
BTL_LDFLAGS = -static -nostdlib --nmagic --oformat=binary -melf_i386
USER_LDFLAGS = -static -nostdlib --nmagic -melf_i386

BTL_OBJ = $(OBJ)/boot/loaderasm.o $(OBJ)/boot/loadermain.o $(OBJ)/drivers/hd.o
BTL_OBJ += $(OBJ)/kernel/string.o $(OBJ)/kernel/elf.o

USER_SIMPLE = hello shell gfx postest toyrun
USER_OBJ = $(addprefix $(OBJ)/user/, $(addsuffix .o, $(USER_SIMPLE)))
USER_ELFS = $(addprefix $(BIN)/, $(addsuffix .elf, $(USER_SIMPLE)))
USER_ELFS += $(BIN)/libctest.elf $(BIN)/nettest.elf
USER_ELFS += $(BIN)/hello_musl.elf $(BIN)/toybox.elf

# `all` starts with `clean`, so never run make with -j here
.PHONY: all clean mkfs test test64
.NOTPARALLEL:

# QEMU boots the raw disk image; override with `make test QEMU=...`
QEMU ?= qemu-system-i386
QEMU64 ?= qemu-system-x86_64
QEMUFLAGS ?= -m 64 -drive file=$(DISKIMG),format=raw,if=ide -nic user,model=e1000 -no-reboot

all: clean $(BOOT_BIN) $(LOADER_BIN) $(KERNEL_ELF) $(USER_ELFS) dd

clean:
	rm -rf $(BUILD)
	rm -f kernel.img mkfs_kernel libc.a kernel.map 80m.img floppy.img
	rm -rf bin
	rm -f boot/*.o drivers/*.o fs/*.o init/*.o kernel/*.o mm/*.o
	rm -f user/*.o user/libc/*.o user/*.elf
	mkdir -p $(OBJ) $(BIN)

$(OBJ)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(GCFLAGS) -c $< -o $@

$(OBJ)/%.o: %.s
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL_ELF): $(KNL_COBJ) $(KNL_SOBJ)
	@mkdir -p $(@D)
	$(LD) -T$(KNL_LD) $(KNL_LDFLAGS) $(KNL_COBJ) $(KNL_SOBJ) -o $@ $(MAPFLAGS)

$(LOADER_BIN): $(BTL_OBJ)
	@mkdir -p $(@D)
	$(LD) -T$(BTL_LD) $(BTL_LDFLAGS) $(BTL_OBJ) -o $@

$(BOOT_BIN): $(OBJ)/boot/boot.o
	@mkdir -p $(@D)
	$(LD) -Ttext 0x7c00 --oformat=binary $< -o $@

$(MKFS): tools/mkfs.c include/fs/myfs.h include/common.h
	@mkdir -p $(@D)
	$(HOSTCC) $(INCLUDE) -Os tools/mkfs.c -o $@

mkfs: $(MKFS)

$(LIBC_OBJ): $(OBJ)/user/libc/%.o: user/libc/%.c
	@mkdir -p $(@D)
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(LIBC_A): $(LIBC_OBJ)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

$(USER_OBJ): $(OBJ)/user/%.o: user/%.c
	@mkdir -p $(@D)
	$(CC) $(UFLAGS) -c $< -o $@

$(OBJ)/user/libctest.o: user/libctest.c
	@mkdir -p $(@D)
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(OBJ)/user/nettest.o: user/nettest.c
	@mkdir -p $(@D)
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(BIN)/%.elf: $(OBJ)/user/%.o $(USER_LD)
	@mkdir -p $(@D)
	$(LD) -T$(USER_LD) $(USER_LDFLAGS) $< -o $@

$(BIN)/libctest.elf: $(OBJ)/user/libctest.o $(OBJ)/user/libc/crt0.o $(LIBC_A) $(USER_LD)
	@mkdir -p $(@D)
	$(LD) -T$(USER_LD) $(USER_LDFLAGS) \
		$(OBJ)/user/libc/crt0.o $(OBJ)/user/libctest.o $(LIBC_A) -o $@

$(BIN)/nettest.elf: $(OBJ)/user/nettest.o $(OBJ)/user/libc/crt0.o $(LIBC_A) $(USER_LD)
	@mkdir -p $(@D)
	$(LD) -T$(USER_LD) $(USER_LDFLAGS) \
		$(OBJ)/user/libc/crt0.o $(OBJ)/user/nettest.o $(LIBC_A) -o $@

$(BIN)/hello_musl.elf: user/hello_musl.c $(MUSL_DIR)/lib/libc.a
	@mkdir -p $(@D)
	$(CC) -static -nostdlib -fno-pie -no-pie -I$(MUSL_DIR)/include $< \
		$(MUSL_DIR)/lib/crt1.o $(MUSL_DIR)/lib/libc.a $(MUSL_LIBGCC) -o $@
	$(PYTHON) -c "p='$@';d=bytearray(open(p,'rb').read());d[7]=0xff;open(p,'wb').write(d)"

$(BIN)/toybox.elf: $(TOYBOX_BIN)
	@mkdir -p $(@D)
	cp $(TOYBOX_BIN) $@
	chmod +w $@
	$(PYTHON) -c "p='$@';d=bytearray(open(p,'rb').read());d[7]=0xff;open(p,'wb').write(d)"

$(KERNEL_IMG): $(MKFS) $(LOADER_BIN) $(KERNEL_ELF) $(USER_ELFS)
	MKFS_OUT=$(KERNEL_IMG) $(MKFS) \
		$(LOADER_BIN) loader.bin $(KERNEL_ELF) kernel.elf \
		$(BIN)/hello.elf hello.elf $(BIN)/shell.elf shell.elf \
		$(BIN)/gfx.elf gfx.elf $(BIN)/postest.elf postest.elf \
		$(BIN)/libctest.elf libctest.elf $(BIN)/nettest.elf nettest.elf \
		$(BIN)/hello_musl.elf hello_musl.elf $(BIN)/toybox.elf toybox.elf \
		$(BIN)/toyrun.elf toyrun.elf ./README.md README

# create the target disk on a fresh clone
$(DISKIMG):
	@mkdir -p $(@D)
	dd if=/dev/zero of=$@ bs=1M count=80

dd: $(BOOT_BIN) $(LOADER_BIN) $(KERNEL_ELF) $(KERNEL_IMG) $(DISKIMG)
	dd if=$(BOOT_BIN) of=$(DISKIMG) obs=512 count=1 conv=notrunc
	dd if=$(LOADER_BIN) of=$(DISKIMG) obs=512 seek=1 conv=notrunc
	dd if=$(KERNEL_IMG) of=$(DISKIMG) obs=512 seek=10 conv=notrunc

test: $(DISKIMG)
	$(QEMU) $(QEMUFLAGS)

test64: $(DISKIMG)
	$(QEMU64) $(QEMUFLAGS)
