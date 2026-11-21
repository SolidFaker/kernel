#include "common.h"
#include "mm.h"
#include "page.h"

// Temp page directory table
u32 __INITDATA *pdt_temp = (u32 *)(0x2000);
u32 __INITDATA *pet_high = (u32 *)(0x3000);
u32 __INITDATA *pet_low = (u32 *)(0x4000);

extern void kernel_start(void);

// Set kernel virtual address start from PAGE_OFFSET
void __INIT early_page_init(void)
{
    pdt_temp[0] = (u32)pet_low | PG_WRITE | PG_PRESENT;
    pdt_temp[PDT_INDEX(PAGE_OFFSET)] = (u32)pet_high | PG_WRITE | PG_PRESENT;
    u32 i;
    for (i=0; i<1024; i++) {
        pet_low[i] = (i << 12) | PG_WRITE | PG_PRESENT;
    }
    for (i=0; i<1024; i++) {
        pet_high[i] = (i << 12) | PG_WRITE | PG_PRESENT;
    }
    asm volatile ("mov %0, %%cr3" :: "r"(pdt_temp));   // put page table addr
    u32 cr0;
    asm volatile("mov %%cr0, %0": "=r"(cr0));
    cr0 |= 0x80000000;                        // enable paging!
    asm volatile("mov %0, %%cr0":: "r"(cr0));

    kernel_start();
}

