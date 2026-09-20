#ifndef DRIVERS_DISPLAY_H
#define DRIVERS_DISPLAY_H

#include "common.h"

#define COLOR_BLACK  0
#define COLOR_BLUE  1
#define COLOR_GREEN  2
#define COLOR_CYAN  3
#define COLOR_RED  4
#define COLOR_MAGENTA  5
#define COLOR_BROWN  6
#define COLOR_LIGHT_GREY  7
#define COLOR_DARK_GREY  8
#define COLOR_LIGHT_BLUE  9
#define COLOR_LIGHT_GREEN  10
#define COLOR_LIGHT_CYAN  11
#define COLOR_LIGHT_RED  12
#define COLOR_LIGHT_MAGENTA  13
#define COLOR_LIGHT_BROWN  14
#define COLOR_WHITE  15

#define CRT_ADDR_REG 0x3D4   // CRT controller register - Address Register
#define CRT_DATA_REG 0x3D5   // CRT controller register - Date Register
#define CURSOR_START 0xA     // Cursor start location
#define CURSOR_END   0xB     // Cursor end location
#define VBUFFER_START_H 0xC  // Video Buffer Start High 8 bit
#define VBUFFER_START_L 0xD  // Video Buffer Start Low 8 bit
#define CURSOR_H 0xE         // Cursor high 8 bit
#define CURSOR_L 0xF         // Cursor low 8 bit
#define VBUFFER_MEM 0xC00B8000
#define VBUFFER_LEN 0x8000

extern struct tty *tty_print;

void flush_screen(struct tty *tty);
void flush_line(struct tty *tty, u8);
void display_print_color(u8 bg, u8 fg, const char *string);
void display_print(const char *string);
void display_write(struct tty *tty, const char *string, u32 len);
void display_putc(u8 bg, u8 fg, struct tty *tty, char c);
void display_print_hex(u32 num);

#endif
