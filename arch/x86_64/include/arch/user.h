#ifndef ARCH_USER_H
#define ARCH_USER_H
#include "common.h"
#include "init.h"

static inline void arch_prepare_user_frame(struct trap_frame *frame,
                                           reg_t entry, reg_t sp)
{
    frame->cs = __USER_CS;
    frame->ss = __USER_DS;
    frame->eflags = 0x202;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
    frame->user_lr = 0;
}

static inline void arch_set_fork_result(struct trap_frame *frame, reg_t value)
{
    frame->eax = value;
}

static inline uaddr arch_kthread_sp(reg_t *sp)
{
    return (uaddr)sp + sizeof(reg_t);
}

static inline reg_t arch_syscall0(reg_t nr)
{
    reg_t r;
    asm volatile("syscall" : "=a"(r) : "0"(nr) : "rcx", "r11", "memory");
    return r;
}
static inline reg_t arch_syscall1(reg_t nr, reg_t a0)
{
    reg_t r;
    asm volatile("syscall" : "=a"(r) : "0"(nr), "D"(a0) : "rcx", "r11", "memory");
    return r;
}
static inline reg_t arch_syscall2(reg_t nr, reg_t a0, reg_t a1)
{
    reg_t r;
    asm volatile("syscall" : "=a"(r) : "0"(nr), "D"(a0), "S"(a1) : "rcx", "r11", "memory");
    return r;
}
static inline reg_t arch_syscall3(reg_t nr, reg_t a0, reg_t a1, reg_t a2)
{
    reg_t r;
    asm volatile("syscall" : "=a"(r) : "0"(nr), "D"(a0), "S"(a1), "d"(a2)
                 : "rcx", "r11", "memory");
    return r;
}
static inline reg_t arch_syscall4(reg_t nr, reg_t a0, reg_t a1, reg_t a2,
                                  reg_t a3)
{
    register reg_t r10 __asm__("r10") = a3;
    reg_t r;
    asm volatile("syscall" : "=a"(r)
                 : "0"(nr), "D"(a0), "S"(a1), "d"(a2), "r"(r10)
                 : "rcx", "r11", "memory");
    return r;
}
static inline void arch_set_tls(uaddr value)
{
    asm volatile("wrmsr" :: "c"(0xc0000100), "a"((u32)value),
                 "d"((u32)(value >> 32)) : "memory");
}

static inline uaddr arch_get_tls(void)
{
    u32 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xc0000100));
    return low | ((uaddr)high << 32);
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

static inline void arch_prepare_thread_frame(struct trap_frame *frame,
                                             uaddr entry, uaddr arg,
                                             uaddr stack)
{
    uaddr sp = (stack - 8) & ~(uaddr)15;
    *(uaddr *)sp = 0;
    frame->raw[5] = arg;
    frame->raw[7] = sp;
    frame->edi = arg;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;
}

static inline uaddr arch_signal_stack_adjust(uaddr base)
{
    return base + 8;
}

static inline uaddr arch_signal_stack_base(struct trap_frame *frame)
{
    return frame->useresp - sizeof(reg_t);
}

static inline void arch_signal_arguments(struct trap_frame *frame, int sig,
                                         uaddr info, uaddr saved,
                                         reg_t restorer)
{
    *(reg_t *)frame->useresp = restorer;
    frame->raw[5] = sig;
    frame->raw[4] = info;
    frame->raw[3] = saved;
    frame->edi = sig;
    frame->esi = info;
    frame->edx = saved;
}

static inline void arch_write_sigreturn(u8 *code, int linux)
{
    u32 nr = linux ? 15 : 51;
    code[0] = 0x48; code[1] = 0xc7; code[2] = 0xc0;
    __builtin_memcpy(code + 3, (u8 *)&nr, 4);
    code[7] = 0x0f; code[8] = 0x05;
}
#endif
