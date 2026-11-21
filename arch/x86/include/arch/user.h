#ifndef ARCH_USER_H
#define ARCH_USER_H

#include "common.h"
#include "init.h"

static inline void arch_prepare_user_frame(struct trap_frame *frame,
                                           reg_t entry, reg_t sp)
{
    frame->gs = frame->fs = frame->es = frame->ds = __USER_DS;
    frame->ss = __USER_DS;
    frame->cs = __USER_CS;
    frame->eflags = 0x202;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
}

static inline void arch_set_fork_result(struct trap_frame *frame, reg_t value)
{
    frame->eax = value;
}

static inline uaddr arch_kthread_sp(reg_t *sp)
{
    return (uaddr)sp;
}

static inline void arch_set_tls_entry(u32 entry, uaddr value)
{
    struct gdt_desc_struct *g = &this_cpu.gdt[entry];
    g->base0 = (u16)value;
    g->base1 = (value >> 16) & 0xff;
    g->base2 = (value >> 24) & 0xff;
}
static inline uaddr arch_get_tls_entry(u32 entry)
{
    struct gdt_desc_struct *g = &this_cpu.gdt[entry];
    return g->base0 | ((uaddr)g->base1 << 16) | ((uaddr)g->base2 << 24);
}

static inline uaddr arch_signal_stack_adjust(uaddr base)
{
    return base;
}

static inline uaddr arch_signal_stack_base(struct trap_frame *frame)
{
    return frame->useresp - sizeof(reg_t);
}

static inline void arch_prepare_thread_frame(struct trap_frame *frame,
                                             uaddr entry, uaddr arg,
                                             uaddr stack)
{
    uaddr sp = (stack - 8) & ~(uaddr)15;
    ((u32 *)sp)[0] = 0;
    ((u32 *)sp)[1] = arg;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
}

static inline void arch_signal_arguments(struct trap_frame *frame, int sig,
                                         uaddr info, uaddr saved,
                                         reg_t restorer)
{
    uaddr *sp = (uaddr *)frame->useresp;
    sp[0] = restorer;
    sp[1] = sig;
    sp[2] = info;
    sp[3] = saved;
}

static inline void arch_write_sigreturn(u8 *code, int linux)
{
    u32 nr = linux ? 173 : 51;
    code[0] = 0xb8;
    __builtin_memcpy(code + 1, (u8 *)&nr, 4);
    code[5] = 0xcd;
    code[6] = 0x80;
}

static inline reg_t arch_syscall0(reg_t nr)
{
    reg_t r;
    asm volatile("int $0x80" : "=a" (r) : "0" (nr) : "memory");
    return r;
}

static inline reg_t arch_syscall1(reg_t nr, reg_t a0)
{
    reg_t r;
    asm volatile("int $0x80" : "=a" (r) : "0" (nr), "b" (a0) : "memory");
    return r;
}

static inline reg_t arch_syscall2(reg_t nr, reg_t a0, reg_t a1)
{
    reg_t r;
    asm volatile("int $0x80" : "=a" (r)
                 : "0" (nr), "b" (a0), "c" (a1) : "memory");
    return r;
}

static inline reg_t arch_syscall3(reg_t nr, reg_t a0, reg_t a1, reg_t a2)
{
    reg_t r;
    asm volatile("int $0x80" : "=a" (r)
                 : "0" (nr), "b" (a0), "c" (a1), "d" (a2) : "memory");
    return r;
}

static inline reg_t arch_syscall4(reg_t nr, reg_t a0, reg_t a1, reg_t a2,
                                  reg_t a3)
{
    reg_t r;
    asm volatile("int $0x80" : "=a" (r)
                 : "0" (nr), "b" (a0), "c" (a1), "d" (a2), "S" (a3)
                 : "memory");
    return r;
}

#endif
