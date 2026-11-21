#include "init.h"
#include "page.h"
#include "mm.h"
#include "string.h"
#include "debug.h"
#include "tools.h"
#include "task.h"
#include "signal.h"

page_entry_t *pdt_kernel = (page_entry_t *)(0x9000 + PAGE_OFFSET);

static u32 table_index(uaddr va, int level)
{
    return (va >> (39 - level * 9)) & 0x1ff;
}

static page_entry_t *table_pointer(uaddr raw)
{
    return (page_entry_t *)((raw & PAGE_MASK) + PAGE_OFFSET);
}

void init_page(void)
{
}

void switch_pdt(uaddr pdt_addr)
{
    asm volatile("movq %0, %%cr3" :: "r"(pdt_addr) : "memory");
}

void map(page_entry_t *pdt_now, uaddr va, uaddr pa, u32 flags)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        u32 index = table_index(va, level);
        uaddr raw = table[index].raw;
        if ((raw & PG_SIZE) && level == 2) {
            uaddr table_pa = page_alloc();
            page_entry_t *small = (page_entry_t *)(table_pa + PAGE_OFFSET);
            uaddr base = raw & ~(uaddr)0x1fffff;
            u32 i;
            for (i = 0; i < 512; i++)
                small[i].raw = base + i * PAGE_SIZE |
                    PG_PRESENT | PG_WRITE | (raw & PG_USER);
            table[index].raw = table_pa | PG_PRESENT | PG_WRITE |
                (raw & PG_USER);
            raw = table[index].raw;
        }
        if (!(raw & PG_PRESENT)) {
            uaddr table_pa = page_alloc();
            memset((u8 *)(table_pa + PAGE_OFFSET), 0, PAGE_SIZE);
            table[index].raw = table_pa | PG_PRESENT | PG_WRITE |
                (flags & PG_USER);
            raw = table[index].raw;
        }
        table = table_pointer(raw);
    }
    table[PET_INDEX(va)].raw = (pa & PAGE_MASK) | PG_PRESENT |
        (flags & (PG_WRITE | PG_USER));
    asm volatile("invlpg %0" :: "m"(*(u8 *)(uaddr)va) : "memory");
}

void unmap(page_entry_t *pdt_now, uaddr va)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        uaddr raw = table[table_index(va, level)].raw;
        if (!(raw & PG_PRESENT) || (raw & PG_SIZE)) return;
        table = table_pointer(raw);
    }
    table[PET_INDEX(va)].raw = 0;
    asm volatile("invlpg %0" :: "m"(*(u8 *)(uaddr)va) : "memory");
}

u32 get_mapping(page_entry_t *pdt_now, uaddr va, uaddr *pa)
{
    page_entry_t *table = pdt_now;
    int level;
    for (level = 0; level < 3; level++) {
        uaddr raw = table[table_index(va, level)].raw;
        if (!(raw & PG_PRESENT)) return 0;
        if (raw & PG_SIZE) {
            uaddr mask = level == 2 ? 0x1fffff : 0x3fffffff;
            if (pa) *pa = (raw & ~mask) + (va & mask);
            return 1;
        }
        table = table_pointer(raw);
    }
    {
        uaddr raw = table[PET_INDEX(va)].raw;
        if (!(raw & PG_PRESENT)) return 0;
        if (pa) *pa = (raw & PAGE_MASK) + (va & ~PAGE_MASK);
    }
    return 1;
}

void page_fault(struct trap_frame *frame)
{
    uaddr address;
    asm volatile("movq %%cr2, %0" : "=r"(address));
    printk("x86_64 page fault pc=%x address=%x error=%x\n",
           (u64)frame->eip, (u64)address, (u64)frame->err_code);
    if (frame->cs & 3) {
        signal_kill((int)current->pid, SIGSEGV);
        return;
    }
    cli();
    for (;;) hlt();
}
