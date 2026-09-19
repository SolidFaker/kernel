#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "fs.h"
#include "mm.h"
#include "page.h"
#include "string.h"
#include "tools.h"
#include "task.h"
#include "init.h"
#include "vfs.h"

typedef int (*sysc_func) (struct trap_frame *r);

static void syscall_handler(struct trap_frame *frame);

static sysc_func syscalls[SYSCALL_NUM];

int fork()
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (1));
    return a;
}

int print_hex(u32 hex)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (2), "b"(hex));
    return a;
}

int print_str(const char *str)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (0), "b"(str));
    return a;
}

static int sys_exec(struct trap_frame *frame)
{
    int i = 0;
    struct fs_node *fs_root = (struct fs_node *)(frame->ecx);
    struct dirent *node = 0;
    if (fs_root == 0) {
        return -1;
    }
    while ( (node = readdir_fs(fs_root, i)) != 0)
    {
        struct fs_node *fsnode = finddir_fs(fs_root, node->name);
        if (fsnode && (fsnode->flags & 0x7) == FS_FILE){
            char buf[256];
            read_fs(fsnode, 0, sizeof(buf) - 1, (u8 *)buf);
            buf[sizeof(buf) - 1] = '\0';
        }
        i++;
    }
    return 0;
}

static int sys_fork(struct trap_frame *frame)
{
    if (current->user_stack == NULL) {
        return -1;  // kernel threads have no user space to duplicate
    }

    struct task_struct *new_task = alloc_task();
    new_task->context->esp -= FRAME_SIZE;
    new_task->frame = (struct trap_frame *)(new_task->context->esp);
    *new_task->frame = *frame;
    new_task->frame->eax = 0;

    // The child gets a private copy of the parent's user stack and keeps
    // running at the same stack offset, so locals and return addresses
    // survive the fork.
    memcpy(new_task->user_stack, current->user_stack, USER_STACK_SIZE);
    u32 parent_top = (u32)current->user_stack + USER_STACK_SIZE;
    if (frame->esp >= (u32)current->user_stack && frame->esp < parent_top) {
        new_task->frame->esp =
            (u32)new_task->user_stack + (frame->esp - (u32)current->user_stack);
    } else {
        new_task->frame->esp = (u32)new_task->user_stack + USER_STACK_SIZE;
    }
    if (frame->ebp >= (u32)current->user_stack && frame->ebp < parent_top) {
        new_task->frame->ebp =
            (u32)new_task->user_stack + (frame->ebp - (u32)current->user_stack);
    }

    new_task->context->eip = (u32)int_ret_stub;
    new_task->state = RUNNABLE;
    return new_task->pid;
}

static int sys_print_hex(struct trap_frame *frame)
{
    u32 hex = (u32)(frame->ebx);
    display_print_hex(hex);
    return 0;
}

static int sys_print(struct trap_frame *frame)
{
    char *str = (char *)(frame->ebx);
    // Reject null/near-null pointers. Full user/kernel validation needs real
    // user space: user code currently lives in kernel .rodata, so its string
    // literals legitimately sit above PAGE_OFFSET.
    if ((u32)str < 0x1000) {
        return -1;
    }
    display_print(str);
    return 0;
}

int nosys(struct trap_frame *frame) {
    printk("SYSCALL NO.%d DOSE NOT EXIST.\n", frame->eax);
    return -1;
}

void init_syscall()
{
    // Register our syscall handler.
    register_interrupt_handler (0x80, &syscall_handler);
    syscalls[0] = &sys_print;
    syscalls[1] = &sys_fork;
    syscalls[2] = &sys_print_hex;
    syscalls[3] = &sys_exec;
}

void syscall_handler(struct trap_frame *frame)
{
    u32 ret;
    sysc_func func = 0;

    // Check if the requested syscall number is valid.
    // The syscall number is found in EAX.
    if (frame->eax >= SYSCALL_NUM) {
        PANIC("bad syscall");
    }
    // Get the required syscall location.
    func = syscalls[frame->eax];
    if (func == NULL){
        func = &nosys;
    }
    // Enter syscall
    ret = (*func)(frame);
    frame->eax = ret;
}
