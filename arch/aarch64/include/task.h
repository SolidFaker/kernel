#ifndef TASK_H
#define TASK_H
#include "common.h"
#include "fs.h"
#include "drivers/tty.h"
#include "page.h"

enum task_state { UNUSED, NEW, EMBRYO, SLEEPING, RUNNABLE, ZOMBIE };
struct task_context {
    reg_t eip, esp, ebp, ebx, ecx, edx, esi, edi;
    reg_t eflags, eax, r9, r10, r12, kernel_sp, svc_sp, x28;
} __attribute__((packed));
static inline void arch_set_kthread(struct task_context *context,
                                    reg_t fn, reg_t arg)
{
    context->eip = fn;
    context->edi = arg;
}
struct mm_struct { page_entry_t *pdt; uaddr pdt_phys; int refs; };
struct task_struct {
    u32 pid, tgid;
    u8 is_thread, linux_abi;
    u32 time_slice, flags;
    u8 priority;
    char name[16], cwd[128];
    u32 exit_code;
    uaddr brk, mmap_base;
    void *kernel_stack, *user_stack, *interrupt_stack;
    enum task_state state;
    struct mm_struct *mm;
    struct tty *tty;
    struct file fds[FD_MAX];
    struct task_context *context;
    struct trap_frame *frame;
    struct task_struct *parent;
    u32 pending_sig, blocked_sig;
    reg_t sig_handler[32];
    u32 sig_mask[32];
    reg_t sig_restorer[32];
    u32 alarm_tick, pgid, sid;
    u32 tls_entry;
    uaddr tls;
    uaddr sig_frame;
    u32 uid, euid, gid, egid, suid, sgid;
    u32 ngroups;
    u32 groups[16];
};
struct task_list { struct task_struct *task; struct task_list *next; };
extern struct task_list *running_task_head;
extern struct task_struct *current;
extern u32 pid_now;
extern void switch_task(struct task_context *prev, struct task_context *next);
void enter_user_mode(struct trap_frame *frame);
struct task_struct *alloc_task();
u32 kthread_start(const char *name, u32 (*fn)(void *), struct tty *tty,
                  u8 priority, void *arg);
void kthread_exit();
void init_task();
void schedule();
void switch_to(struct task_struct *next);
u32 task_idle(void *arg);
u32 task_init(void *arg);
void fds_init_std(struct task_struct *task);
#endif
