#include "drivers/timer.h"
#include "task.h"
#include "signal.h"
#include "init.h"
#include "tools.h"

#define GICD_BASE 0x08000000ULL
#define GICC_BASE 0x08010000ULL
#define TIMER_IRQ 27
#define UART_IRQ 33
static u32 current_iar;
volatile u32 tick;

static u32 gic_read(uaddr base, u32 reg)
{
    return *(volatile u32 *)(base + reg);
}
static void gic_write(uaddr base, u32 reg, u32 value)
{
    *(volatile u32 *)(base + reg) = value;
}
void aarch64_irq_enable_irq(u32 irq)
{
    gic_write(GICD_BASE, 0x100 + (irq / 32) * 4, 1u << (irq & 31));
}
static void timer_callback(void)
{
    u64 freq, reload;
    tick++;
    signal_check_alarm();
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    reload = freq / TIMER_HZ;
    if (!reload) reload = 1;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(reload) : "memory");
    schedule();
}
int aarch64_irq_number(void)
{
    u32 irq;
    current_iar = gic_read(GICC_BASE, 0x0c);
    irq = current_iar & 0x3ff;
    if (irq == TIMER_IRQ) return IRQ0;
    if (irq == UART_IRQ) return IRQ1;
    return 0;
}
void aarch64_irq_eoi(void)
{
    if (current_iar) gic_write(GICC_BASE, 0x10, current_iar);
    current_iar = 0;
}
void init_timer(u32 frequency)
{
    u64 freq, reload;
    (void)frequency;
    gic_write(GICD_BASE, 0, 0);
    *(volatile u8 *)(uaddr)(GICD_BASE + 0x800 + TIMER_IRQ) = 1;
    *(volatile u8 *)(uaddr)(GICD_BASE + 0x800 + UART_IRQ) = 1;
    aarch64_irq_enable_irq(TIMER_IRQ);
    aarch64_irq_enable_irq(UART_IRQ);
    gic_write(GICC_BASE, 0, 1);
    gic_write(GICC_BASE, 4, 0xff);
    gic_write(GICD_BASE, 0, 1);
    register_interrupt_handler(IRQ0, (interrupt_handler_t)timer_callback);
    asm volatile("mrs %0, cntfrq_el0" : "=r"(freq));
    reload = freq / TIMER_HZ;
    if (!reload) reload = 1;
    asm volatile("msr cntv_tval_el0, %0" :: "r"(reload) : "memory");
    asm volatile("msr cntv_ctl_el0, %0" :: "r"(1ULL) : "memory");
}
