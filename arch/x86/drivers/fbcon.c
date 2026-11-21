// Framebuffer console: a small graphical terminal for the VBE linear
// framebuffer set up by the 16-bit loader.
//
// Layout: a rounded "window" with a title bar, a 60x40 grid of 16x16
// cells (8x8 font scaled 2x) and a status bar.

#include "drivers/fbcon.h"
#include "drivers/tty.h"
#include "drivers/graphic.h"
#include "drivers/timer.h"
#include "bootinfo.h"
#include "font8x8.h"
#include "page.h"

#define SCR_W      1280
#define SCR_H      1024
#define CELL_W     16
#define CELL_H     16
#define GRID_W     (TTY_COLS * CELL_W)
#define GRID_H     (TTY_ROWS * CELL_H)
#define PAD_X      16
#define TITLE_H    34
#define STATUS_H   26
#define WIN_W      (GRID_W + 2 * PAD_X)
#define WIN_H      (TITLE_H + GRID_H + STATUS_H)
#define WIN_X      ((SCR_W - WIN_W) / 2)
#define WIN_Y      ((SCR_H - WIN_H) / 2)
#define GRID_X     (WIN_X + PAD_X)
#define GRID_Y     (WIN_Y + TITLE_H)

#define COL_BG      0x101018
#define COL_WIN     0x1e1e2e
#define COL_BAR     0x181825
#define COL_STATUS  0x11111b
#define COL_BORDER  0x313244
#define COL_TITLE   0xcdd6f4
#define COL_MUTED   0x7f849c
#define COL_ACCENT  0x89b4fa
#define COL_RED     0xf38ba8
#define COL_YELLOW  0xf9e2af
#define COL_GREEN   0xa6e3a1

static const u32 palette16[16] = {
    0x1e1e2e, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
    0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xcdd6f4,
    0x585b70, 0xf38ba8, 0xa6e3a1, 0xf9e2af,
    0x89b4fa, 0xf5c2e7, 0x94e2d5, 0xffffff
};

static u32 *fb;
static u32 fb_pitch;
static int ready;
static struct tty *cur;
static u32 cursor_row;
static u32 cursor_col;
static int cursor_on = 1;

static inline void fb_fill_words(u32 *dst, u32 color, u32 n)
{
    if (!n) {
        return;
    }
    asm volatile("cld; rep stosl"
                 : "+D"(dst), "+c"(n)
                 : "a"(color)
                 : "memory");
}

static inline void fb_copy_words(u32 *dst, const u32 *src, u32 n)
{
    if (!n) {
        return;
    }
    asm volatile("cld; rep movsl"
                 : "+D"(dst), "+S"(src), "+c"(n)
                 :
                 : "memory");
}

static void fill_rect(int x, int y, int w, int h, u32 color)
{
    int i, j;

    (void)i;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCR_W) { w = SCR_W - x; }
    if (y + h > SCR_H) { h = SCR_H - y; }
    if (w <= 0 || h <= 0) {
        return;
    }
    for (j = 0; j < h; j++) {
        fb_fill_words(fb + (u32)(y + j) * fb_pitch + x, color, (u32)w);
    }
}

static void fill_disc(int cx, int cy, int r, u32 color)
{
    int dy;

    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) {
            dx++;
        }
        fill_rect(cx - dx, cy + dy, 2 * dx + 1, 1, color);
    }
}

static const u8 corner_inset[8] = { 8, 5, 3, 2, 1, 1, 0, 0 };

static void fill_round_rect(int x, int y, int w, int h, u32 color)
{
    int row;

    for (row = 0; row < h; row++) {
        int inset = 0;
        if (row < 8) {
            inset = corner_inset[row];
        } else if (row >= h - 8) {
            inset = corner_inset[h - 1 - row];
        }
        fill_rect(x + inset, y + row, w - 2 * inset, 1, color);
    }
}

static void draw_char(int x, int y, u8 ch, u32 color, int scale)
{
    const unsigned char *glyph;
    int row, col;

    if (ch >= 128) {
        ch = '?';
    }
    if (x < 0 || y < 0 || x + 8 * scale > SCR_W ||
            y + 8 * scale > SCR_H) {
        glyph = font8x8[ch];
        for (row = 0; row < 8; row++) {
            u8 bits = glyph[row];
            for (col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    fill_rect(x + col * scale, y + row * scale,
                              scale, scale, color);
                }
            }
        }
        return;
    }

    glyph = font8x8[ch];
    if (scale == 2) {
        for (row = 0; row < 8; row++) {
            u8 bits = glyph[row];
            if (!bits) {
                continue;
            }
            u32 *r0 = fb + (u32)(y + row * 2) * fb_pitch + x;
            u32 *r1 = r0 + fb_pitch;
            for (col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    int px = col * 2;
                    r0[px] = r0[px + 1] = color;
                    r1[px] = r1[px + 1] = color;
                }
            }
        }
    } else {
        for (row = 0; row < 8; row++) {
            u8 bits = glyph[row];
            u32 *r = fb + (u32)(y + row * scale) * fb_pitch + x;
            for (col = 0; col < 8; col++) {
                if (bits & (1 << col)) {
                    r[col * scale] = color;
                }
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, u32 color, int scale)
{
    while (*s) {
        draw_char(x, y, (u8)*s, color, scale);
        x += 8 * scale;
        s++;
    }
}

static u32 utoa(char *dst, u32 v)
{
    char tmp[10];
    u32 i = 0, n = 0;

    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (i > 0) {
        dst[n++] = tmp[--i];
    }
    return n;
}

static void draw_status(struct tty *t)
{
    char buf[32];
    u32 n = 0;
    u32 secs = tick / TIMER_HZ;
    int y = WIN_Y + WIN_H - STATUS_H + 9;

    fill_rect(WIN_X + 1, WIN_Y + WIN_H - STATUS_H,
              WIN_W - 2, STATUS_H, COL_STATUS);

    draw_text(GRID_X, y, "a-mini-kernel", COL_MUTED, 1);
    draw_text(GRID_X + 16 * 8, y, "Ctrl+Alt+1..4", COL_MUTED, 1);

    n += utoa(buf + n, (u32)(t - tty));
    buf[n] = 0;
    draw_text(WIN_X + WIN_W - PAD_X - 88, y, "tty", COL_MUTED, 1);
    draw_text(WIN_X + WIN_W - PAD_X - 56, y, buf, COL_ACCENT, 1);

    buf[0] = (char)('0' + (secs / 60) / 10);
    buf[1] = (char)('0' + (secs / 60) % 10);
    buf[2] = ':';
    buf[3] = (char)('0' + (secs % 60) / 10);
    buf[4] = (char)('0' + (secs % 60) % 10);
    buf[5] = 0;
    draw_text(WIN_X + WIN_W - PAD_X - 40, y, buf, COL_MUTED, 1);
}

static void draw_window(struct tty *t)
{
    char title[28];
    u32 n;
    int row;

    fill_round_rect(WIN_X + 8, WIN_Y + 8, WIN_W, WIN_H, 0x08080c);
    fill_round_rect(WIN_X, WIN_Y, WIN_W, WIN_H, COL_WIN);

    for (row = 0; row < TITLE_H; row++) {
        int inset = row < 8 ? corner_inset[row] : 0;
        fill_rect(WIN_X + inset, WIN_Y + row,
                  WIN_W - 2 * inset, 1, COL_BAR);
    }
    fill_rect(WIN_X, WIN_Y + TITLE_H, WIN_W, 1, COL_BORDER);

    fill_disc(WIN_X + 22, WIN_Y + 17, 5, COL_RED);
    fill_disc(WIN_X + 40, WIN_Y + 17, 5, COL_YELLOW);
    fill_disc(WIN_X + 58, WIN_Y + 17, 5, COL_GREEN);

    n = 0;
    {
        const char *p = "a-mini-kernel - tty ";
        while (*p) {
            title[n++] = *p++;
        }
    }
    n += utoa(title + n, (u32)(t - tty));
    title[n] = 0;
    draw_text(WIN_X + 80, WIN_Y + 13, title, COL_TITLE, 1);
    {
        char dims[12];
        u32 d = 0;
        d += utoa(dims + d, TTY_COLS);
        dims[d++] = 'x';
        d += utoa(dims + d, TTY_ROWS);
        dims[d] = 0;
        draw_text(WIN_X + WIN_W - PAD_X - (int)d * 8, WIN_Y + 13,
                  dims, COL_MUTED, 1);
    }

    fill_rect(GRID_X, GRID_Y, GRID_W, GRID_H, palette16[0]);
    draw_status(t);
}

void fbcon_draw_cell(struct tty *t, u32 row, u32 col)
{
    u16 cell;
    u8 ch, attr, fg, bg;
    int x, y;

    if (!ready || gfx_is_active()) {
        return;
    }
    if (!t || row >= TTY_ROWS || col >= TTY_COLS) {
        return;
    }

    cell = t->buffer[row * TTY_COLS + col];
    ch = (u8)(cell & 0xFF);
    attr = (u8)(cell >> 8);
    fg = attr & 0x0F;
    bg = (attr >> 4) & 0x0F;

    x = GRID_X + (int)col * CELL_W;
    y = GRID_Y + (int)row * CELL_H;
    fill_rect(x, y, CELL_W, CELL_H, palette16[bg]);
    if (ch >= 32 && ch < 128) {
        draw_char(x, y, ch, palette16[fg], 2);
    }
}

void fbcon_clear(struct tty *t)
{
    (void)t;
    if (!ready) {
        return;
    }
    fill_rect(GRID_X, GRID_Y, GRID_W, GRID_H, palette16[0]);
}

void fbcon_draw_row(struct tty *t, u32 row)
{
    u32 col;

    if (!ready || !t || row >= TTY_ROWS) {
        return;
    }
    for (col = 0; col < TTY_COLS; col++) {
        fbcon_draw_cell(t, row, col);
    }
}

void fbcon_scroll(struct tty *t)
{
    u32 *dst, *src, n;

    if (!ready || !t || t != cur || gfx_is_active()) {
        return;
    }
    dst = fb + (u32)GRID_Y * fb_pitch;
    src = fb + (u32)(GRID_Y + CELL_H) * fb_pitch;
    n = (u32)(GRID_H - CELL_H) * fb_pitch;
    fb_copy_words(dst, src, n);
    fbcon_draw_row(t, TTY_ROWS - 1);
    fbcon_cursor_update(t);
}

static void draw_cursor_bar(struct tty *t)
{
    int x = GRID_X + (int)t->cursor_x * CELL_W;
    int y = GRID_Y + (int)t->cursor_y * CELL_H;

    fill_rect(x, y, 2, CELL_H, COL_ACCENT);
}

void fbcon_cursor_update(struct tty *t)
{
    if (!ready || t != cur || gfx_is_active()) {
        return;
    }

    if (cursor_row != t->cursor_y || cursor_col != t->cursor_x) {
        fbcon_draw_cell(t, cursor_row, cursor_col);
        cursor_row = t->cursor_y;
        cursor_col = t->cursor_x;
    }
    if (cursor_on) {
        draw_cursor_bar(t);
    }
}

void fbcon_redraw_all(void)
{
    u32 row, col;

    if (!ready || gfx_is_active()) {
        return;
    }
    if (!cur) {
        cur = &tty[0];
    }

    fill_rect(0, 0, SCR_W, SCR_H, COL_BG);
    draw_window(cur);

    for (row = 0; row < TTY_ROWS; row++) {
        for (col = 0; col < TTY_COLS; col++) {
            fbcon_draw_cell(cur, row, col);
        }
    }

    cursor_row = cur->cursor_y;
    cursor_col = cur->cursor_x;
    if (cursor_on) {
        draw_cursor_bar(cur);
    }
}

void fbcon_select(struct tty *t)
{
    cur = t;
    cursor_on = 1;
    cursor_row = t->cursor_y;
    cursor_col = t->cursor_x;
    fbcon_redraw_all();
}

void fbcon_blink(void)
{
    if (!ready || !cur || gfx_is_active()) {
        return;
    }
    cursor_on = !cursor_on;
    if (cursor_on) {
        draw_cursor_bar(cur);
    } else {
        fbcon_draw_cell(cur, cur->cursor_y, cur->cursor_x);
    }
    draw_status(cur);
}

int fbcon_ready(void)
{
    return ready;
}

int fbcon_init(void)
{
    struct bootinfo *bi =
        (struct bootinfo *)(BOOTINFO_ADDR + PAGE_OFFSET);

    if (bi->magic != BOOTINFO_MAGIC || bi->bpp != 32 ||
            bi->width != SCR_W || bi->height != SCR_H ||
            bi->pitch < SCR_W * 4) {
        return 0;
    }

#if defined(__x86_64__)
    {
        uaddr off, size = (uaddr)bi->pitch * bi->height;
        for (off = 0; off < size; off += PAGE_SIZE) {
            map(pdt_kernel, FB_VIRT_BASE + off, (uaddr)bi->fb_addr + off,
                PG_PRESENT | PG_WRITE);
        }
    }
#endif
    fb = (u32 *)FB_VIRT_BASE;
    fb_pitch = bi->pitch / 4;
    ready = 1;
    cur = &tty[0];
    cursor_row = cur->cursor_y;
    cursor_col = cur->cursor_x;
    cursor_on = 1;
    fbcon_redraw_all();
    return 1;
}
