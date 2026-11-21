#include "debug.h"
#include "mm.h"
#include "page.h"
#include "init.h"
#include "string.h"
#include "tools.h"
#include "task.h"

u8 kernel_stack[KERNEL_STACK_SIZE];
mmap_entry_t *mmap = 0;
static u32 no_maps;
u32 *count = &no_maps;

static memory_header_t *heap_first;
static uaddr heap_max = HEAP_START;
static uaddr page_stack[PAGE_STACK_SIZE];
static u32 page_stack_top;

static void alloc_chunk(uaddr start, u32 len);
static void split_chunk(memory_header_t *chunk, u32 len);
static void glue_chunk(memory_header_t *chunk);

void init_page_stack(void)
{
    uaddr page = ((uaddr)kernel_end_pos - KERNEL_VIRT_BASE + PAGE_SIZE - 1) &
        PAGE_MASK;
    while (page < RAM_PHYS_END) {
        page_free(page);
        page += PAGE_SIZE;
    }
}

uaddr page_alloc(void)
{
    uaddr pa;
    assert(page_stack_top > 0, "out of memory");
    pa = page_stack[--page_stack_top];
    assert(pa >= RAM_PHYS_START && pa < RAM_PHYS_END, "bad physical page");
    return pa;
}

void page_free(uaddr pa)
{
    assert(pa >= RAM_PHYS_START && pa < RAM_PHYS_END && !(pa & ~PAGE_MASK),
           "bad physical page free");
    assert(page_stack_top < PAGE_STACK_SIZE, "page stack overflow");
    page_stack[page_stack_top++] = pa;
}

uaddr kernel_pdt_phys(void)
{
    return 0x9000;
}

void kmem_mark_user(void *p, u32 len)
{
    (void)p;
    (void)len;
}

static uaddr clone_table(uaddr src_pa, int level)
{
    page_entry_t *src = (page_entry_t *)(src_pa + PAGE_OFFSET);
    uaddr dst_pa = page_alloc();
    page_entry_t *dst = (page_entry_t *)(dst_pa + PAGE_OFFSET);
    u32 i;
    memset((u8 *)dst, 0, PAGE_SIZE);
    for (i = 0; i < 512; i++) {
        uaddr raw = src[i].raw;
        if (!(raw & PG_PRESENT)) continue;
        if (level && !(raw & PG_SIZE)) {
            dst[i].raw = clone_table(raw & PAGE_MASK, level - 1) |
                (raw & ~PAGE_MASK);
        } else if (!level && (raw & PG_USER)) {
            uaddr pa = page_alloc();
            memcpy((u8 *)(pa + PAGE_OFFSET),
                   (const u8 *)((raw & PAGE_MASK) + PAGE_OFFSET), PAGE_SIZE);
            dst[i].raw = pa | (raw & ~PAGE_MASK);
        } else {
            dst[i].raw = raw;
        }
    }
    return dst_pa;
}

struct mm_struct *mm_create(void)
{
    struct mm_struct *mm = (struct mm_struct *)kmalloc(sizeof(*mm));
    uaddr pa = page_alloc();
    u32 i;
    mm->pdt = (page_entry_t *)(pa + PAGE_OFFSET);
    mm->pdt_phys = pa;
    mm->refs = 1;
    memset((u8 *)mm->pdt, 0, PAGE_SIZE);
    for (i = 256; i < 512; i++) {
        uaddr raw = pdt_kernel[i].raw;
        if (raw & PG_PRESENT)
            mm->pdt[i].raw = clone_table(raw & PAGE_MASK, 2) |
                (raw & ~PAGE_MASK);
    }
    return mm;
}

struct mm_struct *mm_copy(struct mm_struct *src)
{
    struct mm_struct *mm = (struct mm_struct *)kmalloc(sizeof(*mm));
    mm->pdt_phys = clone_table(src->pdt_phys, 3);
    mm->pdt = (page_entry_t *)(mm->pdt_phys + PAGE_OFFSET);
    mm->refs = 1;
    return mm;
}

void mm_get(struct mm_struct *mm)
{
    if (mm) mm->refs++;
}

static void free_table(uaddr table_pa, int level)
{
    page_entry_t *table = (page_entry_t *)(table_pa + PAGE_OFFSET);
    u32 i;
    for (i = 0; i < 512; i++) {
        uaddr raw = table[i].raw;
        if (!(raw & PG_PRESENT)) continue;
        if (level && !(raw & PG_SIZE)) free_table(raw & PAGE_MASK, level - 1);
        else if (!level && (raw & PG_USER)) page_free(raw & PAGE_MASK);
    }
    page_free(table_pa);
}

void mm_destroy(struct mm_struct *mm)
{
    if (!mm) return;
    free_table(mm->pdt_phys, 3);
    kfree(mm);
}

void mm_put(struct mm_struct *mm)
{
    if (mm && --mm->refs == 0) mm_destroy(mm);
}

void *kmalloc(u32 len)
{
    memory_header_t *header_cur, *header_prev = 0;
    uaddr chunk_start;
    len = (len + 15) & ~15u;
    len += sizeof(memory_header_t);
    header_cur = heap_first;
    while (header_cur) {
        if (!header_cur->allocated && header_cur->length >= len) {
            split_chunk(header_cur, len);
            header_cur->allocated = 1;
            return (void *)((uaddr)header_cur + sizeof(memory_header_t));
        }
        header_prev = header_cur;
        header_cur = header_cur->next;
    }
    chunk_start = header_prev ? (uaddr)header_prev + header_prev->length :
        HEAP_START;
    if (!header_prev) heap_first = (memory_header_t *)chunk_start;
    alloc_chunk(chunk_start, len);
    header_cur = (memory_header_t *)chunk_start;
    header_cur->prev = header_prev;
    header_cur->next = 0;
    header_cur->allocated = 1;
    header_cur->length = len;
    if (header_prev) header_prev->next = header_cur;
    return (void *)(chunk_start + sizeof(memory_header_t));
}

void kfree(void *p)
{
    memory_header_t *header = (memory_header_t *)((uaddr)p - sizeof(*header));
    header->allocated = 0;
    glue_chunk(header);
}

static void alloc_chunk(uaddr start, u32 len)
{
    while (start + len > heap_max) {
        uaddr pa = page_alloc();
        map(pdt_kernel, heap_max, pa, PG_PRESENT | PG_WRITE);
        heap_max += PAGE_SIZE;
    }
}

static void split_chunk(memory_header_t *chunk, u32 len)
{
    if (chunk->length - len > sizeof(memory_header_t)) {
        memory_header_t *next = (memory_header_t *)((uaddr)chunk + len);
        next->prev = chunk;
        next->next = chunk->next;
        next->allocated = 0;
        next->length = chunk->length - len;
        if (next->next) next->next->prev = next;
        chunk->next = next;
        chunk->length = len;
    }
}

static void glue_chunk(memory_header_t *chunk)
{
    if (chunk->next && !chunk->next->allocated) {
        chunk->length += chunk->next->length;
        if (chunk->next->next) chunk->next->next->prev = chunk;
        chunk->next = chunk->next->next;
    }
    if (chunk->prev && !chunk->prev->allocated) {
        chunk->prev->length += chunk->length;
        chunk->prev->next = chunk->next;
        if (chunk->next) chunk->next->prev = chunk->prev;
    }
}
