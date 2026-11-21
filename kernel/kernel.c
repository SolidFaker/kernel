#include "common.h"
#include "init.h"
#include "tools.h"
#include "mm.h"
#include "task.h"
#include "page.h"
#include "syscall.h"
#include "fs.h"
#include "vfs.h"
#include "fs/myfs.h"
#include "drivers/display.h"
#include "drivers/fbcon.h"
#include "net.h"
#include "jfs.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/tty.h"

extern void arch_init_stack(void);
static void print_info();

void kernel_start(void)
{
    // kernel state initialising
    // no print function here
    init_desc();
    arch_init_stack();

    // print function initialized
    init_tty();

    // memory management initialized
    init_page();
    init_page_stack();

    // e1000 NIC + ARP/IPv4/ICMP
    net_init();

    // graphical terminal (VBE linear framebuffer)
    fbcon_init();

    // multi task init
    init_task();

    // filesystem init: the journal snapshot is authoritative once created
    if (jfs_has_snapshot()) {
        fs_root = init_vfs_empty();
    } else {
        fs_root = init_vfs();
    }
    jfs_init();
    vfs_apply_policies();

    // initialises system call
    init_syscall();

    // interrupt handler registed
    init_keyboard();
    init_timer(TIMER_HZ);     // schedule() here

    // boot log and shell share tty 1, so the terminal shows the whole
    // startup sequence
    flush_screen(&tty[1]);
    tty_print = &tty[1];
    switch_tty(&tty[1]);

    // print system info
    print_info();

    kthread_start("idle", task_idle, &tty[0], 1, NULL);
    kthread_start("ttyd", task_tty, &tty[0], 1, NULL);
    kthread_start("init", task_init, &tty[1], 5, NULL);

    // enable interrupt
    sti();

    printk("running ...\n");
    while(1) {
        hlt();
    }

    hlt();
}

static void print_info()
{
    u8 i=0;
    mmap_entry_t *map_entry = mmap;

    printk("KERNEL LOADED\n");
    printk("KERNEL START: %x\n", kernel_start_pos);
    printk("KERNEL END:   %x\n", kernel_end_pos);
    printk("KERNEL SIZE:  %d kb\n", (kernel_end_pos - kernel_start_pos+1023)/1024);

    printk("----------MEMORY MAP----------\n");
    for (i = 0; i < *count; i++){
        // base/length are 64-bit values; print both halves
        printk("BASE: 0x%08x%08x\tLENGTH: 0x%08x%08x\tTYPE:0x%01X\n",
                (map_entry+i)->base_high,
                (map_entry+i)->base_low,
                (map_entry+i)->length_high,
                (map_entry+i)->length_low,
                (map_entry+i)->type);
    }
}


