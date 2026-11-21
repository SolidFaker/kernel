#include "drivers/pl011.h"

#define UART0_BASE 0x09000000u
#define UART_DR  0x00u
#define UART_FR  0x18u
#define UART_FR_TXFF 0x20u

static void pl011_out(u8 c)
{
    while (*(volatile u32 *)(uaddr)(UART0_BASE + UART_FR) & UART_FR_TXFF) {
        asm volatile("" ::: "memory");
    }
    *(volatile u32 *)(uaddr)(UART0_BASE + UART_DR) = c;
}

void pl011_init(void)
{
    *(volatile u32 *)(uaddr)(UART0_BASE + 0x30) = 0x301;
    *(volatile u32 *)(uaddr)(UART0_BASE + 0x38) = 0x10;
}

int pl011_getchar(void)
{
    if (*(volatile u32 *)(uaddr)(UART0_BASE + UART_FR) & 0x10) {
        return -1;
    }
    return *(volatile u32 *)(uaddr)(UART0_BASE + UART_DR) & 0xff;
}

void pl011_write(const char *text, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        if (text[i] == '\n') {
            pl011_out('\r');
        }
        pl011_out((u8)text[i]);
    }
}
