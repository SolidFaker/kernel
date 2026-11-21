#ifndef MM_H
#define MM_H
#include "common.h"
#include "page.h"

#define USER_STACK_SIZE 2048
#define KERNEL_STACK_SIZE 16384
#define HEAP_START 0xffffffff90000000ULL
#define MMAP_BASE 0x10000000ULL
#define MEMORY_SIZE 0x08000000ULL
#define RAM_PHYS_START 0x100000ULL
#define RAM_PHYS_END 0x08000000ULL
#define PAGE_STACK_SIZE (MEMORY_SIZE / PAGE_SIZE)
#define USER_STACK_VA_TOP 0x08000000ULL
#define USER_STACK_PAGES 16

struct mmap_entry {
    u32 base_low, base_high, length_low, length_high, type;
} __attribute__((packed));
typedef struct mmap_entry mmap_entry_t;
struct memory_header {
    struct memory_header *next, *prev;
    u32 allocated : 1;
    u32 length : 31;
    u64 pad;
};
typedef struct memory_header memory_header_t;
extern mmap_entry_t *mmap;
extern u32 *count;
extern u8 kernel_stack[KERNEL_STACK_SIZE];
extern u8 kernel_start_pos[], kernel_end_pos[];
void init_page_stack(void);
void page_free(uaddr p);
uaddr page_alloc(void);
void kmem_mark_user(void *p, u32 len);
uaddr kernel_pdt_phys(void);
struct mm_struct *mm_create(void);
struct mm_struct *mm_copy(struct mm_struct *src);
void mm_destroy(struct mm_struct *mm);
void mm_get(struct mm_struct *mm);
void mm_put(struct mm_struct *mm);
void *kmalloc(u32 len);
void kfree(void *p);
#endif
