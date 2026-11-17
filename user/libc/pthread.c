#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/mman.h>

#define THREAD_STACK (64 * 1024)
#define MAX_THREADS 32

struct thread_arg {
    void *(*fn)(void *);
    void *arg;
};

struct thread_slot {
    pthread_t tid;
    void *stack;
    unsigned int size;
    int used;
};

static struct thread_slot slots[MAX_THREADS];

static int thr_sys3(int nr, int a, int b, int c)
{
    int r;
    __asm__ volatile("int $0x80" : "=a" (r)
                     : "0" (nr), "b" (a), "c" (b), "d" (c)
                     : "memory");
    return r;
}

static int futex_wait(volatile int *addr, int val)
{
    return thr_sys3(NR_futex, 0, (int)addr, val);
}

static int futex_wake(volatile int *addr, int count)
{
    return thr_sys3(NR_futex, 1, (int)addr, count);
}

static void thread_entry(struct thread_arg *ta)
{
    void *ret = ta->fn(ta->arg);
    free(ta);
    pthread_exit(ret);
}

pthread_t pthread_self(void)
{
    return (pthread_t)getpid();
}

int pthread_equal(pthread_t a, pthread_t b)
{
    return a == b;
}

int pthread_create(pthread_t *tid, const pthread_attr_t *attr,
                   void *(*start)(void *), void *arg)
{
    struct thread_arg *ta;
    void *stack;
    int id, i;

    (void)attr;
    stack = mmap(0, THREAD_STACK, PROT_READ | PROT_WRITE,
                 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (stack == MAP_FAILED) {
        return -1;
    }
    ta = (struct thread_arg *)malloc(sizeof(*ta));
    if (!ta) {
        munmap(stack, THREAD_STACK);
        return -1;
    }
    ta->fn = start;
    ta->arg = arg;

    id = thr_sys3(NR_thread_create, (int)thread_entry, (int)ta,
                  (int)stack + THREAD_STACK);
    if (id < 0) {
        errno = -id;
        free(ta);
        munmap(stack, THREAD_STACK);
        return -1;
    }
    for (i = 0; i < MAX_THREADS; i++) {
        if (!slots[i].used) {
            slots[i].used = 1;
            slots[i].tid = (pthread_t)id;
            slots[i].stack = stack;
            slots[i].size = THREAD_STACK;
            break;
        }
    }
    if (tid) {
        *tid = (pthread_t)id;
    }
    return 0;
}

int pthread_join(pthread_t tid, void **retval)
{
    int st = 0;
    int r = thr_sys3(NR_thread_wait, (int)tid, (int)&st, 0);
    int i;

    if (r < 0) {
        errno = -r;
        return -1;
    }
    for (i = 0; i < MAX_THREADS; i++) {
        if (slots[i].used && slots[i].tid == tid) {
            munmap(slots[i].stack, slots[i].size);
            slots[i].used = 0;
            break;
        }
    }
    if (retval) {
        *retval = (void *)st;
    }
    return 0;
}

int pthread_detach(pthread_t tid)
{
    (void)tid;
    return 0;
}

void pthread_exit(void *retval)
{
    thr_sys3(NR_thread_exit, (int)retval, 0, 0);
    for (;;) {
    }
}

int pthread_mutex_init(pthread_mutex_t *m, const void *attr)
{
    (void)attr;
    m->lock = 0;
    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *m)
{
    (void)m;
    return 0;
}

int pthread_mutex_lock(pthread_mutex_t *m)
{
    while (__sync_lock_test_and_set(&m->lock, 1)) {
        futex_wait(&m->lock, 1);
    }
    return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *m)
{
    if (__sync_lock_test_and_set(&m->lock, 1)) {
        return EBUSY;
    }
    return 0;
}

int pthread_mutex_unlock(pthread_mutex_t *m)
{
    __sync_lock_release(&m->lock);
    futex_wake(&m->lock, 1);
    return 0;
}

int pthread_cond_init(pthread_cond_t *c, const void *attr)
{
    (void)attr;
    c->seq = 0;
    return 0;
}

int pthread_cond_destroy(pthread_cond_t *c)
{
    (void)c;
    return 0;
}

int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m)
{
    unsigned int seq = c->seq;
    pthread_mutex_unlock(m);
    futex_wait((volatile int *)&c->seq, (int)seq);
    pthread_mutex_lock(m);
    return 0;
}

int pthread_cond_signal(pthread_cond_t *c)
{
    __sync_add_and_fetch(&c->seq, 1);
    futex_wake((volatile int *)&c->seq, 1);
    return 0;
}

int pthread_cond_broadcast(pthread_cond_t *c)
{
    __sync_add_and_fetch(&c->seq, 1);
    futex_wake((volatile int *)&c->seq, 0x7fffffff);
    return 0;
}
