#include "drivers/graphic.h"
#include "errno.h"

int gfx_enter(void) { return -ENOSYS; }
void gfx_leave(void) { }
int gfx_is_active(void) { return 0; }
void gfx_clear(u8 color) { (void)color; }
void gfx_set_pixel(int x, int y, u8 color) { (void)x; (void)y; (void)color; }
void gfx_draw_line(int x1, int y1, int x2, int y2, u8 color)
{
    (void)x1; (void)y1; (void)x2; (void)y2; (void)color;
}
void gfx_draw_rect(int x, int y, int w, int h, u8 color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void gfx_fill_rect(int x, int y, int w, int h, u8 color)
{
    (void)x; (void)y; (void)w; (void)h; (void)color;
}
void gfx_draw_circle(int cx, int cy, int r, u8 color)
{
    (void)cx; (void)cy; (void)r; (void)color;
}
void gfx_fill_circle(int cx, int cy, int r, u8 color)
{
    (void)cx; (void)cy; (void)r; (void)color;
}
void gfx_draw_char(int x, int y, char ch, u8 color)
{
    (void)x; (void)y; (void)ch; (void)color;
}
void gfx_draw_text(int x, int y, const char *text, u8 color)
{
    (void)x; (void)y; (void)text; (void)color;
}
void gfx_wait_vsync(void) { }
