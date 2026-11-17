#ifndef DRIVERS_TTY_H
#define DRIVERS_TTY_H

#include "common.h"
#include "posix_abi.h"

#define TTY_NUMBER      4

#define TTY_COLS        76
#define TTY_ROWS        56
#define TTY_BUFFER_SIZE (TTY_COLS * TTY_ROWS * 2)

struct tty {
    u32 offset;                 /* unused, kept for ABI compatibility */
    u8 cursor_x;
    u8 cursor_y;
    volatile u16 *buffer;       /* TTY_COLS * TTY_ROWS cells (char | attr<<8) */
    struct termios termios;
    u32 pgrp;
};

extern struct tty *tty_cur;
extern struct tty tty[TTY_NUMBER];

void switch_tty(struct tty *tty);
void tty_restore_display(void);
void init_tty();
u32 task_tty(__UNUSED__ void *arg);

#endif
