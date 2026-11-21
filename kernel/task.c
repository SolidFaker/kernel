#include "task.h"
#include "mm.h"
#include "tools.h"
#include "string.h"
#include "common.h"
#include "debug.h"
#include "signal.h"
#include "drivers/timer.h"
#include "syscall.h"
#include "arch/user.h"

extern void enter_user_mode(struct trap_frame *frame);
u32 pid_now = 0;
u8 kernel_irq_stack[KERNEL_STACK_SIZE];

struct task_list *running_task_head = NULL;
struct task_struct *current = NULL;
uaddr user_stack_entry = 0;

// Install fd 0/1/2 on the keyboard and the task's tty.
void fds_init_std(struct task_struct *task)
{
    task->fds[0].used = 1;
    task->fds[0].kind = FD_TTY_IN;
    task->fds[1].used = 1;
    task->fds[1].kind = FD_TTY_OUT;
    task->fds[2].used = 1;
    task->fds[2].kind = FD_TTY_OUT;
    task->fds[0].ofd = ofd_alloc();
    task->fds[1].ofd = ofd_alloc();
    task->fds[2].ofd = ofd_alloc();
}

static void task_credentials_init(struct task_struct *task)
{
    task->uid = task->euid = task->suid = 0;
    task->gid = task->egid = task->sgid = 0;
    task->ngroups = 1;
    task->groups[0] = 0;
}

static void task_credentials_copy(struct task_struct *dst,
                                  const struct task_struct *src)
{
    u32 i;
    dst->uid = src->uid;
    dst->euid = src->euid;
    dst->gid = src->gid;
    dst->egid = src->egid;
    dst->suid = src->suid;
    dst->sgid = src->sgid;
    dst->ngroups = src->ngroups;
    for (i = 0; i < src->ngroups && i < 16; i++) {
        dst->groups[i] = src->groups[i];
    }
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
// user stack, then execs Bash with toybox and the native shell as fallbacks.
// A successful exec replaces this task, so this code never runs again.
u32 task_init(__UNUSED__ void *arg)
{
#if defined(AUTO_POSTEST)
    static const char *native_argv[] = { "postest.elf", 0 };
#elif defined(AUTO_NATIVE)
    static const char *bash_argv[] = {
        "bash", "--norc", "-c", "postest.elf; libctest.elf; exit", 0
    };
#elif defined(AUTO_SMOKE)
    static const char *bash_argv[] = {
        "bash", "--norc", "-c", "echo NATIVE_SCRIPT_OK; ls /; echo COREUTILS_OK; exit", 0
    };
#elif defined(AUTO_DEMO)
    static const char *bash_argv[] = {
        "bash", "--norc", "/system/share/tests/posix-test.sh", 0
    };
#elif defined(AUTO_MULTI)
    static const char *bash_argv[] = {
        "bash", "--norc", "/system/share/tests/users-test.sh", 0
    };
#else
    static const char *bash_argv[] = {
        "bash", "--norc", "-i", 0
    };
#endif
#if !defined(AUTO_POSTEST)
    static const char *toy_argv[] = {
        "toybox", "sh", "-i", 0
    };
#endif
    static const char *toy_envp[] = {
        "PATH=/system/bin:/profiles/users/root/bin",
        "HOME=/home/root", "TERM=linux", "USER=root",
        "LOGNAME=root", "SHELL=/system/bin/bash", 0
    };

    struct trap_frame *frame = (struct trap_frame *)current->kernel_stack;

#if defined(AUTO_POSTEST)
    if (kernel_execve("/system/bin/postest.elf", (char *const *)native_argv,
                      (char *const *)toy_envp, frame) < 0)
#else
    if (kernel_execve("/system/bin/bash", (char *const *)bash_argv,
                      (char *const *)toy_envp, frame) < 0 &&
            kernel_execve("/system/bin/toybox.elf", (char *const *)toy_argv,
                          (char *const *)toy_envp, frame) < 0 &&
            kernel_execve("/system/bin/shell.elf", 0, 0, frame) < 0)
#endif
    {
        printk("shell exec failed\n");
        for (;;) hlt();
    }
    enter_user_mode(frame);
    return 0;
}

void init_task()
{
    running_task_head = (struct task_list *)kmalloc(sizeof(struct task_list));
    // current is kernel thread
    current = (struct task_struct *)kmalloc(sizeof(struct task_struct));
    current->context = (struct task_context *)kmalloc(sizeof(struct task_context));
    memset((u8 *)current->context, 0, sizeof(struct task_context));
    current->context->kernel_sp = (uaddr)kernel_irq_stack + KERNEL_STACK_SIZE;
    current->context->svc_sp = (uaddr)kernel_stack + KERNEL_STACK_SIZE;
    current->interrupt_stack = kernel_irq_stack;

    current->state = RUNNABLE;
    current->kernel_stack = &kernel_stack;
    current->time_slice = 50;
    current->priority = 1;
    strcpy(current->name, "main");
    current->pid = pid_now++; // kernel pid is 0
    current->tgid = current->pid;
    current->is_thread = 0;
    current->linux_abi = 0;
    current->tls_entry = GDT_TLS_FIRST;
    current->tls = 0;
    signal_init_task(current);
    current->mm = NULL;       // do not need this for kernel
    current->mmap_base = MMAP_BASE;
    current->user_stack = NULL;
    current->parent = NULL;
    current->flags = 0;
    task_credentials_init(current);
    current->tty = tty_cur;
    current->cwd[0] = '/';
    current->cwd[1] = '\0';
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
        prev->tls = arch_get_tls_entry(prev->tls_entry);
#ifdef DEBUG_E9
        printk("switch %s -> %s\n", prev->name, next->name);
#endif
        current = next;
        arch_set_tls_entry(next->tls_entry, next->tls);
        tty_print = next->tty;
        // Change our kernel stack over.
        set_kernel_stack((uaddr)next->kernel_stack+KERNEL_STACK_SIZE);
        // switch address space if the tasks do not share one
        uaddr next_pdt = next->mm ? next->mm->pdt_phys : kernel_pdt_phys();
        uaddr prev_pdt = prev->mm ? prev->mm->pdt_phys : kernel_pdt_phys();
        if (next_pdt != prev_pdt) {
            switch_pdt(next_pdt);
        }
#ifdef DEBUG_E9
        if (next->pid >= 4)
            printk("switch child ctxpc=%x framepc=%x sp=%x cs=%x flags=%x usp=%x\n",
                   next->context->eip,
                   next->frame ? next->frame->eip : 0,
                   next->context->esp,
                   next->frame ? next->frame->cs : 0,
                   next->frame ? next->frame->eflags : 0,
                   next->frame ? next->frame->useresp : 0);
#endif
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
    memset((u8 *)new_task->context, 0, sizeof(struct task_context));
    u8 *kstack = (u8 *)kmalloc(KERNEL_STACK_SIZE);
    u8 *irqstack = (u8 *)kmalloc(KERNEL_STACK_SIZE);
    u8 *pstack = (u8 *)kmalloc(USER_STACK_SIZE);
    assert(kstack != NULL && irqstack != NULL && pstack != NULL,
           "kern_thread: kmalloc error");

    new_task->state = NEW;
    new_task->priority = current->priority;
    new_task->time_slice = current->priority * 50;
    new_task->exit_code = 0;
    new_task->brk = 0;
    new_task->mmap_base = current->mmap_base;
    // the child keeps the parent's name until it execs something else
    {
        u32 k;
        for (k = 0; k < sizeof(new_task->name) - 1 && current->name[k]; k++) {
            new_task->name[k] = current->name[k];
        }
        new_task->name[k] = '\0';
    }
    new_task->kernel_stack = kstack;
    new_task->interrupt_stack = irqstack;
    new_task->user_stack = pstack;
    new_task->parent = current;
    new_task->pid = pid_now++;
    new_task->tgid = new_task->pid;
    new_task->is_thread = 0;
    new_task->linux_abi = current->linux_abi;
    task_credentials_copy(new_task, current);
    new_task->tls_entry = current->tls_entry;
    new_task->tls = arch_get_tls_entry(current->tls_entry);
    new_task->mm = NULL;
    new_task->tty = current->tty;
    // POSIX-style: the child inherits the parent's open descriptors
    memcpy((u8 *)new_task->fds, (u8 *)current->fds, sizeof(new_task->fds));
    signal_fork(current, new_task);

    {
        u32 ci;
        for (ci = 0; ci < sizeof(new_task->cwd) - 1 && current->cwd[ci]; ci++) {
            new_task->cwd[ci] = current->cwd[ci];
        }
        new_task->cwd[ci] = '\0';
    }
    {
        int fi;
        for (fi = 0; fi < FD_MAX; fi++) {
            if (new_task->fds[fi].used && new_task->fds[fi].ofd) {
                ofd_get(new_task->fds[fi].ofd);
            }
        }
    }

    new_task->context->esp = (uaddr)((uaddr)kstack + KERNEL_STACK_SIZE);
    new_task->context->ebp = (uaddr)kstack;
    new_task->context->svc_sp = new_task->context->esp;
    new_task->context->kernel_sp = (uaddr)irqstack + KERNEL_STACK_SIZE;
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

    reg_t *stack_top = (reg_t *)((uaddr)new_task->kernel_stack + KERNEL_STACK_SIZE);
    *(--stack_top) = (uaddr)arg;
    *(--stack_top) = (uaddr)kthread_exit;
    /**(--stack_top) = (uaddr)fn;*/

    new_task->context->esp = arch_kthread_sp(stack_top);
    new_task->context->svc_sp = new_task->context->esp;
    arch_set_kthread(new_task->context, (uaddr)fn, (uaddr)arg);

    new_task->state = RUNNABLE;
    return new_task->pid;
}

void kthread_exit(){
    reg_t val = arch_get_return_value();
    // never schedule this task again
    current->state = ZOMBIE;
    printk("%s exited with value 0x%x\n", current->name, val);

    while(1) hlt();
}
