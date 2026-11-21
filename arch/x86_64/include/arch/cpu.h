#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#define asmlinkage
#define fastcall

static inline reg_t arch_get_return_value(void)
{
    reg_t value;
    asm volatile("movq %%rax, %0" : "=r"(value));
    return value;
}

static inline void insl(u16 port, void *addr, int cnt)
{
    asm volatile("cld; rep insl" : "=D"(addr), "=c"(cnt)
                 : "d"(port), "0"(addr), "1"(cnt) : "memory", "cc");
}

static inline void cli(void)
{
    asm volatile("cli" ::: "memory", "cc");
}

static inline void hlt(void)
{
    asm volatile("hlt" ::: "memory", "cc");
}

static inline void sti(void)
{
    asm volatile("sti" ::: "memory", "cc");
}

static inline u32 local_irq_save(void)
{
    u64 flags;
    asm volatile("pushfq; popq %0; cli" : "=r" (flags) :: "memory");
    return (u32)flags;
}

static inline void local_irq_restore(u32 flags)
{
    asm volatile("pushq %0; popfq" : : "r" ((u64)flags) : "memory", "cc");
}

static inline void outb(u16 port, u8 v)
{
    asm volatile("outb %0,%1" : : "a" (v), "dN" (port));
}

static inline u8 inb(u16 port)
{
    u8 v;
    asm volatile("inb %1,%0" : "=a" (v) : "dN" (port));
    return v;
}

static inline void outw(u16 port, u16 v)
{
    asm volatile("outw %0,%1" : : "a" (v), "dN" (port));
}

static inline u16 inw(u16 port)
{
    u16 v;
    asm volatile("inw %1,%0" : "=a" (v) : "dN" (port));
    return v;
}

static inline void outl(u16 port, u32 v)
{
    asm volatile("outl %0,%1" : : "a" (v), "dN" (port));
}

static inline u32 inl(u16 port)
{
    u32 v;
    asm volatile("inl %1,%0" : "=a" (v) : "dN" (port));
    return v;
}

#endif
