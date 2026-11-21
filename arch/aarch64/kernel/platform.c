#include "arch/arch.h"
#include "drivers/pl011.h"

void arch_console_init(void)
{
    pl011_init();
}

void arch_console_write(const char *text, u32 len)
{
    pl011_write(text, len);
}

void arch_idle(void)
{
    for (;;) {
        hlt();
    }
}
