#ifndef INIT_H
#define INIT_H
#include "common.h"

#define GDT_ENTRY_NUM 16
#define GDT_TLS_FIRST 6
#define IDT_ENTRY_NUM 256
#define __KERNEL_CS 0x08
#define __KERNEL_DS 0x10
#define __USER_DS 0x1b
#define __USER_CS 0x23
#define IRQ0 32
#define IRQ1 33
#define FRAME_SIZE sizeof(struct trap_frame)

struct trap_frame {
    reg_t gs, fs, es, ds;
    reg_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    reg_t int_no, err_code;
    reg_t eip, cs, eflags, useresp, ss;
    reg_t r6, r7, r8, r9, r10, r11, r12, user_lr;
    reg_t r13, r14, r15;
    reg_t raw[16];
};

struct tss_entry_struct {
    u32 reserved0;
    u64 rsp0, rsp1, rsp2;
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __PACKED;

struct gdt_desc_struct {
    u16 limit0, base0;
    u16 base1 : 8, type : 4, s : 1, dpl : 2, p : 1;
    u16 limit1 : 4, avl : 1, zero : 1, db : 1, g : 1, base2 : 8;
} __PACKED;

struct idt_desc_struct {
    u16 addr0;
    u16 segment;
    u8 ist;
    u8 type : 4, zero0 : 1, dpl : 2, p : 1;
    u16 addr1;
    u32 addr2;
    u32 reserved;
} __PACKED;

struct desc_ptr_struct {
    u16 limit;
    u64 base;
} __PACKED;

struct cpu_struct {
    struct gdt_desc_struct gdt[GDT_ENTRY_NUM];
    struct desc_ptr_struct gdt_ptr;
    struct tss_entry_struct tss;
    struct idt_desc_struct idt[IDT_ENTRY_NUM];
    struct desc_ptr_struct idt_ptr;
};

typedef void (*interrupt_handler_t)(struct trap_frame *frame);
extern struct cpu_struct this_cpu;
extern uaddr kernel_stack_top;
extern void int_ret_stub(void);
void irq_enable(u8 irq);
void init_desc(void);
void register_interrupt_handler(u8 n, interrupt_handler_t h);
void int_handler(struct trap_frame *frame);
void set_kernel_stack(uaddr esp);
void arch_init_stack(void);
#endif
