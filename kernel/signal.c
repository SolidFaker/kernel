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
#include "arch/user.h"
#include "string.h"
#include "tools.h"

#define SIG_BIT(s) (1u << (s))

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
    t->sig_frame = 0;
    t->alarm_tick = 0;
    for (i = 0; i < NSIG; i++) {
        t->sig_handler[i] = (uaddr)SIG_DFL;
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
    child->sig_frame = 0;
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
    t->sig_frame = 0;

    for (i = 0; i < NSIG; i++) {
        if (t->sig_handler[i] != (uaddr)SIG_IGN) {
            t->sig_handler[i] = (uaddr)SIG_DFL;
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
        struct task_struct *t = signal_find_task((uaddr)pid);
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
        u32 group = pid == 0 ? current->pgid : (uaddr)(-pid);
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
        now + (uaddr)seconds * TIMER_HZ : 0;
    return (int)old;
}

void signal_check_alarm(void)
{
    if (current->alarm_tick && tick >= current->alarm_tick) {
        current->alarm_tick = 0;
        current->pending_sig |= SIG_BIT(SIGALRM);
    }
}

#define SIGNAL_TRAMPOLINE_VA 0x00f00000ULL

static reg_t signal_trampoline(void)
{
    uaddr va = SIGNAL_TRAMPOLINE_VA;
    uaddr pa;
    if (get_mapping(current->mm->pdt, va, &pa)) return va;
    pa = page_alloc();
    map(current->mm->pdt, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
    arch_write_sigreturn((u8 *)va, current->linux_abi);
    return va;
}

void signal_deliver(struct trap_frame *frame)
{
    for (;;) {
        u32 pending;
        int sig;
        reg_t handler, restorer;
        uaddr base;
        struct trap_frame *saved;
        u8 *info;

        if (!(frame->cs & 3)) return;
        pending = current->pending_sig & ~current->blocked_sig;
        pending |= current->pending_sig & SIG_BIT(SIGKILL);
        if (!pending) return;
        for (sig = 1; sig < NSIG; sig++)
            if (pending & SIG_BIT(sig)) break;
        if (sig >= NSIG) return;
        current->pending_sig &= ~SIG_BIT(sig);
        handler = current->sig_handler[sig];
        restorer = current->sig_restorer[sig];
        if (sig == SIGKILL || sig == SIGSTOP) handler = (uaddr)SIG_DFL;
        if (handler == (uaddr)SIG_IGN) continue;
        if (handler == (uaddr)SIG_DFL || handler == 0) {
            if (default_ignored(sig)) continue;
            terminate_current(sig);
        }
        if (!restorer) restorer = signal_trampoline();
#ifdef DEBUG_E9
        if (sig == SIGUSR1)
            printk("deliver usr1 handler=%x restorer=%x oldpc=%x\n",
                   handler, restorer, frame->eip);
#endif

        base = (frame->useresp - 32 - sizeof(*frame) - 144) & ~(uaddr)15;
        base = arch_signal_stack_adjust(base);
        saved = (struct trap_frame *)(base + 16);
        info = (u8 *)saved + sizeof(*saved);
        memcpy((u8 *)saved, (const u8 *)frame, sizeof(*saved));
        memset(info, 0, 128);
        *(u32 *)info = sig;
        *(u32 *)(info + 128) = current->blocked_sig;
        *(uaddr *)(info + 136) = current->sig_frame;
        current->sig_frame = (uaddr)saved;
        frame->useresp = base;
        frame->esp = base;
        frame->eip = handler;
        arch_signal_arguments(frame, sig, (uaddr)info, (uaddr)saved, restorer);
        current->blocked_sig |= SIG_BIT(sig) | current->sig_mask[sig];
        current->blocked_sig &= ~SIG_BIT(SIGKILL);
        return;
    }
}

static int signal_restore(struct trap_frame *frame)
{
    struct trap_frame *saved = (struct trap_frame *)current->sig_frame;
    uaddr base = ((uaddr)saved) - 16;
    uaddr end = base + 16 + sizeof(*saved) + 144;
    u32 old_mask;
    int result;
    if (base < 0x1000 || end < base || end > USER_ADDR_LIMIT)
        return -EFAULT;
    old_mask = *(u32 *)((u8 *)saved + sizeof(*saved) + 128);
    {
        uaddr previous = *(uaddr *)((u8 *)saved + sizeof(*saved) + 136);
        current->sig_frame = previous;
    }
#ifdef DEBUG_E9
    printk("sigreturn base=%x saved=%x pc=%x sp=%x cs=%x\n",
           base, saved, saved->eip, saved->useresp, saved->cs);
#endif
    result = (int)saved->eax;
    memcpy((u8 *)frame, (const u8 *)saved, sizeof(*frame));
    current->blocked_sig = old_mask;
    return result;
}

int signal_sigreturn(struct trap_frame *frame)
{
    return signal_restore(frame);
}

int signal_sigreturn_linux(struct trap_frame *frame, int rt)
{
    (void)rt;
    return signal_restore(frame);
}
