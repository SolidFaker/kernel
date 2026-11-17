#ifndef DRIVERS_GRAPHIC_H
#define DRIVERS_GRAPHIC_H

#include "common.h"

#define GFX_VGA_MEM 0xC00A0000   /* kernel alias of physical 0xA0000 */

int  gfx_enter(void);
void gfx_leave(void);
int  gfx_is_active(void);

void gfx_clear(u8 color);
void gfx_set_pixel(int x, int y, u8 color);
void gfx_draw_line(int x1, int y1, int x2, int y2, u8 color);
void gfx_draw_rect(int x, int y, int w, int h, u8 color);
void gfx_fill_rect(int x, int y, int w, int h, u8 color);
void gfx_draw_circle(int cx, int cy, int r, u8 color);
void gfx_fill_circle(int cx, int cy, int r, u8 color);
void gfx_draw_char(int x, int y, char ch, u8 color);
void gfx_draw_text(int x, int y, const char *text, u8 color);
void gfx_wait_vsync(void);

#endif
