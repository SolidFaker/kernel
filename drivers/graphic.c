// lib for grapich mode 
// Not used for now

#include "common.h"
#include "font.h"

volatile u8 *video_buffer = (volatile u8*)0xa0000;

static int abs(int a)
{
    int temp = (a >> 31); 
    return (a + temp) ^ temp;
}

void set_pixel(u16 x, u16 y, u8 color)
{
    video_buffer[y*320 +x] = color;
}

void print_font8(u16 x, u16 y, u8 ch)
{
    u16 i,j;
    for(i=x; i<x+8; i++){
        for(j=y; j<y+8; j++){
            if(((font8[ch][j-y]>>(i-x))&0x1) == 1)
                set_pixel(i,j,0xf);
        }
    }
}

void draw_line(u16 x1, u16 y1, u16 x2, u16 y2, u8 color)
{
    int dx = x2 - x1;
    int dy = y2 - y1;
    int ux = ((dx > 0) << 1) - 1;//x的增量方向，取或-1
    int uy = ((dy > 0) << 1) - 1;//y的增量方向，取或-1
    int x = x1, y = y1, eps;//eps为累加误差

    eps = 0;dx = abs(dx); dy = abs(dy); 
    if (dx > dy) {
        for (x = x1; x != x2; x += ux) {
            set_pixel(x,y,color);
            eps += dy;
            if ((eps << 1) >= dx) {
                y += uy; eps -= dx;
            }
        }
    } else {
        for (y = y1; y != y2; y += uy) {
            set_pixel(x,y,color);
            eps += dx;
            if ((eps << 1) >= dy) {
                x += ux; eps -= dy;
            }
        }
    }
}

void draw_rectangle(u16 x, u16 y, u16 width, u16 hight, u8 color)
{
    u16 i,j;
    for (i=x; i<x+width; i++){
        for (j=y; j<y+hight; j++){
            set_pixel(i,j,color);
        }
    }

}


