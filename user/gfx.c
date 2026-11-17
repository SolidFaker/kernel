// user/gfx.c -- VGA 320x200x256 graphics demo.
// Run it from the shell with `gfx` (or `run gfx.elf`).

#include "unistd.h"
#include "gfx.h"

static int read(int fd, void *buf, unsigned int len)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_read), "b" (fd), "c" (buf), "d" (len)
                     : "memory");
    return a;
}

static int gfx_call(struct gfx_req *req)
{
    int a;
    __asm__ volatile("int $0x80" : "=a" (a)
                     : "0" (NR_gfx), "b" (req) : "memory");
    return a;
}

static void exit(int status)
{
    __asm__ volatile("int $0x80" : : "a" (NR_exit), "b" (status) : "memory");
}

static void clear(unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_CLEAR;
    r.color = color;
    gfx_call(&r);
}

static void line(int x1, int y1, int x2, int y2, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_LINE;
    r.x = (unsigned int)x1;
    r.y = (unsigned int)y1;
    r.x2 = (unsigned int)x2;
    r.y2 = (unsigned int)y2;
    r.color = color;
    gfx_call(&r);
}

static void rect(int x, int y, int w, int h, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_RECT;
    r.x = (unsigned int)x;
    r.y = (unsigned int)y;
    r.w = (unsigned int)w;
    r.h = (unsigned int)h;
    r.color = color;
    gfx_call(&r);
}

static void fill_rect(int x, int y, int w, int h, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_FILL_RECT;
    r.x = (unsigned int)x;
    r.y = (unsigned int)y;
    r.w = (unsigned int)w;
    r.h = (unsigned int)h;
    r.color = color;
    gfx_call(&r);
}

static void circle(int cx, int cy, int radius, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_CIRCLE;
    r.x = (unsigned int)cx;
    r.y = (unsigned int)cy;
    r.w = (unsigned int)radius;
    r.color = color;
    gfx_call(&r);
}

static void fill_circle(int cx, int cy, int radius, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_FILL_CIRCLE;
    r.x = (unsigned int)cx;
    r.y = (unsigned int)cy;
    r.w = (unsigned int)radius;
    r.color = color;
    gfx_call(&r);
}

static void text(int x, int y, const char *s, unsigned int color)
{
    struct gfx_req r;
    r.op = GFX_TEXT;
    r.x = (unsigned int)x;
    r.y = (unsigned int)y;
    r.color = color;
    r.text = s;
    gfx_call(&r);
}

static void wait_vsync(void)
{
    struct gfx_req r;
    r.op = GFX_VSYNC;
    gfx_call(&r);
}

static void draw_scene(void)
{
    static const unsigned int bars[16] = {
        GFX_RGB(0, 0, 0),     GFX_RGB(128, 0, 0),
        GFX_RGB(0, 128, 0),   GFX_RGB(128, 128, 0),
        GFX_RGB(0, 0, 128),   GFX_RGB(128, 0, 128),
        GFX_RGB(0, 128, 128), GFX_RGB(192, 192, 192),
        GFX_RGB(128, 128, 128), GFX_RGB(255, 0, 0),
        GFX_RGB(0, 255, 0),   GFX_RGB(255, 255, 0),
        GFX_RGB(0, 0, 255),   GFX_RGB(255, 0, 255),
        GFX_RGB(0, 255, 255), GFX_RGB(255, 255, 255)
    };
    int i;

    clear(GFX_RGB(0, 0, 64));

    for (i = 0; i < 16; i++) {
        fill_rect(i * 20, 0, 20, 14, bars[i]);
    }

    text(12, 22, "A-MINI-KERNEL", GFX_WHITE);
    text(12, 38, "VGA 320x200x256", GFX_CYAN);

    fill_circle(258, 44, 22, GFX_RGB(255, 224, 0));
    circle(258, 44, 30, GFX_RGB(255, 160, 0));

    rect(12, 62, 140, 58, GFX_RGB(200, 200, 200));
    fill_rect(20, 70, 38, 40, GFX_RGB(224, 32, 32));
    fill_rect(66, 70, 38, 40, GFX_RGB(32, 224, 32));
    fill_rect(112, 70, 32, 40, GFX_RGB(64, 96, 255));

    circle(285, 140, 10, GFX_RGB(255, 255, 0));
    circle(285, 140, 18, GFX_RGB(255, 128, 0));
    circle(285, 140, 26, GFX_RGB(255, 0, 0));

    line(12, 132, 220, 132, GFX_RGB(64, 255, 64));
    line(220, 176, 308, 196, GFX_RGB(64, 255, 64));
    line(308, 176, 220, 196, GFX_RGB(64, 255, 64));

    text(12, 140, "lines / rects / circles / text", GFX_WHITE);
    text(12, 158, "press any key to return", GFX_YELLOW);
}

void _start(void)
{
    struct gfx_req r;
    char c;

    r.op = GFX_ENTER;
    if (gfx_call(&r) < 0) {
        return;
    }

    draw_scene();
    wait_vsync();

    read(0, &c, 1);

    r.op = GFX_LEAVE;
    gfx_call(&r);

    exit(0);
}
