#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#define asmlinkage __attribute__((regparm(0)))
#define fastcall __attribute__((regparm(3)))

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
    u32 flags;
    asm volatile("pushfl; popl %0; cli" : "=r" (flags) : : "memory");
    return flags;
}

static inline void local_irq_restore(u32 flags)
{
    asm volatile("pushl %0; popfl" : : "r" (flags) : "memory", "cc");
}

static inline void stosb(void *addr, int data, int cnt)
{
    asm volatile("cld; rep stosb" :
            "=D" (addr), "=c" (cnt) :
            "0" (addr), "1" (cnt), "a" (data) :
            "memory", "cc");
}

static inline void insl(u16 port, void *addr, int cnt)
{
    asm volatile("cld; rep insl" :
            "=D" (addr), "=c" (cnt) :
            "d" (port), "0" (addr), "1" (cnt) :
            "memory", "cc");
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

static inline reg_t arch_get_return_value(void)
{
    reg_t value;
    asm volatile("movl %%eax, %0" : "=r"(value));
    return value;
}

#endif
