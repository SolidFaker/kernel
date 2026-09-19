#include "common.h"
#include "init.h"
#include "tools.h"
#include "mm.h"
#include "task.h"
#include "page.h"
#include "syscall.h"
#include "vfs.h"
#include "fs/myfs.h"
#include "drivers/display.h"
#include "drivers/timer.h"
#include "drivers/keyboard.h"
#include "drivers/tty.h"

#include "test.h" // kernel test function

static inline void init_stack();
static void print_info();

void kernel_start(void)
{
    // kernel state initialising
    // no print function here
    init_desc();
    init_stack();

    // print function initialized
    init_tty();

    // memory management initialized
    init_page();
    init_page_stack();

    // multi task init
    init_task();

    // filesystem init
    fs_root = init_vfs();

    // initialises system call
    init_syscall();

    // interrupt handler registed
    init_keyboard();
    init_timer(2000);     // schedule() here

    flush_screen(tty_cur);

    // print system info
    print_info();

    kthread_start(task_idle, &tty[0], 1, NULL);
    kthread_start(task_tty, &tty[0], 1, NULL);
    kthread_start(test_a, &tty[1], 2, NULL);
    kthread_start(test_b, &tty[2], 1, NULL);
    kthread_start(test_c, &tty[2], 3, NULL);
    kthread_start(test_d, &tty[3], 4, NULL);

    // enable interrupt
    sti();

    printk("running ...\n");
    while(1) {
        printk("%d\r", tick);
    }

    hlt();
}

static inline void init_stack()
{
    // use new stack space; one asm block so the input setup cannot be
    // interleaved with the stack switch
    asm volatile (
        "movw $0x10, %%ax\n\t"
        "movw %%ax, %%ss\n\t"
        "movl %0, %%ebp\n\t"
        "movl %1, %%esp\n\t"
        :
        : "r" ((u32)kernel_stack),
          "r" ((u32)kernel_stack + sizeof(kernel_stack))
        : "eax", "memory");
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


