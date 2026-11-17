#include "drivers/tty.h"
#include "drivers/keyboard.h"
#include "drivers/display.h"
#include "drivers/fbcon.h"
#include "string.h"

struct tty *tty_cur;

struct tty tty[TTY_NUMBER];

static u16 tty_cells[TTY_NUMBER][TTY_COLS * TTY_ROWS];

void init_tty()
{
    u8 i;
    u32 j;

    for (i = 0; i < TTY_NUMBER; i++) {
        tty[i].offset = 0;
        tty[i].buffer = tty_cells[i];
        tty[i].cursor_x = 0;
        tty[i].cursor_y = 0;
        memset((u8 *)&tty[i].termios, 0, sizeof(tty[i].termios));
        tty[i].termios.c_iflag = ICRNL;
        tty[i].termios.c_oflag = OPOST;
        tty[i].termios.c_lflag = ISIG | ICANON | ECHO | ECHOE;
        tty[i].termios.c_cc[VMIN] = 1;
        tty[i].termios.c_cc[VTIME] = 0;
        tty[i].termios.c_ispeed = 38400;
        tty[i].termios.c_ospeed = 38400;
        tty[i].pgrp = 0;
        for (j = 0; j < TTY_COLS * TTY_ROWS; j++) {
            tty[i].buffer[j] = 0x20 | (0x07 << 8);
        }
    }

    tty_print = &tty[0];
    tty_cur = &tty[0];
}

u32 task_tty(__UNUSED__ void *arg)
{
    while(1){
        // Switch tty
        if (is_controls_pressed(ALT) && !is_controls_pressed(CONTROL)){
            switch(pressed_key()){
                case '1':
                    switch_tty(&tty[0]);
                    break;
                case '2':
                    switch_tty(&tty[1]);
                    break;
                case '3':
                    switch_tty(&tty[2]);
                    break;
                case '4':
                    switch_tty(&tty[3]);
                    break;
            }
        }
    }
    return 0;
}

void switch_tty(struct tty *tty)
{
    if (tty_cur == tty) {
        return;
    }
    tty_cur = tty;
    fbcon_select(tty);
}

void tty_restore_display(void)
{
    fbcon_redraw_all();
}
