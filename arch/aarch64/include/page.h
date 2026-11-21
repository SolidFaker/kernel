#ifndef PAGE_H
#define PAGE_H
#include "common.h"
#include "init.h"
#define PG_PRESENT 1
#define PG_WRITE 2
#define PG_USER 4
#define PG_SIZE 0x80
#define PG_ACCESSED 0x20
#define PG_DIRTY 0x40
#define PAGE_SIZE 0x1000
#define PAGE_MASK (~(uaddr)0xfff)
#define PHYS_ADDR_MASK 0x0000fffffffff000ULL
#define PAGE_OFFSET 0
#define USER_ADDR_LIMIT 0x40000000ULL
#define PAGE_TABLE_SIZE 512
#define PDT_ENTRY_NUM 512
#define PDT_INDEX(x) (((uaddr)(x) >> 39) & 0x1ff)
#define PET_INDEX(x) (((uaddr)(x) >> 12) & 0x1ff)
struct page_entry { uaddr raw; };
typedef struct page_entry page_entry_t;
extern page_entry_t *pdt_kernel;
void switch_pdt(uaddr pdt_addr);
void map(page_entry_t *pdt_now, uaddr va, uaddr pa, u32 flags);
void unmap(page_entry_t *pdt_now, uaddr va);
u32 get_mapping(page_entry_t *pdt_now, uaddr va, uaddr *pa);
void init_page(void);
void page_fault(struct trap_frame *frame);
#endif
