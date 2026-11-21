#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#define asmlinkage
#define fastcall

static inline reg_t arch_get_return_value(void)
{
    reg_t value;
    asm volatile("mov %0, x0" : "=r"(value));
    return value;
}

static inline void cli(void)
{
    asm volatile("msr daifset, #2" ::: "memory", "cc");
}

static inline void sti(void)
{
    asm volatile("msr daifclr, #2" ::: "memory", "cc");
}

static inline void hlt(void)
{
    asm volatile("wfi" ::: "memory", "cc");
}

static inline u32 local_irq_save(void)
{
    u64 flags;
    asm volatile("mrs %0, daif" : "=r" (flags) :: "memory");
    cli();
    return (u32)flags;
}

static inline void local_irq_restore(u32 flags)
{
    if (!(flags & 0x80)) {
        sti();
    }
    asm volatile("" ::: "memory", "cc");
}

#endif
