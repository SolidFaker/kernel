#include "arch/arch.h"

#define SERIAL_PORT 0x3f8u

int serial_getchar(void)
{
    if (!(inb(SERIAL_PORT + 5) & 1)) return -1;
    return inb(SERIAL_PORT);
}

static void serial_out(u8 c)
{
    while (!(inb(SERIAL_PORT + 5) & 0x20)) {
        asm volatile("" ::: "memory");
    }
    outb(SERIAL_PORT, c);
}

void arch_console_init(void)
{
    outb(SERIAL_PORT + 1, 0);
    outb(SERIAL_PORT + 3, 0x80);
    outb(SERIAL_PORT, 1);
    outb(SERIAL_PORT + 1, 0);
    outb(SERIAL_PORT + 3, 0x03);
    outb(SERIAL_PORT + 2, 0xc7);
}

void arch_console_write(const char *text, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            serial_out('\r');
        }
        serial_out((u8)text[i]);
    }
}

void arch_idle(void)
{
    for (;;) {
        hlt();
    }
}
