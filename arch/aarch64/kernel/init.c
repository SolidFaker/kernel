#include "common.h"
#include "init.h"
#include "signal.h"
#include "syscall.h"
#include "string.h"
#include "tools.h"

struct cpu_struct this_cpu;
interrupt_handler_t interrupt_handlers[256];
uaddr kernel_stack_top;
extern u8 aarch64_vector_table[];

void init_desc(void)
{
    asm volatile("msr vbar_el1, %0" :: "r"((uaddr)aarch64_vector_table) : "memory");
}
void register_interrupt_handler(u8 n, interrupt_handler_t h)
{
    interrupt_handlers[n] = h;
}
void irq_enable(u8 irq) { (void)irq; }
void set_kernel_stack(uaddr esp) { kernel_stack_top = esp; }
void arch_init_stack(void) { }
void int_handler(struct trap_frame *frame)
{
    interrupt_handler_t handler = interrupt_handlers[frame->int_no];
    if (handler) handler(frame);
    signal_deliver(frame);
}
void aarch64_error_dispatch(struct trap_frame *frame)
{
    printk("AArch64 exception pc=%x esr=%x\n", frame->eip, frame->err_code);
    cli();
    for (;;) hlt();
}
