#include "drivers/timer.h"
#include "drivers/fbcon.h"
#include "common.h"
#include "tools.h"
#include "task.h"
#include "init.h"
#include "signal.h"

volatile u32 tick = 0;
void timer_callback()
{
    static u32 blink_ticks = 0;

    tick++;
#ifdef DEBUG_E9
    if (!(tick % 2000))
        printk("timer tick=%d current=%d\n", tick, current->pid);
#endif
    signal_check_alarm();
    if (++blink_ticks >= TIMER_HZ / 2) {
        blink_ticks = 0;
        fbcon_blink();
    }
    schedule();
}

void init_timer(u32 frequency)
{
    // register timer tick interrupt handler
    register_interrupt_handler(IRQ0, (interrupt_handler_t)timer_callback);
    // Intel 8253/8254 Chip's PIT I/ Port start from O40h to 43h
    // hardware frequency is 1193180
    u32 divisor = 1193180 / frequency;

    // D7 D6 D5 D4 D3 D2 D1 D0
    // 0 0 1 1 0 1 1 0
    // set 8253/8254 running mode 3
    outb(0x43, 0x36);

    // set frequency
    outb(0x40, (divisor & 0xFF));
    outb(0x40, ((divisor >> 8) & 0xFF));

    // enable timer tick interrupt
    irq_enable(0);
}
