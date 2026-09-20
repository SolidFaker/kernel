#include "task.h"
#include "mm.h"
#include "tools.h"
#include "string.h"
#include "common.h"
#include "debug.h"
#include "drivers/timer.h"
#include "syscall.h"

extern void enter_user_mode();
u32 pid_now = 0;

struct task_list *running_task_head = NULL;
struct task_struct *current = NULL;

// Install fd 0/1/2 on the keyboard and the task's tty.
void fds_init_std(struct task_struct *task)
{
    task->fds[0].used = 1;
    task->fds[0].kind = FD_TTY_IN;
    task->fds[1].used = 1;
    task->fds[1].kind = FD_TTY_OUT;
    task->fds[2].used = 1;
    task->fds[2].kind = FD_TTY_OUT;
}

// System idle task
// save power
u32 task_idle(__UNUSED__ void *arg)
{
    while (1){
        hlt();
    }
    return 0;
}

// Boot-time launcher for the user world: drops to CPL3 on the per-task
// user stack, then execs the shell. It runs only for a moment - the
// exec replaces this task with shell.elf, and this code never runs again.
u32 task_init(__UNUSED__ void *arg)
{
    asm volatile ("movl %0, %%ebp"::"r"((u32)current->user_stack));
    asm volatile ("movl %0, %%esp"::"r"((u32)current->user_stack + USER_STACK_SIZE));

    switch_to_user_mode();
    execve("shell.elf", 0, 0);
    print_str("shell exec failed\n");
    exit(1);
    return 0;  // not reached
}

void init_task()
{
    running_task_head = (struct task_list *)kmalloc(sizeof(struct task_list));
    // current is kernel thread
    current = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    current->context = (struct task_context *)kmalloc(sizeof(struct task_context));

    current->state = RUNNABLE;
    current->kernel_stack = &kernel_stack;
    current->time_slice = 50;
    current->priority = 1;
    strcpy(current->name, "main");
    current->pid = pid_now++; // kernel pid is 0
    current->mm = NULL;       // do not need this for kernel
    current->user_stack = NULL;
    current->parent = NULL;
    current->flags = 0;
    current->tty = tty_cur;
    memset((u8 *)current->fds, 0, sizeof(current->fds));
    fds_init_std(current);

    running_task_head->task = current;
    running_task_head->next = running_task_head;

}

// Time slice for each process would be decrease everytime schedule() called
// 
void schedule()
{
    // Current task run out time slice
    //
    // NOTE: a task must never become non-RUNNABLE from inside code reached
    // through schedule() (i.e. between two switches). Every switch happens
    // with interrupts off inside some unfinished timer IRQ; if the task set
    // stops making progress the whole chain of suspended rotations can
    // livelock and the timer never completes (tick freezes). Blocking
    // syscalls (read/waitpid/nanosleep) therefore keep the task RUNNABLE
    // and wait with sti+hlt instead; only exit() leaves the rotation, and
    // it never comes back.
    if (current->time_slice == 0) {
        current->time_slice = current->priority * 50;
        // Find next RUNNABLE
        for (;;) {
            running_task_head = running_task_head->next;
            // Next task is RUNNABLE
            if (running_task_head->task->state == RUNNABLE) {
                // Switch to next
                switch_to(running_task_head->task);
            }
            // Return if no more task
            if (running_task_head->task == current) {
                return;
            }
        }
    } else {
        current->time_slice--;
    }
}

void switch_to(struct task_struct *next)
{
    if (current != next) {
        struct task_struct *prev = current;
        current = next;
        tty_print = next->tty;
        // Change our kernel stack over.
        set_kernel_stack((u32)next->kernel_stack+KERNEL_STACK_SIZE);
        // switch address space if the tasks do not share one
        u32 next_pdt = next->mm ? next->mm->pdt_phys : kernel_pdt_phys();
        u32 prev_pdt = prev->mm ? prev->mm->pdt_phys : kernel_pdt_phys();
        if (next_pdt != prev_pdt) {
            switch_pdt(next_pdt);
        }
        switch_task(prev->context, current->context);
    }
}

// New task allocated
struct task_struct *alloc_task()
{
    struct task_struct *new_task = 
        (struct task_struct *)kmalloc(sizeof(struct task_struct));
    new_task->context = 
        (struct task_context *)kmalloc(sizeof(struct task_context));
    u8 *kstack = (u8 *)kmalloc(KERNEL_STACK_SIZE);
    u8 *pstack = (u8 *)kmalloc(USER_STACK_SIZE);
    assert(kstack != NULL && pstack != NULL, "kern_thread: kmalloc error");
    // the user stack is the only kernel heap memory CPL3 code must reach
    kmem_mark_user(pstack, USER_STACK_SIZE);

    new_task->state = NEW;
    new_task->priority = current->priority;
    new_task->time_slice = current->priority * 50;
    new_task->exit_code = 0;
    new_task->brk = 0;
    // the child keeps the parent's name until it execs something else
    {
        u32 k;
        for (k = 0; k < sizeof(new_task->name) - 1 && current->name[k]; k++) {
            new_task->name[k] = current->name[k];
        }
        new_task->name[k] = '\0';
    }
    new_task->kernel_stack = kstack;
    new_task->user_stack = pstack;
    new_task->parent = current;
    new_task->pid = pid_now++;
    new_task->mm = NULL;
    new_task->tty = current->tty;
    // POSIX-style: the child inherits the parent's open descriptors
    memcpy((u8 *)new_task->fds, (u8 *)current->fds, sizeof(new_task->fds));

    new_task->context->esp = (u32)((u32)kstack + KERNEL_STACK_SIZE);
    new_task->context->ebp = (u32)kstack;
    // let task's eflags = 0x2000 (enable interrupt)
    new_task->context->eflags = 0x200;

    // insert new task to tasklist's tail
    struct task_list *tail = running_task_head;
    assert(tail != NULL, "Must init sched!");

    while (tail->next != running_task_head) {
        tail = tail->next;
    }
    tail->next = (struct task_list *)kmalloc(sizeof(struct task_list));
    tail->next->task = new_task;
    tail->next->next = running_task_head;

    return new_task;
}

// Create kernel taskess
u32 kthread_start(const char *name, u32 (*fn)(void *), struct tty *tty,
                  u8 priority, void *arg)
{
    struct task_struct *new_task = alloc_task();

    {
        u32 k;
        for (k = 0; k < sizeof(new_task->name) - 1 && name[k]; k++) {
            new_task->name[k] = name[k];
        }
        new_task->name[k] = '\0';
    }
    new_task->priority = priority;
    new_task->time_slice = priority * 50;
    new_task->mm = NULL;
    new_task->tty = tty;

    u32 *stack_top = (u32 *)((u32)new_task->kernel_stack + KERNEL_STACK_SIZE);
    *(--stack_top) = (u32)arg;
    *(--stack_top) = (u32)kthread_exit;
    /**(--stack_top) = (u32)fn;*/

    new_task->context->esp = (u32)stack_top;
    new_task->context->eip = (u32)fn;

    new_task->state = RUNNABLE;
    return new_task->pid;
}

void kthread_exit(){
    u32 val;
    // grab the return value before printk clobbers %eax
    asm volatile ("movl %%eax, %0\n" :"=m"(val));
    // never schedule this task again
    current->state = ZOMBIE;
    printk("%s exited with value 0x%x\n", current->name, val);

    while(1) hlt();
}

void switch_to_user_mode()
{
    // Set up our kernel stack.
    set_kernel_stack((u32)(current->kernel_stack)+KERNEL_STACK_SIZE);

    // Set up a stack structure for switching to user mode.
    enter_user_mode();
}
