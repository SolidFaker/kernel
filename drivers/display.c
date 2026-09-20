#include "drivers/display.h"
#include "drivers/tty.h"
#include "common.h"
#include "string.h"

struct tty *tty_print;

static void flush_cursor()
{
    // for default vga support the screen width = 80
    u16 cursor_location =
        tty_cur->cursor_y * 80 + tty_cur->cursor_x + (tty_cur->offset>>1);
    u32 flags = local_irq_save();
    outb(CRT_ADDR_REG, CURSOR_H); // set vga cursor high 8 bit
    outb(CRT_DATA_REG, cursor_location >> 8); // send cursor high 8 bit
    outb(CRT_ADDR_REG, CURSOR_L); // set vga cursor low 8 bit
    outb(CRT_DATA_REG, cursor_location); // send cursor low 8 bit
    local_irq_restore(flags);
}

void flush_line(struct tty *_tty, u8 line)
{
    u8 attribute_byte = (COLOR_BLACK << 4) | (COLOR_WHITE);
    u16 blank = 0x20 | (attribute_byte << 8);
    u16 i;
    for (i=0; i<80; i++){
        _tty->buffer[line*80+i] = blank;
    }
    _tty->cursor_x = 0;
    flush_cursor();
}

void flush_screen(struct tty *_tty)
{
    u8 attribute_byte = (COLOR_BLACK << 4) | (COLOR_WHITE);
    u16 blank = 0x20 | (attribute_byte << 8);
    u16 i;
    // clear every row below the title bar, including the last one
    for (i = 1 * 80; i < 25 * 80; i++){
        _tty->buffer[i] = blank;
    }
    _tty->cursor_x = 0;
    _tty->cursor_y = 1;
    flush_cursor();
}

static inline void scroll(struct tty *_tty)
{
    if (_tty->cursor_y > 24) {
        u16 i;

        for (i = 1 * 80; i < 24 * 80; i++) {
            _tty->buffer[i] = _tty->buffer[i + 80];
        }
        flush_line(_tty, 24);

        _tty->cursor_y = 24;
        flush_cursor();
    }
}

void display_putc(u8 bg, u8 fg, struct tty *_tty, char c)
{
    u16 attribute = (u16)(fg | bg << 4)<<8;

    if (c == 0x08 && _tty->cursor_x) {
        // backspace
        _tty->cursor_x--;
        _tty->buffer[_tty->cursor_y*80 + _tty->cursor_x] = 
            ' ' | ((COLOR_WHITE|COLOR_BLACK<<4)<<8);
    } else if (c == 0x09) {
        //tab
        _tty->cursor_x = (_tty->cursor_x+8) & ~(8-1);
    } else if (c == '\r') {
        _tty->cursor_x = 0;
    } else if (c == '\n') {
        _tty->cursor_x = 0;
        _tty->cursor_y++;
    } else if (c >= ' ') {
        _tty->buffer[_tty->cursor_y*80 + _tty->cursor_x] = c | attribute;
        _tty->cursor_x++;
    }

    // start new line when _tty->cursor_x > 79
    if (_tty->cursor_x > 79) {
        _tty->cursor_x = 0;
        _tty->cursor_y++;
    }

    scroll(_tty);
    if (_tty == tty_cur){
        flush_cursor();
    }
}

void display_print(const char *string)
{
    while (*string) {
        display_putc(COLOR_BLACK, COLOR_WHITE, tty_print, *string++);
    }
}

// Bounded write used by the write() syscall: len bytes, no NUL needed.
void display_write(struct tty *_tty, const char *string, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        display_putc(COLOR_BLACK, COLOR_WHITE, _tty, string[i]);
    }
}

void display_print_color(u8 bg, u8 fg, const char *string)
{
    while (*string) {
        display_putc(bg, fg, tty_print, *string++);
    }
}

void display_print_hex(u32 num)
{
    char buf[11] = "0x00000000";
    int i;
    for (i = 0; i < 8; i++) {
        buf[2 + i] = "0123456789abcdef"[(num >> ((7 - i) * 4)) & 0xf];
    }
    display_print(buf);
}
