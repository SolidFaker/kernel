#include "common.h"
#include "init.h"
#include "signal.h"
#include "syscall.h"
#include "string.h"
#include "tools.h"
#include "page.h"
#include "task.h"

struct cpu_struct this_cpu;
interrupt_handler_t interrupt_handlers[256];
uaddr kernel_stack_top;

extern void isr_bad(void);
extern void isr_page_fault(void);
extern void irq_timer(void);
extern void irq_keyboard(void);
extern void isr_ud(void);
extern void isr_gp(void);
extern void syscall_entry(void);

static void wrmsr(u32 msr, u64 value)
{
    asm volatile("wrmsr" :: "c"(msr), "a"((u32)value), "d"((u32)(value >> 32))
                 : "memory");
}

static u64 rdmsr(u32 msr)
{
    u32 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return low | ((u64)high << 32);
}

static void set_idt(u8 n, void (*handler)(void))
{
    uaddr address = (uaddr)handler;
    struct idt_desc_struct *gate = &this_cpu.idt[n];
    memset((u8 *)gate, 0, sizeof(*gate));
    gate->addr0 = address;
    gate->addr1 = address >> 16;
    gate->addr2 = address >> 32;
    gate->segment = __KERNEL_CS;
    gate->type = 0xe;
    gate->p = 1;
}

static void init_pic(void)
{
    outb(0x20, 0x11); outb(0xa0, 0x11);
    outb(0x21, 0x20); outb(0xa1, 0x28);
    outb(0x21, 0x04); outb(0xa1, 0x02);
    outb(0x21, 0x01); outb(0xa1, 0x01);
    outb(0x21, 0xff); outb(0xa1, 0xff);
}

void set_kernel_stack(uaddr esp)
{
    kernel_stack_top = esp;
    this_cpu.tss.rsp0 = esp;
}

void arch_init_stack(void)
{
}

void init_desc(void)
{
    u64 *gdt = (u64 *)this_cpu.gdt;
    uaddr tss = (uaddr)&this_cpu.tss;
    u64 limit = sizeof(this_cpu.tss) - 1;
    u64 low;
    int i;

    memset((u8 *)&this_cpu, 0, sizeof(this_cpu));
    gdt[0] = 0;
    gdt[1] = 0x00209a0000000000ULL;
    gdt[2] = 0x0000920000000000ULL;
    gdt[3] = 0x0000f20000000000ULL;
    gdt[4] = 0x0020fa0000000000ULL;
    low = (limit & 0xffff) | ((tss & 0xffffff) << 16) |
          (0x89ULL << 40) | ((limit >> 16) & 0xf) << 48 |
          ((tss >> 24) & 0xff) << 56;
    gdt[5] = low;
    gdt[6] = tss >> 32;
    this_cpu.tss.iomap_base = sizeof(this_cpu.tss);
    this_cpu.gdt_ptr.limit = sizeof(this_cpu.gdt) - 1;
    this_cpu.gdt_ptr.base = (uaddr)this_cpu.gdt;
    this_cpu.idt_ptr.limit = sizeof(this_cpu.idt) - 1;
    this_cpu.idt_ptr.base = (uaddr)this_cpu.idt;
    for (i = 0; i < IDT_ENTRY_NUM; i++) set_idt(i, isr_bad);
    set_idt(14, isr_page_fault);
    set_idt(6, isr_ud);
    set_idt(13, isr_gp);
    set_idt(IRQ0, irq_timer);
    set_idt(IRQ1, irq_keyboard);
    asm volatile("lgdt %0" :: "m"(this_cpu.gdt_ptr) : "memory");
    asm volatile("lidt %0" :: "m"(this_cpu.idt_ptr) : "memory");
    asm volatile("ltr %0" :: "r"((u16)0x28) : "memory");
    init_pic();
    irq_enable(2);
    register_interrupt_handler(14, page_fault);
    register_interrupt_handler(255, 0);
    {
        uaddr cr4;
        asm volatile("movq %%cr4, %0" : "=r"(cr4));
        cr4 |= 0x620;
        asm volatile("movq %0, %%cr4" :: "r"(cr4) : "memory");
        asm volatile("movq %%cr0, %0" : "=r"(cr4));
        cr4 &= ~0xcULL;
        asm volatile("movq %0, %%cr0" :: "r"(cr4) : "memory");
    }
    wrmsr(0xc0000080, rdmsr(0xc0000080) | 1);
    wrmsr(0xc0000081, (u64)__KERNEL_CS << 32);
    wrmsr(0xc0000082, (uaddr)syscall_entry);
    wrmsr(0xc0000084, 0x600);
}

void register_interrupt_handler(u8 n, interrupt_handler_t h)
{
    interrupt_handlers[n] = h;
}

void irq_enable(u8 irq)
{
    u16 mask = inb(0x21) | ((u16)inb(0xa1) << 8);
    mask &= ~(1u << irq);
    outb(0x21, mask);
    outb(0xa1, mask >> 8);
}

void int_handler(struct trap_frame *frame)
{
    interrupt_handler_t handler = interrupt_handlers[frame->int_no];
    if (frame->int_no >= 32 && frame->int_no <= 47) {
        outb(0x20, 0x20);
        if (frame->int_no >= 40) outb(0xa0, 0x20);
    }
    if (handler) handler(frame);
    else if (frame->int_no == 255 || frame->int_no == 6 || frame->int_no == 13) {
        printk("x86_64 exception %d pid=%d pc=%x error=%x cs=%x ss=%x flags=%x usp=%x rawsp=%x\n",
               frame->int_no, current->pid, frame->eip, frame->err_code,
               frame->cs, frame->ss, frame->eflags, frame->useresp,
               frame->raw[7]);
        cli();
        for (;;) hlt();
    }
    signal_deliver(frame);
}

void x86_64_syscall_dispatch(struct trap_frame *frame)
{
    syscall_handler(frame);
#ifdef DEBUG_E9
    {
        static int traced;
        if (!current->linux_abi && traced++ < 512)
            printk("x64 syscall nr=%d ret=%d pc=%x\n",
                   frame->raw[0], frame->eax, frame->eip);
    }
#endif
    frame->raw[0] = frame->eax;
    signal_deliver(frame);
}

reg_t arch_linux_syscall_number(reg_t nr)
{
    switch (nr) {
    case 0: return 3;
    case 1: return 4;
    case 2: return 5;
    case 3: return 6;
    case 7: return 168;
    case 8: return 19;
    case 9: return 192;
    case 10: return 125;
    case 11: return 91;
    case 12: return 45;
    case 14: return 175;
    case 16: return 54;
    case 19: return 145;
    case 20: return 146;
    case 21: return 33;
    case 22: return 42;
    case 24: return 158;
    case 27: return 27;
    case 32: return 41;
    case 33: return 63;
    case 34: return 29;
    case 36: return 36;
    case 39: return 20;
    case 43: return 543;
    case 57: case 58: return 2;
    case 59: return 11;
    case 60: return 1;
    case 61: return 114;
    case 62: return 37;
    case 63: return 122;
    case 72: return 55;
    case 74: return 118;
    case 76: return 92;
    case 77: return 93;
    case 79: return 183;
    case 80: return 12;
    case 82: return 38;
    case 83: return 39;
    case 84: return 40;
    case 85: return 55;
    case 87: return 10;
    case 89: return 85;
    case 90: return 15;
    case 92: return 182;
    case 93: return 181;
    case 94: return 94;
    case 95: return 60;
    case 97: return 597;
    case 98: return 598;
    case 99: return 599;
    case 102: return 24;
    case 104: return 47;
    case 105: return 23;
    case 106: return 46;
    case 107: return 49;
    case 108: return 50;
    case 109: return 71;
    case 110: return 64;
    case 111: return 65;
    case 112: return 66;
    case 113: return 70;
    case 114: return 24;
    case 116: return 81;
    case 117: return 164;
    case 119: return 170;
    case 115: return 205;
    case 118: return 618;
    case 120: return 620;
    case 121: return 132;
    case 126: return 242;
    case 158: return 658;
    case 172: return 172;
    case 186: return 686;
    case 15: return 173;
    case 13: return 513;
    case 35: return 535;
    case 78: return 578;
    case 4: return 504;
    case 5: return 505;
    case 157: return 172;
    case 204: return 242;
    case 217: return 220;
    case 201: return 13;
    case 218: return 258;
    case 228: return 728;
    case 234: return 270;
    case 291: return 340;
    case 231: return 252;
    case 240: return 240;
    case 248: return 340;
    case 257: return 295;
    case 258: return 296;
    case 260: return 309;
    case 262: return 762;
    case 263: return 301;
    case 264: return 302;
    case 266: return 304;
    case 267: return 305;
    case 268: return 306;
    case 269: return 307;
    case 270: return 308;
    case 280: return 299;
    case 316: return 302;
    case 293: return 331;
    case 318: return 355;
    case 332: return 383;
    default: return nr;
    }
}
