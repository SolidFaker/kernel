#ifndef GFX_H
#define GFX_H

/* Graphics ABI shared by the kernel and user programs.
   One syscall (NR_gfx) takes a pointer to struct gfx_req and dispatches
   on `op`. Coordinates are in pixels, colors are 3-3-2 palette indices. */

#define GFX_WIDTH   320
#define GFX_HEIGHT  200

#define GFX_ENTER        0   /* switch to 320x200x256 mode */
#define GFX_LEAVE        1   /* restore 80x25 text mode */
#define GFX_CLEAR        2   /* color */
#define GFX_PIXEL        3   /* x, y, color */
#define GFX_LINE         4   /* x, y, x2, y2, color */
#define GFX_RECT         5   /* x, y, w, h, color (outline) */
#define GFX_FILL_RECT    6   /* x, y, w, h, color */
#define GFX_CIRCLE       7   /* x, y (centre), w (radius), color */
#define GFX_FILL_CIRCLE  8   /* x, y (centre), w (radius), color */
#define GFX_CHAR         9   /* x, y, ch, color */
#define GFX_TEXT        10   /* x, y, text, color */
#define GFX_VSYNC       11   /* wait for vertical retrace */

struct gfx_req {
    unsigned int op;
    unsigned int x;
    unsigned int y;
    unsigned int x2;
    unsigned int y2;
    unsigned int w;
    unsigned int h;
    unsigned int color;
    unsigned int ch;
    const char *text;
};

/* Pack 8-bit RGB into a 3-3-2 palette index. */
#define GFX_RGB(r, g, b) \
    ((((r) & 0xE0)) | ((((g) & 0xE0)) >> 3) | ((((b) & 0xC0)) >> 6))

#define GFX_BLACK   GFX_RGB(0, 0, 0)
#define GFX_WHITE   GFX_RGB(255, 255, 255)
#define GFX_RED     GFX_RGB(255, 0, 0)
#define GFX_GREEN   GFX_RGB(0, 255, 0)
#define GFX_BLUE    GFX_RGB(0, 0, 255)
#define GFX_YELLOW  GFX_RGB(255, 255, 0)
#define GFX_CYAN    GFX_RGB(0, 255, 255)
#define GFX_MAGENTA GFX_RGB(255, 0, 255)

#endif
