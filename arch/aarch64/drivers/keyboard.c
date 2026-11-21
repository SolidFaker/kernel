#include "init.h"
#include "drivers/keyboard.h"
#include "drivers/pl011.h"
#include "drivers/tty.h"

#define KBD_BUF_SIZE 128
static char kbd_buf[KBD_BUF_SIZE];
static u32 kbd_head, kbd_tail;
u8 pressed;

static void kbd_putc(char c)
{
    u32 next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) return;
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

int kbd_haschar(void)
{
    int c;
    if (kbd_tail != kbd_head) return 1;
    c = pl011_getchar();
    if (c >= 0) {
        if (!tty_handle_control(tty_cur, (u8)c)) kbd_putc((char)c);
        return kbd_tail != kbd_head;
    }
    return 0;
}

int kbd_getchar(void)
{
    char c;
    if (kbd_tail == kbd_head) return -1;
    c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return (u8)c;
}

void keyboard_callback(void)
{
    int c;
    while ((c = pl011_getchar()) >= 0) {
        if (!tty_handle_control(tty_cur, (u8)c)) kbd_putc((char)c);
    }
}

void init_keyboard(void)
{
    register_interrupt_handler(IRQ1, (interrupt_handler_t)keyboard_callback);
    aarch64_irq_enable_irq(33);
}

u8 is_controls_pressed(u8 key)
{
    (void)key;
    return 0;
}

u8 pressed_key(void)
{
    return 0;
}

u8 is_pressed(u8 keycode)
{
    (void)keycode;
    return 0;
}
