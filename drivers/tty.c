#include "drivers/tty.h"
#include "drivers/keyboard.h"
#include "drivers/display.h"

struct tty *tty_cur;

struct tty tty[TTY_NUMBER];

static void set_video_start_addr(u32 addr);

void set_title_bar(struct tty *tty, u8 tty_num)
{
    u16 attribute = (u16)((COLOR_BLUE | COLOR_LIGHT_GREY << 4) << 8);
    volatile u16 *buffer = tty->buffer;
    u8 i;
    u8 title[6] = {'T', 'T', 'Y', ' ', (u8)(tty_num + '0'), 0};
    for (i=0; i<80; i++) {
        if (i<6) {
            buffer[i] = title[i]  | attribute;
        } else {
            buffer[i] = (u8)' ' | attribute;
        }
    }
}

void init_tty()
{
    u8 i;
    for (i=0; i<TTY_NUMBER; i++ ){
        tty[i].offset = TTY_BUFFER_SIZE * i;
        tty[i].buffer = (volatile u16 *)(VBUFFER_MEM + tty[i].offset);
        tty[i].cursor_x = 0;
        tty[i].cursor_y = 1;
        set_title_bar(&tty[i], i);
    }
    tty_print = &tty[0];
    switch_tty(tty_print);
    tty_cur = tty_print;
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
// Switch video buffer start address
// using io function write registers to set VGA memory start address
// Because of every char on the screen takes 2 bytes 
// So the start address must be half of offset addr
void switch_tty(struct tty *tty)
{
    if (tty_cur == tty){ return; }
    tty_cur = tty;
    set_video_start_addr(tty->offset >> 1);
}

static void set_video_start_addr(u32 addr)
{
    u32 flags = local_irq_save();
    outb(CRT_ADDR_REG, VBUFFER_START_H);
    outb(CRT_DATA_REG, (addr >> 8) & 0xFF);
    outb(CRT_ADDR_REG, VBUFFER_START_L);
    outb(CRT_DATA_REG, addr & 0xFF);
    local_irq_restore(flags);
}
