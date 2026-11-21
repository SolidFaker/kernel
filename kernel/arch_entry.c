#include "arch/arch.h"

#ifndef ARCH_NAME
#define ARCH_NAME "unknown"
#endif

void kernel_start(void)
{
    static const char message[] =
        "A-MINI-KERNEL " ARCH_NAME ": native boot OK\n";
    arch_console_init();
    arch_console_write(message, sizeof(message) - 1);
    arch_idle();
}
