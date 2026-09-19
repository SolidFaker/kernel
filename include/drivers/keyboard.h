#ifndef DRIVERS_KEYBOARD_H
#define DRIVERS_KEYBOARD_H

#include "common.h"

#define CONTROL         0x1
#define ALT             0x2
#define ALTGR           0x4
#define LSHIFT          0x8
#define RSHIFT          0x10
#define CAPSLOCK        0x20
#define SCROLLLOCK      0x40
#define NUMLOCK         0x80

// Key Break Mask
#define RELEASED_MASK 0x80

//extern u8 *scancodes;
//extern u8 scancode;
extern u8 pressed;

struct keymap {
    u8 scancodes[128];
    u8 capslock_scancodes[128];
    u8 shift_scancodes[128];
    u8 control_map[8];
    u8 controls;
};

void init_keyboard();
u8 is_controls_pressed(u8 controls_key);
u8 pressed_key();
u8 is_pressed(u8 keycode);
int kbd_getchar(void);

#endif
