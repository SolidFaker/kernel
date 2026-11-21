#include "arch/arch.h"
#include "drivers/display.h"
#include "drivers/tty.h"
#include "drivers/fbcon.h"
#include "common.h"
#include "string.h"

struct tty *tty_print;

/* Current console colours, 0..15, updated by ANSI SGR sequences. */
static u8 cur_fg = COLOR_LIGHT_GREY;
static u8 cur_bg = COLOR_BLACK;

/* Minimal ANSI SGR parser: ESC [ ... m. */
static u8 ansi_state;
static u8 ansi_count;
static u8 ansi_params[8];
static u32 ansi_value;
static int ansi_have_digit;

static void ansi_reset(void)
{
    ansi_state = 0;
    ansi_count = 0;
    ansi_value = 0;
    ansi_have_digit = 0;
    cur_fg = COLOR_LIGHT_GREY;
    cur_bg = COLOR_BLACK;
}

static void ansi_push(u32 v)
{
    if (ansi_count < sizeof(ansi_params)) {
        ansi_params[ansi_count++] = (u8)v;
    }
}

static void ansi_apply(void)
{
    u32 i = 0;

    ansi_push(ansi_have_digit ? ansi_value : 0);
    ansi_have_digit = 0;
    ansi_value = 0;

    if (ansi_count == 1 && ansi_params[0] == 0) {
        cur_fg = COLOR_LIGHT_GREY;
        cur_bg = COLOR_BLACK;
        ansi_count = 0;
        return;
    }

    while (i < ansi_count) {
        u32 p = ansi_params[i++];
        if (p == 0) {
            cur_fg = COLOR_LIGHT_GREY;
            cur_bg = COLOR_BLACK;
        } else if (p == 1) {
            cur_fg |= 8;
        } else if (p == 22) {
            cur_fg &= 7;
        } else if (p == 39) {
            cur_fg = COLOR_LIGHT_GREY;
        } else if (p == 49) {
            cur_bg = COLOR_BLACK;
        } else if (p == 38 || p == 48) {
            /* skip 38;5;n / 48;5;n and 38;2;r;g;b */
            if (i < ansi_count && ansi_params[i] == 5) {
                i += 2;
            } else if (i < ansi_count && ansi_params[i] == 2) {
                i += 4;
            }
        } else if (p >= 30 && p <= 37) {
            cur_fg = (u8)((cur_fg & 8) | (p - 30));
        } else if (p >= 40 && p <= 47) {
            cur_bg = (u8)(p - 40);
        } else if (p >= 90 && p <= 97) {
            cur_fg = (u8)(p - 90 + 8);
        } else if (p >= 100 && p <= 107) {
            cur_bg = (u8)(p - 100 + 8);
        }
    }
    ansi_count = 0;
}

static void scroll(struct tty *_tty)
{
    u32 i;
    u32 cells = TTY_COLS * TTY_ROWS;

    if (_tty->cursor_y < TTY_ROWS) {
        return;
    }

    for (i = 0; i < cells - TTY_COLS; i++) {
        _tty->buffer[i] = _tty->buffer[i + TTY_COLS];
    }
    for (i = cells - TTY_COLS; i < cells; i++) {
        _tty->buffer[i] = 0x20 | ((u16)((cur_bg << 4) | cur_fg) << 8);
    }
    _tty->cursor_y = TTY_ROWS - 1;

    if (_tty == tty_cur) {
        fbcon_scroll(_tty);
    }
}

static void put_raw(struct tty *_tty, char c)
{
    u8 fg = cur_fg & 0x0F;
    u8 bg = cur_bg & 0x0F;
    u16 attr = (u16)((bg << 4) | fg) << 8;
    u32 pos;

    if (c == 0x08 && _tty->cursor_x) {
        _tty->cursor_x--;
        pos = _tty->cursor_y * TTY_COLS + _tty->cursor_x;
        _tty->buffer[pos] = ' ' | attr;
        if (_tty == tty_cur) {
            fbcon_draw_cell(_tty, _tty->cursor_y, _tty->cursor_x);
        }
    } else if (c == 0x09) {
        _tty->cursor_x = (u8)((_tty->cursor_x + 8) & ~7);
    } else if (c == '\r') {
        _tty->cursor_x = 0;
    } else if (c == '\n') {
        _tty->cursor_x = 0;
        _tty->cursor_y++;
    } else if (c >= ' ') {
        pos = _tty->cursor_y * TTY_COLS + _tty->cursor_x;
        _tty->buffer[pos] = (u8)c | attr;
        _tty->cursor_x++;
        if (_tty == tty_cur) {
            fbcon_draw_cell(_tty, _tty->cursor_y, pos % TTY_COLS);
        }
    }

    if (_tty->cursor_x >= TTY_COLS) {
        _tty->cursor_x = 0;
        _tty->cursor_y++;
    }

    scroll(_tty);

    if (_tty == tty_cur) {
        fbcon_cursor_update(_tty);
    }
}

void display_putc(u8 bg, u8 fg, struct tty *_tty, char c)
{
    u8 old_fg = cur_fg;
    u8 old_bg = cur_bg;

    cur_fg = fg;
    cur_bg = bg;
    put_raw(_tty, c);
    cur_fg = old_fg;
    cur_bg = old_bg;
}

static void ansi_feed(struct tty *_tty, u8 c)
{
    if (ansi_state == 0) {
        if (c == 0x1B) {
            ansi_state = 1;
        } else {
            put_raw(_tty, (char)c);
        }
        return;
    }

    if (ansi_state == 1) {
        if (c == '[') {
            ansi_state = 2;
            ansi_count = 0;
            ansi_value = 0;
            ansi_have_digit = 0;
        } else {
            ansi_state = 0;
            put_raw(_tty, (char)c);
        }
        return;
    }

    /* ansi_state == 2: CSI parameters */
    if (c >= '0' && c <= '9') {
        ansi_have_digit = 1;
        ansi_value = ansi_value * 10 + (u32)(c - '0');
    } else if (c == ';') {
        ansi_push(ansi_have_digit ? ansi_value : 0);
        ansi_have_digit = 0;
        ansi_value = 0;
    } else if (c == 'm') {
        ansi_apply();
        ansi_state = 0;
    } else {
        ansi_state = 0;
    }
}

void flush_line(struct tty *_tty, u8 line)
{
    u32 i;
    u16 blank = 0x20 | (0x07 << 8);

    for (i = 0; i < TTY_COLS; i++) {
        _tty->buffer[line * TTY_COLS + i] = blank;
    }
    _tty->cursor_x = 0;
    if (_tty == tty_cur) {
        fbcon_draw_row(_tty, line);
    }
}

void flush_screen(struct tty *_tty)
{
    u32 i;
    u16 blank = 0x20 | (0x07 << 8);

    for (i = 0; i < TTY_COLS * TTY_ROWS; i++) {
        _tty->buffer[i] = blank;
    }
    _tty->cursor_x = 0;
    _tty->cursor_y = 0;
    ansi_reset();
    if (_tty == tty_cur) {
        fbcon_redraw_all();
    }
}

void display_print(const char *string)
{
#if defined(__x86_64__)
    arch_console_write(string, (u32)strlen(string));
#endif
    ansi_reset();
    while (*string) {
        put_raw(tty_print, *string++);
    }
}

// Bounded write used by the write() syscall: len bytes, no NUL needed.
// This path understands ANSI SGR colour sequences.
void display_write(struct tty *_tty, const char *string, u32 len)
{
    u32 i;
    for (i = 0; i < len; i++) {
        ansi_feed(_tty, (u8)string[i]);
    }
#if defined(__x86_64__)
    arch_console_write(string, len);
#endif
}

void display_print_color(u8 bg, u8 fg, const char *string)
{
    u8 old_fg = cur_fg;
    u8 old_bg = cur_bg;

    cur_fg = fg;
    cur_bg = bg;
#if defined(__x86_64__)
    arch_console_write(string, (u32)strlen(string));
#endif
    while (*string) {
        put_raw(tty_print, *string++);
    }
    cur_fg = old_fg;
    cur_bg = old_bg;
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
