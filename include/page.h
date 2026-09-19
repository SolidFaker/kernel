#ifndef PAGE_H
#define PAGE_H

#include "common.h"
#include "init.h"

#define  PG_PRESENT   0x001
#define  PG_WRITE     0x002
#define  PG_USER      0x004
#define  PG_ACCESSED  0x020
#define  PG_DIRTY     0x040
#define  PG_SIZE      0x080

#define  PAGE_SIZE    0x1000
#define  PAGE_MASK    0xFFFFF000
#define  PAGE_OFFSET  0xC0000000

#define  KPDT_COUNT       16
#define  PAGE_TABLE_SIZE  1024
#define  KPDT_SIZE        (KPDT_COUNT * PAGE_TABLE_SIZE)
#define  KERNV_START      0x100000
#define  KERNP_START      0x100000

// get page directory index
#define PDT_INDEX(x) (((x) >> 22) & 0x3FF)
// get page entry index
#define PET_INDEX(x) (((x) >> 12) & 0x3FF)
// page aligned address
#define OFFSET_INDEX(x) ((x) & 0xFFF)

struct page_entry {
    u32 flags  : 9;  // page flags
    u32 avail  : 3;  // available for system
    u32 base   : 20; // base address
};
typedef struct page_entry page_entry_t;
//typedef u32 page_entry_t;

extern page_entry_t pdt_kernel[1024];

void switch_pdt(u32 pdt_addr);

void map(page_entry_t *pdt_now, u32 va, u32 pa, u32 flags);
void unmap(page_entry_t *pdt_now, u32 va);
u32 get_mapping(page_entry_t *pdt_now, u32 va, u32 *pa);
void init_page();
void page_fault(struct trap_frame *frame);

#endif
