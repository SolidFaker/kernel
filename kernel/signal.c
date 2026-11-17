// POSIX signals: pending/blocked masks, dispositions, handler delivery and
// default actions. Handlers return through the user-provided sa_restorer,
// which must invoke the sigreturn syscall.

#include "signal.h"
#include "posix_abi.h"
#include "errno.h"
#include "fs.h"
#include "init.h"
#include "mm.h"
#include "page.h"
#include "task.h"
#include "drivers/timer.h"
#include "string.h"

#define SIG_BIT(s) (1u << (s))

struct sig_context {
    u32 edi, esi, ebp, ebx, edx, ecx, eax;
    u32 eip, cs, eflags, useresp, ss;
    u32 signum, old_mask;
};

static int default_ignored(int sig)
{
    return sig == SIGCHLD || sig == SIGCONT || sig == SIGURG ||
           sig == SIGWINCH;
}

struct task_struct *signal_find_task(u32 pid)
{
    struct task_list *node;

    if (!running_task_head) {
        return 0;
    }
    node = running_task_head;
    do {
        if (node->task->pid == pid) {
            return node->task;
        }
        node = node->next;
    } while (node != running_task_head);
    return 0;
}

void signal_init_task(struct task_struct *t)
{
    u32 i;

    t->pending_sig = 0;
    t->blocked_sig = 0;
    t->alarm_tick = 0;
    for (i = 0; i < NSIG; i++) {
        t->sig_handler[i] = (u32)SIG_DFL;
        t->sig_mask[i] = 0;
        t->sig_restorer[i] = 0;
    }
    t->pgid = t->pid;
    t->sid = t->pid;
}

void signal_fork(struct task_struct *parent, struct task_struct *child)
{
    u32 i;

    child->pending_sig = 0;
    child->blocked_sig = parent->blocked_sig;
    child->alarm_tick = 0;
    for (i = 0; i < NSIG; i++) {
        child->sig_handler[i] = parent->sig_handler[i];
        child->sig_mask[i] = parent->sig_mask[i];
        child->sig_restorer[i] = parent->sig_restorer[i];
    }
    child->pgid = parent->pgid;
    child->sid = parent->sid;
}

void signal_exec(struct task_struct *t)
{
    u32 i;

    for (i = 0; i < NSIG; i++) {
        if (t->sig_handler[i] != (u32)SIG_IGN) {
            t->sig_handler[i] = (u32)SIG_DFL;
            t->sig_mask[i] = 0;
            t->sig_restorer[i] = 0;
        }
    }
}

static void terminate_current(int sig)
{
    int i;

    current->exit_code = current->linux_abi ? sig : (128 + sig);
    if (!current->is_thread && current->parent) {
        current->parent->pending_sig |= SIG_BIT(SIGCHLD);
    }
    for (i = 0; i < FD_MAX; i++) {
        if (current->fds[i].used) {
            ofd_put(current->fds[i].ofd);
            memset((u8 *)&current->fds[i], 0, sizeof(current->fds[i]));
        }
    }
    switch_pdt(kernel_pdt_phys());
    if (current->mm) {
        mm_put(current->mm);
        current->mm = 0;
    }
    current->state = ZOMBIE;
    current->time_slice = 0;
    schedule();
    for (;;) {
        hlt();
    }
}

static int send_one(struct task_struct *t, int sig)
{
    if (!t || sig <= 0 || sig >= NSIG) {
        return -EINVAL;
    }
    t->pending_sig |= SIG_BIT(sig);
    return 0;
}

int signal_kill(int pid, int sig)
{
    struct task_list *node;
    int found = 0;

    if (sig < 0 || sig >= NSIG) {
        return -EINVAL;
    }
    if (pid > 0) {
        struct task_struct *t = signal_find_task((u32)pid);
        if (!t) {
            return -ESRCH;
        }
        if (sig == 0) {
            return 0;
        }
        return send_one(t, sig);
    }

    if (!running_task_head) {
        return -ESRCH;
    }
    node = running_task_head;
    do {
        struct task_struct *t = node->task;
        u32 group = pid == 0 ? current->pgid : (u32)(-pid);
        if (t->pgid == group) {
            found = 1;
            if (sig != 0) {
                send_one(t, sig);
            }
        }
        node = node->next;
    } while (node != running_task_head);

    return found ? 0 : -ESRCH;
}

int signal_alarm(int seconds)
{
    u32 old = 0;
    u32 now = tick;

    if (current->alarm_tick && current->alarm_tick > now) {
        old = (current->alarm_tick - now + TIMER_HZ - 1) / TIMER_HZ;
    }
    current->alarm_tick = seconds > 0 ?
        now + (u32)seconds * TIMER_HZ : 0;
    return (int)old;
}

void signal_check_alarm(void)
{
    if (current->alarm_tick && tick >= current->alarm_tick) {
        current->alarm_tick = 0;
        current->pending_sig |= SIG_BIT(SIGALRM);
    }
}

void signal_deliver(struct trap_frame *frame)
{
    for (;;) {
        u32 pending;
        int sig;
        u32 handler, restorer;
        u32 *sp;
        struct sig_context *ctx;

        if (!(frame->cs & 3)) {
            return;  /* only user-mode returns */
        }
        pending = current->pending_sig & ~current->blocked_sig;
        pending |= current->pending_sig & SIG_BIT(SIGKILL);
        if (!pending) {
            return;
        }

        for (sig = 1; sig < NSIG; sig++) {
            if (pending & SIG_BIT(sig)) {
                break;
            }
        }
        if (sig >= NSIG) {
            return;
        }
        current->pending_sig &= ~SIG_BIT(sig);

        handler = current->sig_handler[sig];
        restorer = current->sig_restorer[sig];
        if (sig == SIGKILL || sig == SIGSTOP) {
            handler = (u32)SIG_DFL;
        }
        if (handler == (u32)SIG_IGN) {
            continue;
        }
        if (handler == (u32)SIG_DFL || handler == 0 || restorer == 0) {
            if (default_ignored(sig)) {
                continue;
            }
            terminate_current(sig);  /* does not return */
        }

        sp = (u32 *)frame->useresp;
        sp -= sizeof(struct sig_context) / sizeof(u32);
        ctx = (struct sig_context *)sp;
        ctx->edi = frame->edi;
        ctx->esi = frame->esi;
        ctx->ebp = frame->ebp;
        ctx->ebx = frame->ebx;
        ctx->edx = frame->edx;
        ctx->ecx = frame->ecx;
        ctx->eax = frame->eax;
        ctx->eip = frame->eip;
        ctx->cs = frame->cs;
        ctx->eflags = frame->eflags;
        ctx->useresp = frame->useresp;
        ctx->ss = frame->ss;
        ctx->signum = (u32)sig;
        ctx->old_mask = current->blocked_sig;

        sp -= 2;
        sp[0] = restorer;       /* return address used by the handler */
        sp[1] = (u32)sig;       /* handler argument */

        frame->useresp = (u32)sp;
        frame->eip = handler;
        current->blocked_sig |= SIG_BIT(sig) | current->sig_mask[sig];
        current->blocked_sig &= ~SIG_BIT(SIGKILL);
        return;
    }
}

static int sigreturn_ctx(struct trap_frame *frame, struct sig_context *ctx)
{
    if ((u32)ctx < 0x1000 || (u32)ctx + sizeof(*ctx) >= PAGE_OFFSET) {
        return -EFAULT;
    }
    current->blocked_sig = ctx->old_mask;
    frame->edi = ctx->edi;
    frame->esi = ctx->esi;
    frame->ebp = ctx->ebp;
    frame->ebx = ctx->ebx;
    frame->edx = ctx->edx;
    frame->ecx = ctx->ecx;
    frame->eip = ctx->eip;
    frame->cs = ctx->cs;
    frame->eflags = ctx->eflags;
    frame->useresp = ctx->useresp;
    frame->ss = ctx->ss;
    return (int)ctx->eax;
}

int signal_sigreturn(struct trap_frame *frame)
{
    if (frame->useresp < 0x1000 || frame->useresp >= PAGE_OFFSET) {
        return -EFAULT;
    }
    return sigreturn_ctx(frame,
        (struct sig_context *)(frame->useresp + 4));
}

int signal_sigreturn_linux(struct trap_frame *frame, int rt)
{
    if (frame->useresp < 0x1000 || frame->useresp >= PAGE_OFFSET) {
        return -EFAULT;
    }
    return sigreturn_ctx(frame,
        (struct sig_context *)(frame->useresp + (rt ? 4 : 0)));
}
