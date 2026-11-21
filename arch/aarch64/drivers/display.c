#include "drivers/display.h"
#include "drivers/fbcon.h"
#include "drivers/tty.h"
#include "drivers/pl011.h"
#include "string.h"

struct tty *tty_print = &tty[0];

void flush_screen(struct tty *tty)
{
    (void)tty;
}

void flush_line(struct tty *tty, u8 row)
{
    (void)tty;
    (void)row;
}

void display_print_color(u8 bg, u8 fg, const char *string)
{
    (void)bg;
    (void)fg;
    pl011_write(string, (u32)strlen(string));
}

void display_print(const char *string)
{
    pl011_write(string, (u32)strlen(string));
}

void display_write(struct tty *tty, const char *string, u32 len)
{
    (void)tty;
    pl011_write(string, len);
}

void display_putc(u8 bg, u8 fg, struct tty *tty, char c)
{
    (void)bg;
    (void)fg;
    display_write(tty, &c, 1);
}

void display_print_hex(u32 num)
{
    static const char hex[] = "0123456789abcdef";
    char buf[8];
    int i;
    for (i = 7; i >= 0; i--) {
        buf[i] = hex[num & 15];
        num >>= 4;
    }
    pl011_write(buf, sizeof(buf));
}

int fbcon_init(void) { return 0; }
int fbcon_ready(void) { return 0; }
void fbcon_select(struct tty *tty) { (void)tty; }
void fbcon_redraw_all(void) { }
void fbcon_draw_cell(struct tty *tty, u32 row, u32 col)
{
    (void)tty; (void)row; (void)col;
}
void fbcon_draw_row(struct tty *tty, u32 row) { (void)tty; (void)row; }
void fbcon_scroll(struct tty *tty) { (void)tty; }
void fbcon_clear(struct tty *tty) { (void)tty; }
void fbcon_cursor_update(struct tty *tty) { (void)tty; }
void fbcon_blink(void) { }
