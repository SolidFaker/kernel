#include "fs.h"
#include "mm.h"
#include "pipe.h"
#include "socket.h"

#define OFD_POOL 256
static struct ofd ofd_pool[OFD_POOL];

struct ofd *ofd_alloc(void)
{
    struct ofd *o = 0;
    int i;

    for (i = 0; i < OFD_POOL; i++) {
        if (!ofd_pool[i].used) {
            o = &ofd_pool[i];
            break;
        }
    }
    if (!o) {
        return 0;
    }
    o->used = 1;
    o->refs = 1;
    o->offset = 0;
    o->pipe = 0;
    o->sock = 0;
    o->pipe_write = 0;
    o->append = 0;
    o->nonblock = 0;
    o->path[0] = 0;
    return o;
}

struct ofd *ofd_get(struct ofd *o)
{
    if (o) {
        o->refs++;
    }
    return o;
}

void ofd_put(struct ofd *o)
{
    if (!o) {
        return;
    }
    if (--o->refs > 0) {
        return;
    }
    if (o->pipe) {
        pipe_end_close(o->pipe, o->pipe_write);
    }
    if (o->sock) {
        socket_put(o->sock);
    }
    o->used = 0;
}
