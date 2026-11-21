#include "init.h"
#include "page.h"
#include "mm.h"
#include "string.h"
#include "debug.h"
#include "tools.h"
#include "task.h"
#include "signal.h"

static page_entry_t boot_root[512] __attribute__((aligned(4096)));
static page_entry_t boot_l3[512] __attribute__((aligned(4096)));
static page_entry_t boot_l2[512] __attribute__((aligned(4096)));
static page_entry_t boot_l2_low[512] __attribute__((aligned(4096)));
page_entry_t *pdt_kernel = boot_root;

static u32 table_index(uaddr va, int level)
{
    return (va >> (39 - level * 9)) & 0x1ff;
}
static page_entry_t *table_pointer(uaddr raw)
{
    return (page_entry_t *)(raw & PHYS_ADDR_MASK);
}
static void tlb_flush(void)
{
    asm volatile("dsb ishst; tlbi vmalle1; dsb ish; isb" ::: "memory");
}

void init_page(void)
{
    uaddr va;
    u64 tcr = 0x5b5103510ULL;
    memset((u8 *)boot_root, 0, PAGE_SIZE);
    memset((u8 *)boot_l3, 0, PAGE_SIZE);
    memset((u8 *)boot_l2, 0, PAGE_SIZE);
    memset((u8 *)boot_l2_low, 0, PAGE_SIZE);
    boot_root[0].raw = (uaddr)boot_l3 | 3;
    boot_l3[table_index(0, 1)].raw = (uaddr)boot_l2_low | 3;
    boot_l3[table_index(RAM_PHYS_START, 1)].raw = (uaddr)boot_l2 | 3;
    for (va = 0x08000000; va < 0x0a000000; va += 0x200000)
        boot_l2_low[table_index(va, 2)].raw = va | 1 | (1ULL << 10);
    for (va = RAM_PHYS_START; va < RAM_PHYS_END; va += 0x200000)
        boot_l2[table_index(va, 2)].raw = va | 1 | (1ULL << 10) |
            (3ULL << 8) | 4;
    asm volatile("msr mair_el1, %0" :: "r"(0xff00ULL) : "memory");
    asm volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
    asm volatile("msr ttbr0_el1, %0" :: "r"((uaddr)boot_root) : "memory");
    asm volatile("msr ttbr1_el1, %0" :: "r"((uaddr)boot_root) : "memory");
    tlb_flush();
    asm volatile("mrs %0, sctlr_el1" : "=r"(va));
    va &= ~2ULL;
    va |= 1 | 0x1000;
    asm volatile("dsb ish; msr sctlr_el1, %0; isb" :: "r"(va) : "memory");
}
void switch_pdt(uaddr pdt_addr)
{
    asm volatile("dsb ishst; msr ttbr0_el1, %0" :: "r"(pdt_addr) : "memory");
    tlb_flush();
}
void map(page_entry_t *pdt_now, uaddr va, uaddr pa, u32 flags)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        u32 index = table_index(va, level);
        uaddr raw = table[index].raw;
        if ((raw & 3) == 1 && level == 2) {
            uaddr table_pa = page_alloc();
            page_entry_t *small = (page_entry_t *)table_pa;
            uaddr base = raw & PHYS_ADDR_MASK & ~(uaddr)0x1fffff;
            uaddr attrs = raw & ((3ULL << 53) | (3ULL << 8) |
                                 (3ULL << 6) | (1ULL << 5) |
                                 (7ULL << 2) | (1ULL << 10));
            u32 i;
            for (i = 0; i < 512; i++)
                small[i].raw = base + i * PAGE_SIZE | 3 | attrs;
            table[index].raw = table_pa | 3;
            raw = table[index].raw;
        }
        if (!(raw & 1)) {
            uaddr table_pa = page_alloc();
            memset((u8 *)table_pa, 0, PAGE_SIZE);
            table[index].raw = table_pa | 3;
            raw = table[index].raw;
        }
        table = table_pointer(raw);
    }
    {
        uaddr ap = (flags & PG_USER) ?
            ((flags & PG_WRITE) ? (1ULL << 6) : (3ULL << 6)) : 0;
        uaddr xn = (flags & PG_USER) ? (1ULL << 53) : (3ULL << 53);
        table[PET_INDEX(va)].raw = (pa & PHYS_ADDR_MASK) | 3 |
            (1ULL << 10) | (1ULL << 5) | (3ULL << 8) |
            (1ULL << 2) | xn | ap;
    }
    tlb_flush();
}
void unmap(page_entry_t *pdt_now, uaddr va)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        uaddr raw = table[table_index(va, level)].raw;
        if (!(raw & 1) || (raw & 3) == 1) return;
        table = table_pointer(raw);
    }
    table[PET_INDEX(va)].raw = 0;
    tlb_flush();
}
u32 get_mapping(page_entry_t *pdt_now, uaddr va, uaddr *pa)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        uaddr raw = table[table_index(va, level)].raw;
        if (!(raw & 1)) return 0;
        if ((raw & 3) == 1) {
            uaddr mask = 0x1fffff;
            if (pa) *pa = (raw & PHYS_ADDR_MASK & ~mask) + (va & mask);
            return 1;
        }
        table = table_pointer(raw);
    }
    {
        uaddr raw = table[PET_INDEX(va)].raw;
        if (!(raw & 1)) return 0;
        if (pa) *pa = (raw & PHYS_ADDR_MASK) + (va & ~PAGE_MASK);
    }
    return 1;
}
void page_fault(struct trap_frame *frame)
{
    uaddr far, esr, sctlr;
    asm volatile("mrs %0, far_el1" : "=r"(far));
    asm volatile("mrs %0, esr_el1" : "=r"(esr));
    asm volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    printk("AArch64 page fault pc=%x address=%x status=%x sctlr=%x lr=%x fp=%x\n",
           frame->eip, far, esr, sctlr, frame->x[30], frame->x[29]);
    if (frame->cs & 3) {
        signal_kill((int)current->pid, SIGSEGV);
        return;
    }
    cli();
    for (;;) hlt();
}
