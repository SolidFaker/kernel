#ifndef DRIVERS_FBCON_H
#define DRIVERS_FBCON_H

#include "common.h"

struct tty;

int  fbcon_init(void);
int  fbcon_ready(void);

void fbcon_select(struct tty *tty);
void fbcon_redraw_all(void);
void fbcon_draw_cell(struct tty *tty, u32 row, u32 col);
void fbcon_draw_row(struct tty *tty, u32 row);
void fbcon_scroll(struct tty *tty);
void fbcon_clear(struct tty *tty);
void fbcon_cursor_update(struct tty *tty);
void fbcon_blink(void);

#endif
