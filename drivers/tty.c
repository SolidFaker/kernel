#include "drivers/tty.h"
#include "drivers/keyboard.h"
#include "drivers/display.h"
#include "drivers/fbcon.h"
#include "string.h"
#include "signal.h"

struct tty *tty_cur;

struct tty tty[TTY_NUMBER];

static u16 tty_cells[TTY_NUMBER][TTY_COLS * TTY_ROWS];

void init_tty()
{
    u8 i;

    for (i = 0; i < TTY_NUMBER; i++) {
        volatile u8 *cc = tty[i].termios.c_cc;
        tty[i].offset = 0;
        tty[i].buffer = tty_cells[i];
        tty[i].cursor_x = 0;
        tty[i].cursor_y = 0;
        memset((u8 *)&tty[i].termios, 0, sizeof(tty[i].termios));
        tty[i].termios.c_iflag = ICRNL;
        tty[i].termios.c_oflag = OPOST;
        tty[i].termios.c_lflag = ISIG | ICANON | ECHO | ECHOE;
        cc[VINTR] = 3;
        cc[VQUIT] = 28;
        cc[VERASE] = 8;
        cc[VKILL] = 21;
        cc[VEOF] = 4;
        cc[VSTART] = 17;
        cc[VSTOP] = 19;
        cc[VSUSP] = 26;
        cc[VMIN] = 1;
        cc[VTIME] = 0;
        *(volatile u32 *)&tty[i].termios.c_ispeed = 38400;
        *(volatile u32 *)&tty[i].termios.c_ospeed = 38400;
        tty[i].pgrp = 0;
        tty[i].in_len = 0;
        tty[i].in_pos = 0;
        {
            volatile u16 *cells = tty[i].buffer;
            int remaining = TTY_COLS * TTY_ROWS;
            while (remaining-- > 0) {
                *cells++ = 0x20 | (0x07 << 8);
            }
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

int tty_input_ready(struct tty *t)
{
    return t && (t->in_pos < t->in_len || kbd_haschar());
}

int tty_handle_control(struct tty *t, u8 c)
{
    int sig = 0;
    char echo[3];

    if (!t || !(t->termios.c_lflag & ISIG)) {
        return 0;
    }
    if (c == t->termios.c_cc[VINTR]) {
        sig = SIGINT;
    } else if (c == t->termios.c_cc[VQUIT]) {
        sig = SIGQUIT;
    }
    if (!sig) {
        return 0;
    }
    t->in_len = 0;
    t->in_pos = 0;
    if (t->termios.c_lflag & ECHO) {
        echo[0] = '^';
        echo[1] = (char)('@' + c);
        echo[2] = '\n';
        display_write(t, echo, sizeof(echo));
    }
    if (t->pgrp) {
        signal_kill(-(int)t->pgrp, sig);
    }
    return 1;
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
