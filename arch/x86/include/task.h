#ifndef TASK_H
#define TASK_H

#include "common.h"
#include "fs.h"
#include "drivers/tty.h"
#include "page.h"

enum task_state { UNUSED, NEW, EMBRYO, SLEEPING, RUNNABLE, ZOMBIE };

struct task_context {
    reg_t eip;
    reg_t esp;
    reg_t ebp;
    reg_t ebx;
    reg_t ecx;
    reg_t edx;
    reg_t esi;
    reg_t edi;
    reg_t eflags;
    reg_t eax;
    reg_t kernel_sp;
    reg_t svc_sp;
}__attribute__((packed));

static inline void arch_set_kthread(struct task_context *context,
                                    reg_t fn, reg_t arg)
{
    context->eip = fn;
    context->edi = arg;
}

struct mm_struct {
    page_entry_t *pdt;     // kernel-space alias of the page directory
    uaddr pdt_phys;          // physical address, for loading into CR3
    int refs;              // shared by threads
};

struct task_struct {
    u32 pid;
    u32 tgid;
    u8 is_thread;
    u8 linux_abi;
    u32 time_slice;
    u32 flags;
    u8 priority;
    char name[16];               // task name: kthread label / program name
    char cwd[128];               // absolute working directory
    u32 exit_code;               // set by exit(), read by waitpid()
    uaddr brk;                   // user heap break (mm tasks only)
    uaddr mmap_base;               // next anonymous mmap address
    void *kernel_stack;
    void *user_stack;
    void *interrupt_stack;
    enum task_state state;
    struct mm_struct *mm;         // task memory space
    struct tty *tty;
    struct file fds[FD_MAX];      // open files; 0/1/2 = std streams
    struct task_context *context;      // switch() here to run taskess
    struct trap_frame *frame;
    struct task_struct *parent;

    /* POSIX signal state */
    u32 pending_sig;
    u32 blocked_sig;
    reg_t sig_handler[32];
    u32 sig_mask[32];
    reg_t sig_restorer[32];
    u32 alarm_tick;
    u32 tls_entry;
    uaddr tls;
    uaddr sig_frame;
    u32 pgid;
    u32 sid;
    u32 uid, euid, gid, egid, suid, sgid;
    u32 ngroups;
    u32 groups[16];
};

struct task_list {
    struct task_struct *task;
    struct task_list *next;
};

// schedulable taskess list
extern struct task_list *running_task_head;
// current taskess
extern struct task_struct *current;
// Global pid
extern u32 pid_now;

// saves the running state into prev's context and loads next's
extern void switch_task(struct task_context *prev, struct task_context *next);

void enter_user_mode(struct trap_frame *frame);
struct task_struct *alloc_task();
u32  kthread_start(const char *name, u32 (*fn)(void *),
                   struct tty *tty, u8 priority, void *arg);
void kthread_exit();
void init_task();
void schedule();
void switch_to(struct task_struct *next);
u32 task_idle(void *arg);
u32 task_init(void *arg);
void fds_init_std(struct task_struct *task);

#endif
