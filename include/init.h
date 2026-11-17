#ifndef INIT_H
#define INIT_H

#include "common.h"

#define DPL0 0
#define DPL3 3
#define GDT_ENTRY_NUM  64
#define GDT_TLS_FIRST  6
#define IDT_ENTRY_NUM  256

#define GDT_ENTRY_KERNEL_CS 1
#define GDT_ENTRY_KERNEL_DS 2
#define GDT_ENTRY_USER_CS   3
#define GDT_ENTRY_USER_DS   4
#define GDT_TSS_ENTRY       5

#define __KERNEL_CS   (GDT_ENTRY_KERNEL_CS*8)
#define __KERNEL_DS   (GDT_ENTRY_KERNEL_DS*8)
#define __USER_DS			(GDT_ENTRY_USER_DS*8 + 3)
#define __USER_CS			(GDT_ENTRY_USER_CS*8 + 3)

#define DEC_KERNEL_CS 0xc09a
#define DEC_KERNEL_DS 0xc092
#define DEC_USER_CS   0xc0fa
#define DEC_USER_DS   0xc0f2

// 定义IRQ
#define IRQ0 32 // 电脑系统计时器
#define IRQ1 33 // 键盘
#define IRQ2 34 // 与 IRQ9 相接，MPU−401 MD 使用
#define IRQ3 35 // 串口设备
#define IRQ4 36 // 串口设备
#define IRQ5 37 // 建议声卡使用
#define IRQ6 38 // 软驱传输控制使用
#define IRQ7 39 // 打印机传输控制使用
#define IRQ8 40 // 即时时钟
#define IRQ9 41 // 与 IRQ2 相接，可设定给其他硬件
#define IRQ10 42 // 建议网卡使用
#define IRQ11 43 // 建议 AGP 显卡使用
#define IRQ12 44 // 接 PS/2 鼠标，也可设定给其他硬件
#define IRQ13 45 // 协处理器使用
#define IRQ14 46 // IDE0 传输控制使用
#define IRQ15 47 // IDE1 传输控制使用

#define FRAME_SIZE sizeof(struct trap_frame)
// A struct describing a Task State Segment.
struct tss_entry_struct
{
    u32 prev_tss;   // The previous TSS - if we used hardware task switching this would form a linked list.
    u32 esp0;       // The stack pointer to load when we change to kernel mode.
    u32 ss0;        // The stack segment to load when we change to kernel mode.
    u32 esp1;       // Unused...
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax, ecx, edx, ebx; 
    u32 esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs;
    u32 ldt;        // Unused...
    u16 trap;
    u16 iomap_base;
} __PACKED;

struct trap_frame {
    u32 gs, fs, es, ds;
    u32 edi, esi, ebp, esp, ebx, edx, ecx, eax; // Pushed by pusha.
    u32 int_no, err_code;             // Interrupt number and error code (if applicable)
    u32 eip, cs, eflags, useresp, ss; // Pushed by the processor automatically.
};

enum {
    GATE_INTERRUPT = 0xE,
    GATE_TRAP = 0xF,
    GATE_CALL = 0xC,
    GATE_TASK = 0x5,
};

struct gdt_desc_struct {
    u16 limit0; 
    u16 base0;
    u16 base1 : 8, type : 4, s : 1, dpl : 2, p : 1;
    u16 limit1 : 4, avl : 1, zero : 1, db : 1, g : 1, base2 : 8;
} __PACKED;

struct idt_desc_struct {
    u16 addr0;
    u16 segment;
    u16 reserved : 5, zero0 : 3, type : 4, zero1 : 1, dpl : 2, p : 1;
    u16 addr1;
} __PACKED;

// A struct describing a pointer to an array of interrupt handlers.
// This is in a format suitable for giving to 'lidt'.
struct desc_ptr_struct
{
    u16 limit;
    u32 base;                // The address of the first element in our idt_entry_t array.
} __PACKED;

struct cpu_struct {
    struct gdt_desc_struct gdt[GDT_ENTRY_NUM];
    struct desc_ptr_struct  gdt_ptr;
    struct tss_entry_struct tss;
    struct idt_desc_struct idt[IDT_ENTRY_NUM];
    struct desc_ptr_struct  idt_ptr;
};

typedef void (*interrupt_handler_t)(struct trap_frame *frame);

extern struct cpu_struct this_cpu;
// These extern directives let us access the addresses of our ASM ISR handlers.
extern void gdt_flush(u32);
extern void idt_flush(u32);
extern void tss_flush();
extern void int_ret_stub(void);

void irq_enable(u8 irq);
void init_desc(void);
void register_interrupt_handler(u8 n, interrupt_handler_t h);
void int_handler(struct trap_frame *frame);
void set_kernel_stack(u32 esp);

#endif
