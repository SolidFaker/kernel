#include "pipe.h"
#include "errno.h"
#include "string.h"

#define PIPE_MAX 8

struct pipe {
    u8 used;
    u8 buf[PIPE_CAP];
    u32 head;
    u32 tail;
    u32 count;
    int readers;
    int writers;
};

static struct pipe pipes[PIPE_MAX];

struct pipe *pipe_create(void)
{
    int i;

    for (i = 0; i < PIPE_MAX; i++) {
        if (!pipes[i].used) {
            pipes[i].used = 1;
            pipes[i].head = 0;
            pipes[i].tail = 0;
            pipes[i].count = 0;
            pipes[i].readers = 0;
            pipes[i].writers = 0;
            return &pipes[i];
        }
    }
    return 0;
}

void pipe_destroy(struct pipe *p)
{
    if (p) {
        p->used = 0;
    }
}

void pipe_end_open(struct pipe *p, int write_end)
{
    if (!p) {
        return;
    }
    if (write_end) {
        p->writers++;
    } else {
        p->readers++;
    }
}

void pipe_end_close(struct pipe *p, int write_end)
{
    if (!p) {
        return;
    }
    if (write_end) {
        if (p->writers > 0) {
            p->writers--;
        }
    } else {
        if (p->readers > 0) {
            p->readers--;
        }
    }
    if (p->readers == 0 && p->writers == 0) {
        p->used = 0;
    }
}

u32 pipe_count(struct pipe *p)
{
    return p ? p->count : 0;
}

int pipe_read(struct pipe *p, u8 *buf, u32 len, int nonblock)
{
    u32 done = 0;

    if (!p) {
        return -EBADF;
    }
    while (done < len) {
        while (p->count == 0) {
            if (p->writers == 0) {
                return (int)done;
            }
            if (nonblock) {
                return done ? (int)done : -EAGAIN;
            }
            sti();
            hlt();
            cli();
        }
        buf[done++] = p->buf[p->tail];
        p->tail = (p->tail + 1) % PIPE_CAP;
        p->count--;
    }
    return (int)done;
}

int pipe_write(struct pipe *p, const u8 *buf, u32 len, int nonblock)
{
    u32 done = 0;

    if (!p) {
        return -EBADF;
    }
    while (done < len) {
        while (p->count == PIPE_CAP) {
            if (p->readers == 0) {
                return done ? (int)done : -EPIPE;
            }
            if (nonblock) {
                return done ? (int)done : -EAGAIN;
            }
            sti();
            hlt();
            cli();
        }
        p->buf[p->head] = buf[done++];
        p->head = (p->head + 1) % PIPE_CAP;
        p->count++;
    }
    return (int)done;
}

int pipe_readable(struct pipe *p)
{
    return p && (p->count > 0 || p->writers == 0);
}

int pipe_writable(struct pipe *p)
{
    return p && (p->count < PIPE_CAP || p->readers == 0);
}

int pipe_hup_read(struct pipe *p)
{
    return p && p->writers == 0;
}

int pipe_hup_write(struct pipe *p)
{
    return p && p->readers == 0;
}
