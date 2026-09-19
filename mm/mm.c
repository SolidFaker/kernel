#include "debug.h"
#include "mm.h"
#include "page.h"
#include "init.h"
#include "string.h"
#include "tools.h"
#include "task.h"

u8 kernel_stack[KERNEL_STACK_SIZE];
mmap_entry_t *mmap = (mmap_entry_t *)(0x1000 + PAGE_OFFSET);
u32 *count = (u32 *)(0x500 + PAGE_OFFSET);

memory_header_t *heap_first = 0;
static void alloc_chunk(u32 start, u32 len);
static void free_chunk(memory_header_t *chunk);
static void split_chunk(memory_header_t *chunk, u32 len);
static void glue_chunk(memory_header_t *chunk);

static u32 heap_max = HEAP_START;
static u32 page_stack[PAGE_STACK_SIZE];
static u32 page_stack_top = 0;

void init_page_stack()
{
    mmap_entry_t *map_entry = mmap;

    u8 i;
    // entry 0 is the legacy low memory region, skipped on purpose:
    // boot scratch areas (mmap/count/temp page tables) live below 0x5000.
    // Regions above 4GB are unusable on 32-bit; base_high is ignored and
    // anything past MEMORY_SIZE would overflow the page stack anyway.
    for (i = 1; i < *count; i++) {
        // If this memory section is usable
        if ((map_entry+i)->type != 0x1) {
            continue;
        }

        u32 page_addr = (map_entry+i)->base_low;
        u32 end = page_addr + (map_entry+i)->length_low;
        if (end > MEMORY_SIZE) {
            end = MEMORY_SIZE;
        }

        while (page_addr < end) {
            if (page_addr < (u32)kernel_start_pos ||
                    page_addr > (u32)kernel_end_pos + 0x1000)
            {
                page_free(page_addr);
            }
            page_addr += PAGE_SIZE;
        }
    }
}

u32 page_alloc()
{
    assert(page_stack_top > 0, "out of memory");
    u32 page = page_stack[page_stack_top--];
    return page;
}

void page_free(u32 p)
{
    assert(page_stack_top < PAGE_STACK_SIZE - 1, "page stack overflow");
    page_stack[++page_stack_top] = p;
}

// Grant CPL3 access to the pages backing [p, p+len). Used for the per-task
// user stack; the rest of the kernel heap stays supervisor-only.
void kmem_mark_user(void *p, u32 len)
{
    u32 va = (u32)p & PAGE_MASK;
    u32 end = ((u32)p + len + PAGE_SIZE - 1) & PAGE_MASK;

    for (; va < end; va += PAGE_SIZE) {
        u32 pa;
        if (get_mapping(pdt_kernel, va, &pa)) {
            map(pdt_kernel, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
        }
    }
}

u32 kernel_pdt_phys(void)
{
    return (u32)pdt_kernel - PAGE_OFFSET;
}

// Build an empty user address space. The kernel half of the directory
// (entries 768..1023) is shared with pdt_kernel by referencing the very
// same page tables, so kernel text/data/heap stay mapped (and stay in
// sync for map()s inside existing tables) in every task. Only a page
// table that appears AFTER this point (kernel heap growing past its
// current 4MB region) would be missed - not a case this kernel hits.
struct mm_struct *mm_create(void)
{
    struct mm_struct *mm =
        (struct mm_struct *)kmalloc(sizeof(struct mm_struct));
    u32 pdt_pa = page_alloc();

    mm->pdt = (page_entry_t *)(pdt_pa + PAGE_OFFSET);
    mm->pdt_phys = pdt_pa;
    bzero((u8 *)mm->pdt, PAGE_SIZE);

    u32 i;
    for (i = PDT_INDEX(PAGE_OFFSET); i < PAGE_TABLE_SIZE; i++) {
        mm->pdt[i] = pdt_kernel[i];
    }
    return mm;
}

// Duplicate an address space: every user page is copied (no COW), so
// fork() children get private memory immediately.
struct mm_struct *mm_copy(struct mm_struct *src)
{
    struct mm_struct *mm = mm_create();
    u32 i, j;

    for (i = 0; i < PDT_INDEX(PAGE_OFFSET); i++) {
        if (!(src->pdt[i].flags & PG_PRESENT)) {
            continue;
        }
        page_entry_t *spet =
            (page_entry_t *)((u32)(src->pdt[i].base << 12) + PAGE_OFFSET);
        u32 dpet_pa = page_alloc();
        page_entry_t *dpet = (page_entry_t *)(dpet_pa + PAGE_OFFSET);
        bzero((u8 *)dpet, PAGE_SIZE);

        mm->pdt[i] = src->pdt[i];
        mm->pdt[i].base = dpet_pa >> 12;

        for (j = 0; j < PAGE_TABLE_SIZE; j++) {
            if (!(spet[j].flags & PG_PRESENT)) {
                continue;
            }
            u32 pa = page_alloc();
            memcpy((u8 *)(pa + PAGE_OFFSET),
                   (u8 *)((u32)(spet[j].base << 12) + PAGE_OFFSET),
                   PAGE_SIZE);
            dpet[j].base = pa >> 12;
            dpet[j].flags = spet[j].flags;
        }
    }
    return mm;
}

// Must not run while CR3 still points at mm->pdt_phys.
void mm_destroy(struct mm_struct *mm)
{
    u32 i, j;

    for (i = 0; i < PDT_INDEX(PAGE_OFFSET); i++) {
        if (!(mm->pdt[i].flags & PG_PRESENT)) {
            continue;
        }
        page_entry_t *pet =
            (page_entry_t *)((u32)(mm->pdt[i].base << 12) + PAGE_OFFSET);
        for (j = 0; j < PAGE_TABLE_SIZE; j++) {
            if (pet[j].flags & PG_PRESENT) {
                page_free(pet[j].base << 12);
            }
        }
        page_free(mm->pdt[i].base << 12);
    }
    page_free(mm->pdt_phys);
    kfree(mm);
}

void *kmalloc(u32 len)
{
    len += sizeof(memory_header_t);
    memory_header_t *header_cur = heap_first;
    memory_header_t *header_prev = 0;

    while (header_cur){
        if (header_cur->allocated == 0 && header_cur->length >= len){
            // Split memory by length
            split_chunk(header_cur, len);
            header_cur->allocated = 1;
            // return memory address memory header is ahead of memory chunk
            return (void *)((u32)header_cur + sizeof(memory_header_t));
        }
        header_prev = header_cur;
        header_cur = header_cur->next;
    }

    u32 chunk_start;

    if (header_prev){
        chunk_start = (u32)header_prev + header_prev->length;
    } else {
        chunk_start = HEAP_START;
        heap_first = (memory_header_t *)chunk_start;
    }
    // need for a new page?
    alloc_chunk(chunk_start, len);
    header_cur = (memory_header_t *)(chunk_start);
    header_cur->prev = header_prev;
    header_cur->next = 0;
    header_cur->allocated = 1;
    header_cur->length = len;

    if (header_prev) {
        header_prev->next = header_cur;
    }

    return (void*)(chunk_start + sizeof(memory_header_t));
}

void kfree(void *p)
{
    // relesse header and reset allocated flag
    memory_header_t *header = (memory_header_t*)((u32)p - sizeof(memory_header_t));
    header->allocated = 0;

    // glue memory chunk
    glue_chunk(header);
}

void alloc_chunk(u32 start, u32 len)
{
    // 如果当前堆的位置已经到达界限则申请内存页
    // 必须循环申请内存页直到有到足够的可用内存
    while (start + len > heap_max) {
        u32 page = page_alloc();
        map(pdt_kernel, heap_max, page, PG_PRESENT | PG_WRITE);
        heap_max += PAGE_SIZE;
    }
}

void free_chunk(memory_header_t *chunk)
{
    if (chunk->prev == 0) {
        heap_first = 0;
    } else {
        chunk->prev->next = 0;
    }

    // 空闲的内存超过 1 页的话就释放掉
    while ((heap_max - PAGE_SIZE) >= (u32)chunk) {
        heap_max -= PAGE_SIZE;
        u32 page;
        get_mapping(pdt_kernel, heap_max, &page);
        unmap(pdt_kernel, heap_max);
        page_free(page);
    }
}

void split_chunk(memory_header_t *chunk, u32 len)
{
    // 切分内存块之前得保证之后的剩余内存至少容纳一个内存管理块的大小
    // the remainder starts right after the kept part; the old code placed
    // the header at the chunk END, overwriting the next chunk's header
    // (and leaving its ->next pointing at itself, hanging later walks)
    if (chunk->length - len > sizeof (memory_header_t)) {
        memory_header_t *newchunk = (memory_header_t *)((u32)chunk + len);
        newchunk->prev = chunk;
        newchunk->next = chunk->next;
        newchunk->allocated = 0;
        newchunk->length = chunk->length - len;

        if (newchunk->next) {
            newchunk->next->prev = newchunk;
        }
        chunk->next = newchunk;
        chunk->length = len;
    }
}

void glue_chunk(memory_header_t *chunk)
{
    // 如果该内存块前面有链内存块且未被使用则拼合
    if (chunk->next && chunk->next->allocated == 0) {
        chunk->length = chunk->length + chunk->next->length;
        if (chunk->next->next) {
            chunk->next->next->prev = chunk;
        }
        chunk->next = chunk->next->next;
    }

    if (chunk->prev && chunk->prev->allocated == 0) {
        chunk->prev->length = chunk->prev->length + chunk->length;
        chunk->prev->next = chunk->next;
        if (chunk->next) {
            chunk->next->prev = chunk->prev;
        }
        chunk = chunk->prev;
    }

    // 假如该内存后面没有链表内存块了直接释放掉
    if (chunk->next == 0) {
        free_chunk(chunk);
    }
}
