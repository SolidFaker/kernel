// VBE linear-framebuffer graphics used by the user-mode demo.
//
// User programs still draw on a logical 320x200 canvas (see include/gfx.h);
// it is scaled 3x and centred inside the 1024x768 framebuffer. The terminal
// is restored when gfx_leave() is called.

#include "drivers/graphic.h"
#include "drivers/fbcon.h"
#include "font8x8.h"
#include "gfx.h"
#include "page.h"

#define SCR_W 1280
#define SCR_H 1024
#define LOG_W GFX_WIDTH
#define LOG_H GFX_HEIGHT
#define SCALE 4
#define OFF_X ((SCR_W - LOG_W * SCALE) / 2)
#define OFF_Y ((SCR_H - LOG_H * SCALE) / 2)

static u32 *fb = (u32 *)FB_VIRT_BASE;
static int gfx_on = 0;

static u32 color332(u8 c)
{
    u32 r = (u32)((c >> 5) & 7) * 255 / 7;
    u32 g = (u32)((c >> 2) & 7) * 255 / 7;
    u32 b = (u32)(c & 3) * 255 / 3;
    return (r << 16) | (g << 8) | b;
}

static void fill_phys(int x, int y, int w, int h, u32 color)
{
    int i, j;

    for (j = 0; j < h; j++) {
        int py = y + j;
        if (py < 0 || py >= SCR_H) {
            continue;
        }
        for (i = 0; i < w; i++) {
            int px = x + i;
            if (px >= 0 && px < SCR_W) {
                fb[py * SCR_W + px] = color;
            }
        }
    }
}

int gfx_is_active(void)
{
    return gfx_on;
}

int gfx_enter(void)
{
    if (!fbcon_ready()) {
        return -1;
    }

    gfx_on = 1;
    fill_phys(0, 0, SCR_W, SCR_H, 0x101018);
    fill_phys(OFF_X - 4, OFF_Y - 4,
              LOG_W * SCALE + 8, LOG_H * SCALE + 8, 0x313244);
    fill_phys(OFF_X, OFF_Y, LOG_W * SCALE, LOG_H * SCALE, 0x000000);
    return 0;
}

void gfx_leave(void)
{
    if (!gfx_on) {
        return;
    }
    gfx_on = 0;
    fbcon_redraw_all();
}

void gfx_clear(u8 color)
{
    if (!gfx_on) {
        return;
    }
    fill_phys(OFF_X, OFF_Y, LOG_W * SCALE, LOG_H * SCALE,
              color332(color));
}

void gfx_set_pixel(int x, int y, u8 color)
{
    if (!gfx_on || x < 0 || y < 0 || x >= LOG_W || y >= LOG_H) {
        return;
    }
    fill_phys(OFF_X + x * SCALE, OFF_Y + y * SCALE,
              SCALE, SCALE, color332(color));
}

void gfx_draw_line(int x1, int y1, int x2, int y2, u8 color)
{
    int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    int dy = y2 > y1 ? y2 - y1 : y1 - y2;
    int sx = x1 < x2 ? 1 : -1;
    int sy = y1 < y2 ? 1 : -1;
    int err = dx - dy;

    for (;;) {
        gfx_set_pixel(x1, y1, color);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        {
            int e2 = 2 * err;
            if (e2 > -dy) {
                err -= dy;
                x1 += sx;
            }
            if (e2 < dx) {
                err += dx;
                y1 += sy;
            }
        }
    }
}

void gfx_draw_rect(int x, int y, int w, int h, u8 color)
{
    if (w <= 0 || h <= 0) {
        return;
    }
    gfx_draw_line(x, y, x + w - 1, y, color);
    gfx_draw_line(x, y + h - 1, x + w - 1, y + h - 1, color);
    gfx_draw_line(x, y, x, y + h - 1, color);
    gfx_draw_line(x + w - 1, y, x + w - 1, y + h - 1, color);
}

void gfx_fill_rect(int x, int y, int w, int h, u8 color)
{
    int i, j;

    if (!gfx_on || w <= 0 || h <= 0) {
        return;
    }
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            gfx_set_pixel(x + i, y + j, color);
        }
    }
}

static void circle_points(int cx, int cy, int x, int y, u8 color)
{
    gfx_set_pixel(cx + x, cy + y, color);
    gfx_set_pixel(cx - x, cy + y, color);
    gfx_set_pixel(cx + x, cy - y, color);
    gfx_set_pixel(cx - x, cy - y, color);
    gfx_set_pixel(cx + y, cy + x, color);
    gfx_set_pixel(cx - y, cy + x, color);
    gfx_set_pixel(cx + y, cy - x, color);
    gfx_set_pixel(cx - y, cy - x, color);
}

void gfx_draw_circle(int cx, int cy, int r, u8 color)
{
    int x = 0;
    int y = r;
    int d = 3 - 2 * r;

    if (!gfx_on || r < 0) {
        return;
    }
    while (x <= y) {
        circle_points(cx, cy, x, y, color);
        if (d < 0) {
            d += 4 * x + 6;
        } else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

void gfx_fill_circle(int cx, int cy, int r, u8 color)
{
    int dy;

    if (!gfx_on || r < 0) {
        return;
    }
    for (dy = -r; dy <= r; dy++) {
        int dx = 0;
        while ((dx + 1) * (dx + 1) + dy * dy <= r * r) {
            dx++;
        }
        gfx_draw_line(cx - dx, cy + dy, cx + dx, cy + dy, color);
    }
}

void gfx_draw_char(int x, int y, char ch, u8 color)
{
    const unsigned char *glyph;
    u8 c = (u8)ch;
    int row, col;

    if (!gfx_on) {
        return;
    }
    if (c >= 128) {
        c = '?';
    }
    glyph = font8x8[c];
    for (row = 0; row < 8; row++) {
        u8 bits = glyph[row];
        for (col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                gfx_set_pixel(x + col, y + row, color);
            }
        }
    }
}

void gfx_draw_text(int x, int y, const char *text, u8 color)
{
    int start_x = x;

    if (!gfx_on) {
        return;
    }
    while (*text) {
        if (*text == '\n') {
            x = start_x;
            y += 8;
        } else if (*text == '\t') {
            x = (x + 32) & ~31;
        } else {
            gfx_draw_char(x, y, *text, color);
            x += 8;
        }
        text++;
    }
}

void gfx_wait_vsync(void)
{
    /* The VBE linear framebuffer has no cheap vsync register; the demo
       only calls this once, so a no-op is fine. */
}
