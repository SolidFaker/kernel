#ifndef TEST_H
#define TEST_H

#include "common.h"
#include "drivers/timer.h"
#include "fs.h"
#include "mm.h"
#include "syscall.h"
#include "task.h"
#include "tools.h"
#include "vfs.h"

static u32 test_a(__UNUSED__ void *arg)
{
    // list the contents of /
    int i = 0;
    struct dirent *node = 0;
    while ( (node = readdir_fs(fs_root, i)) != 0)
    {
        display_print("==============================\n");
        display_print("Found file ");
        display_print(node->name);
        struct fs_node *fsnode = finddir_fs(fs_root, node->name);
        if ((fsnode->flags&0x7) == FS_DIRECTORY){
            display_print("\t(directory)\n");
        } else {
            char buf[256];
            u32 sz = read_fs(fsnode, 0, sizeof(buf) - 1, (u8 *)buf);
            buf[sz] = '\0';
            printk("\tlength:%u\n", sz);
            display_print("------------------------------\n");
            printk("%s\n", buf);
        }
        i++;
        display_print("==============================\n");
    }
    return 0;
}

static u32 test_d(__UNUSED__ void *args)
{
    // Switch kernel stack to user stack
    // this is only for test
    asm volatile ("movl %0, %%ebp"::"r"((u32)current->user_stack));
    asm volatile ("movl %0, %%esp"::"r"((u32)current->user_stack + USER_STACK_SIZE));

    int a = 0;
    switch_to_user_mode();
    a = fork();
    if (a == 0){
        print_str("child running ...\n");
    } else {
        print_hex(a);
        print_str("\nparent running ...\n");
    }
    while(1);

    return 0;
}

static u32 test_c(__UNUSED__ void *arg)
{
    u32 t = 0;
    while(1){
        t = tick + 10;
        display_print_color(COLOR_BLACK, COLOR_RED, "C");
        while(tick < t){
        }
    }
    return 0;
}

static u32 test_b(__UNUSED__ void *arg)
{
    u32 t = 0;
    while(1){
        t = tick + 10;
        display_print_color(COLOR_BLACK, COLOR_GREEN, "B");
        while(tick < t){
        }
    }
    return 0;
}

#endif /* ifndef TEST_H */
