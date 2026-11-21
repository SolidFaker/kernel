#ifndef INIT_H
#define INIT_H
#include "common.h"

#define GDT_ENTRY_NUM 16
#define GDT_TLS_FIRST 6
#define IDT_ENTRY_NUM 256
#define __KERNEL_CS 0
#define __KERNEL_DS 0
#define __USER_CS 3
#define __USER_DS 3
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
    reg_t x[31];
    reg_t pad;
    reg_t vector[64];
};

struct tss_entry_struct { u64 dummy; };
struct gdt_desc_struct { u64 dummy; };
struct idt_desc_struct { u64 dummy; };
struct desc_ptr_struct { u64 dummy; };
struct cpu_struct { u64 dummy; };

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
void aarch64_irq_enable_irq(u32 irq);
#endif
