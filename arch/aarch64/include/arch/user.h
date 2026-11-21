#ifndef ARCH_USER_H
#define ARCH_USER_H
#include "common.h"
#include "init.h"

static inline void arch_prepare_user_frame(struct trap_frame *frame,
                                           reg_t entry, reg_t sp)
{
    frame->cs = 3;
    frame->ss = 3;
    frame->eflags = 0;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
    frame->user_lr = 0;
}
static inline void arch_set_fork_result(struct trap_frame *frame, reg_t value)
{
    frame->eax = value;
    frame->x[0] = value;
}

static inline uaddr arch_kthread_sp(reg_t *sp) { return (uaddr)sp; }
static inline reg_t arch_syscall0(reg_t nr)
{
    register reg_t x0 __asm__("x0");
    register reg_t x8 __asm__("x8") = nr;
    asm volatile("svc #0" : "=r"(x0) : "r"(x8) : "memory");
    return x0;
}
static inline reg_t arch_syscall1(reg_t nr, reg_t a0)
{
    register reg_t x0 __asm__("x0") = a0;
    register reg_t x8 __asm__("x8") = nr;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}
static inline reg_t arch_syscall2(reg_t nr, reg_t a0, reg_t a1)
{
    register reg_t x0 __asm__("x0") = a0;
    register reg_t x1 __asm__("x1") = a1;
    register reg_t x8 __asm__("x8") = nr;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
    return x0;
}
static inline reg_t arch_syscall3(reg_t nr, reg_t a0, reg_t a1, reg_t a2)
{
    register reg_t x0 __asm__("x0") = a0;
    register reg_t x1 __asm__("x1") = a1;
    register reg_t x2 __asm__("x2") = a2;
    register reg_t x8 __asm__("x8") = nr;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}
static inline reg_t arch_syscall4(reg_t nr, reg_t a0, reg_t a1, reg_t a2,
                                  reg_t a3)
{
    register reg_t x0 __asm__("x0") = a0;
    register reg_t x1 __asm__("x1") = a1;
    register reg_t x2 __asm__("x2") = a2;
    register reg_t x3 __asm__("x3") = a3;
    register reg_t x8 __asm__("x8") = nr;
    asm volatile("svc #0" : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}
static inline void arch_set_tls(uaddr value)
{
    asm volatile("msr tpidr_el0, %0" :: "r"(value) : "memory");
}
static inline uaddr arch_get_tls(void)
{
    uaddr value;
    asm volatile("mrs %0, tpidr_el0" : "=r"(value));
    return value;
}
static inline void arch_set_tls_entry(u32 entry, uaddr value)
{
    (void)entry;
    arch_set_tls(value);
}
static inline uaddr arch_get_tls_entry(u32 entry)
{
    (void)entry;
    return arch_get_tls();
}
static inline uaddr arch_signal_stack_adjust(uaddr base)
{
    return base;
}

static inline uaddr arch_signal_stack_base(struct trap_frame *frame)
{
    return frame->useresp;
}

static inline void arch_prepare_thread_frame(struct trap_frame *frame,
                                             uaddr entry, uaddr arg,
                                             uaddr stack)
{
    uaddr sp = (stack - 16) & ~(uaddr)15;
    *(uaddr *)sp = 0;
    frame->x[0] = arg;
    frame->ebx = arg;
    frame->eax = arg;
    frame->cs = 3;
    frame->ss = 3;
    frame->eflags = 0;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
    frame->user_lr = 0;
}

static inline void arch_signal_arguments(struct trap_frame *frame, int sig,
                                         uaddr info, uaddr saved,
                                         reg_t restorer)
{
    frame->x[0] = sig;
    frame->x[1] = info;
    frame->x[2] = saved;
    frame->x[30] = restorer;
    frame->ebx = sig;
    frame->ecx = info;
    frame->edx = saved;
    frame->eax = sig;
    frame->user_lr = restorer;
}

static inline void arch_write_sigreturn(u8 *code, int linux)
{
    u32 mov = 0xd2800008u | ((linux ? 139 : 51) << 5);
    u32 svc = 0xd4000001u;
    __builtin_memcpy(code, (u8 *)&mov, 4);
    __builtin_memcpy(code + 4, (u8 *)&svc, 4);
}
#endif
