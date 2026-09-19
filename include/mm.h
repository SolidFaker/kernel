#ifndef MM_H
#define MM_H

#include "common.h"
#include "page.h"

// stack size of thread
#define USER_STACK_SIZE      2048
#define KERNEL_STACK_SIZE    2048
#define HEAP_START      0xE0000000
// max memory
#define MEMORY_SIZE     0x04000000
#define PAGE_STACK_SIZE (MEMORY_SIZE/PAGE_SIZE)

struct mmap_entry {
    u32 base_low;
    u32 base_high;
    u32 length_low;
    u32 length_high;
    u32 type;
}__attribute__((packed));
typedef struct mmap_entry mmap_entry_t;

struct memory_header {
    struct memory_header *next;
    struct memory_header *prev;
    u32 allocated :1;
    u32 length    :31;
};
typedef struct memory_header memory_header_t;

extern mmap_entry_t *mmap;
extern u32 *count;

extern u8 kernel_stack[KERNEL_STACK_SIZE];
extern u8 kernel_start_pos[];
extern u8 kernel_end_pos[];

void init_page_stack();
void page_free(u32 p);
u32  page_alloc();
void kmem_mark_user(void *p, u32 len);

void *kmalloc(u32 len);
void kfree(void *p);

#endif
