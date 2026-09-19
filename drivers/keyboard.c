#include "init.h"
#include "drivers/keyboard.h"
#include "tools.h"
#include "drivers/tty.h"
#include "common.h"

static struct keymap us_keymap = {
    //normal keys
    {
        /* first row - indices 0 to 14 */
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        /* second row - indices 15 to 28 */
        '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', //Enter key
        /* 29 = Control, 30 - 41: third row */
        0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
        /* fourth row, indices 42 to 54, zeroes are shift-keys*/
        0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
        '*',
        /* Special keys */
        0,    // ALT - 56
        ' ', // Space - 57
        0,    // Caps lock - 58
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1 to F10 - 59 to 68
        0, // Num lock - 69
        0, // Scroll lock - 70
        0, // Home - 71
        0, // Up arrow - 72 (extended key, arrives with 0xE0 prefix)
        0, // Page up - 73
        '-',
        0, // Left arrow - 75
        0,
        0, // Right arrow -77
        '+',
        0, // End - 79
        0, // Down arrow - 80 (extended key, arrives with 0xE0 prefix)
        0, // Page down - 81
        0, // Insert - 82
        0, // Delete - 83
        0, 0, 0,
        0, // F11 - 87
        0, // F12 - 88
        0, // All others undefined
    },
    // caps
    {
        /* first row - indices 0 to 14 */
        0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
        /* second row - indices 15 to 28 */
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        /* 29 = Control, 30 - 41: third row */
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ';', '\'', '`',
        /* fourth row, indices 42 to 54, zeroes are shift-keys*/
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '/', 0, '*',
        /* Special keys */
        0,   // ALT - 56
        ' ', // Space - 57
        0,   // Caps lock - 58
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1 to F10 - 59 to 68
        0, // Num lock - 69
        0, // Scroll lock - 70
        0, // Home - 71
        0, // Up arrow - 72
        0, // Page up - 73
        '-',
        0, // Left arrow - 75
        0,
        0, // Right arrow -77
        '+',
        0, // End - 79
        0, // Dowm arrow - 80
        0, // Page down - 81
        0, // Insert - 82
        0, // Delete - 83
        0, 0, 0,
        0, // F11 - 87
        0, // F12 - 88
        0, // All others undefined
    },
    // shift
    {
        /* first row - indices 0 to 14 */
        0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
        /* second row - indices 15 to 28 */
        '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
        /* 29 = Control, 30 - 41: third row */
        0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
        /* fourth row, indices 42 to 54, zeroes are shift-keys*/
        0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0, '*',
        /* Special keys */
        0,   // ALT - 56
        ' ', // Space - 57
        0,   // Caps lock - 58
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, // F1 to F10 - 59 to 68
        0, // Num lock - 69
        0, // Scroll lock - 70
        0, // Home - 71
        0, // Up arrow - 72
        0, // Page up - 73
        '-',
        0, // Left arrow - 75
        0,
        0, // Right arrow -77
        '+',
        0, // End - 79
        0, // Dowm arrow - 80
        0, // Page down - 81
        0, // Insert - 82
        0, // Delete - 83
        0, 0, 0,
        0, // F11 - 87
        0, // F12 - 88
        0, // All others undefined
    },
    // control_map
    {
        29, // Ctrl
        56, // Alt
        0,  // AltGr
        42, // left Shift
        54, // right Shift
        58, // Caps lock
        70, // Scroll lock
        69  // Num lock
    },
    // 键盘的控制键信息初始化为 0 
    0
};

u8 scancode; // keyboard buffer
u8 pressed = 0;
static u8 ext_scan = 0; // set after a 0xE0 extended-key prefix
struct keymap *default_layout = &us_keymap;
u8 *scancodes;

// character queue consumed by the read() syscall (the shell)
#define KBD_BUF_SIZE 128
static char kbd_buf[KBD_BUF_SIZE];
static u32 kbd_head = 0;
static u32 kbd_tail = 0;

static void kbd_putc(char c)
{
    u32 next = (kbd_head + 1) % KBD_BUF_SIZE;
    if (next == kbd_tail) {
        return; // full: drop the key
    }
    kbd_buf[kbd_head] = c;
    kbd_head = next;
}

int kbd_getchar(void)
{
    if (kbd_tail == kbd_head) {
        return -1;
    }
    char c = kbd_buf[kbd_tail];
    kbd_tail = (kbd_tail + 1) % KBD_BUF_SIZE;
    return (u8)c;
}

static inline void switch_scancodes(){
    // If shift pressed, switch to shift code layout
    if ((default_layout->controls & (LSHIFT | RSHIFT))
            && !(default_layout->controls & CONTROL)) 
    {
        scancodes = default_layout->shift_scancodes;
    }
    // If Caps Lock on , switch layout to upper case
    if ((default_layout->controls & (CAPSLOCK)) 
            && !(default_layout->controls & CONTROL)) 
    {
        scancodes = default_layout->capslock_scancodes;
    }
}

static inline void set_control_code(){
    u32 i;
    // Check control set
    for (i = 0; i < 8; i++) {
        // If pressed is control key set control bit to 1
        // otherwise set to 0
        if (default_layout->control_map[i] == scancode) {
            if (default_layout->controls & 1 << i) {
                default_layout->controls &= ~(1 << i);
            } else {
                default_layout->controls |= (1 << i);
            }
            return;
        }
    }
}

static inline void reset_control_code(){
    u32 i;
    // Clean control state
    for (i = 0; i < 5; i++) {
        if(default_layout->control_map[i] == (scancode & ~RELEASED_MASK)) {
            default_layout->controls &= ~(1 << i);
            return;
        }
    }
}

void keyboard_callback()
{
    scancode = inb(0x60);

    // Extended keys (arrows, Home...) send a 0xE0 prefix byte first.
    // They have no printable character, so swallow the two-byte sequence
    // instead of indexing the keymap with the prefix byte.
    if (ext_scan) {
        ext_scan = 0;
        pressed = 0;
        return;
    }
    if (scancode == 0xE0) {
        ext_scan = 1;
        return;
    }

    scancodes = default_layout->scancodes;
    // keyboard handler
    // scancode & RELEASED_MASK means the key has been break
    if (scancode & RELEASED_MASK) {
        // key released
        pressed = 0;
        reset_control_code();
        return;
    }
    pressed = 1;
    set_control_code();
    // control combos (alt+digit tty switching etc.) are not console input
    if (is_controls_pressed(CONTROL) || is_controls_pressed(ALT)) {
        return;
    }
    // capslock & shift mode
    switch_scancodes();
    // queue the character for read(); echoing is done by the consumer
    char c = (char)scancodes[scancode];
    if (c) {
        kbd_putc(c);
    }
}

// return 1 if key is pressed, 0 if not
u8 is_controls_pressed(u8 controls_key)
{
    if(default_layout->controls & (controls_key)){
        return 1;
    }
    return 0;
}

u8 pressed_key()
{
    return scancodes[scancode];
}

// return 1 if key is pressed, 0 if not
u8 is_pressed(u8 keycode)
{
    if (pressed && keycode == scancodes[scancode]){
        return 1;
    }
    return 0;
}

void init_keyboard()
{
    // register keyboard interrupt handler
    register_interrupt_handler(IRQ1, (interrupt_handler_t)&keyboard_callback);
    // enable keyboard interrupt
    irq_enable(1);
    scancodes = default_layout->scancodes;
}
