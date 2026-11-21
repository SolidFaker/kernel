#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arch/user.h>

struct blk {
    size_t size;
    struct blk *next;
    unsigned int free;
    unsigned int pad;
};

static struct blk *head;
static volatile int heap_lock;

static void lock_heap(void)
{
    while (__sync_lock_test_and_set(&heap_lock, 1)) {
        arch_syscall4(NR_futex, 0, (uaddr)&heap_lock, 1, 0);
    }
}

static void unlock_heap(void)
{
    __sync_lock_release(&heap_lock);
    arch_syscall4(NR_futex, 1, (uaddr)&heap_lock, 1, 0);
}

static size_t align_up(size_t n)
{
    return (n + 15) & ~(size_t)15;
}

void *malloc(size_t n)
{
    struct blk *b;

    if (n == 0) {
        n = 1;
    }
    n = align_up(n);
    lock_heap();
    for (b = head; b; b = b->next) {
        if (b->free && b->size >= n) {
            b->free = 0;
            unlock_heap();
            return b + 1;
        }
    }

    b = (struct blk *)sbrk((int)(sizeof(struct blk) + n));
    if (b == (struct blk *)-1) {
        unlock_heap();
        return 0;
    }
    b->size = n;
    b->free = 0;
    b->next = head;
    head = b;
    unlock_heap();
    return b + 1;
}

void free(void *p)
{
    struct blk *b;

    if (!p) {
        return;
    }
    b = (struct blk *)p - 1;
    lock_heap();
    b->free = 1;
    unlock_heap();
}

void *calloc(size_t n, size_t size)
{
    size_t total = n * size;
    void *p;

    if (n && total / n != size) {
        return 0;
    }
    p = malloc(total);
    if (p) {
        memset(p, 0, total);
    }
    return p;
}

void *realloc(void *p, size_t n)
{
    struct blk *b;
    void *np;

    if (!p) {
        return malloc(n);
    }
    if (n == 0) {
        free(p);
        return 0;
    }
    b = (struct blk *)p - 1;
    if (b->size >= n) {
        return p;
    }
    np = malloc(n);
    if (!np) {
        return 0;
    }
    memcpy(np, p, b->size);
    free(p);
    return np;
}
