// AF_UNIX stream sockets with an in-kernel byte-stream ring buffer.
// This is enough for socketpair(), local bind/listen/accept/connect and
// poll/select readiness without a network stack.

#include "socket.h"
#include "errno.h"
#include "mm.h"
#include "string.h"
#include "posix_abi.h"
#include "net.h"
#include "drivers/timer.h"
#include "task.h"
#include "tools.h"

#define SOCK_MAX 32
#define SOCK_BUF 4096

struct sockbuf {
    u8 data[SOCK_BUF];
    u32 head;
    u32 tail;
    u32 count;
};

struct dgram {
    struct dgram *next;
    u32 addr;
    u16 port;
    u16 len;
    u8 data[1472];
};

struct socket {
    u8 used;
    u8 state;
    u8 shutdown_r;
    u8 shutdown_w;
    u8 family;
    u8 type;
    u8 real_tcp;
    u8 tcp_state;
    u8 peer_closed;
    int refs;
    u32 snd_nxt;
    u32 snd_una;
    u32 rcv_nxt;
    u32 tcp_iss;
    u32 laddr;
    u16 lport;
    u32 raddr;
    u16 rport;
    struct dgram *dhead;
    struct dgram *dtail;
    int backlog;
    char path[108];
    struct socket *peer;
    struct sockbuf rbuf;
    struct socket *accept_head;
    struct socket *accept_tail;
    struct socket *accept_next;
};

#define TCP_SYN 0x02
#define TCP_ACK 0x10
#define TCP_FIN 0x01
#define TCP_PSH 0x08
#define TCP_RST 0x04

#define TS_CLOSED      0
#define TS_SYN_SENT    1
#define TS_ESTABLISHED 2
#define TS_FIN_WAIT    3
#define TS_CLOSE_WAIT  4
#define TS_TIME_WAIT   5

static struct socket socks[SOCK_MAX];
static u16 next_ephemeral = 40000;

void socket_set_type(struct socket *s, int family, int type)
{
    if (s) {
        s->family = (u8)family;
        s->type = (u8)type;
    }
}

int socket_family(struct socket *s)
{
    return s ? s->family : 0;
}

int socket_type(struct socket *s)
{
    return s ? s->type : 0;
}

struct socket *socket_new(void)
{
    int i;
    for (i = 0; i < SOCK_MAX; i++) {
        if (!socks[i].used) {
            memset((u8 *)&socks[i], 0, sizeof(socks[i]));
            socks[i].used = 1;
            socks[i].refs = 1;
            socks[i].state = SOCK_ST_UNBOUND;
            socks[i].family = AF_UNIX;
            socks[i].type = SOCK_STREAM;
            return &socks[i];
        }
    }
    return 0;
}

static void drain_accept(struct socket *s)
{
    struct socket *c = s->accept_head;
    while (c) {
        struct socket *n = c->accept_next;
        c->accept_next = 0;
        socket_put(c);
        c = n;
    }
    s->accept_head = 0;
    s->accept_tail = 0;
}

void socket_put(struct socket *s)
{
    if (!s || !s->used) {
        return;
    }
    if (--s->refs > 0) {
        return;
    }
    if (s->real_tcp) {
        if (s->tcp_state == TS_ESTABLISHED ||
                s->tcp_state == TS_CLOSE_WAIT) {
            net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt,
                         s->rcv_nxt, TCP_ACK | TCP_FIN, 0, 0);
        } else if (s->tcp_state == TS_SYN_SENT) {
            net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt, 0,
                         TCP_RST, 0, 0);
        }
        s->tcp_state = TS_CLOSED;
    }
    if (s->state == SOCK_ST_LISTEN) {
        drain_accept(s);
    }
    while (s->dhead) {
        struct dgram *d = s->dhead;
        s->dhead = d->next;
        kfree(d);
    }
    s->dtail = 0;
    if (s->peer) {
        s->peer->peer = 0;
        s->peer = 0;
    }
    s->used = 0;
}

int socket_pair(struct socket *a, struct socket *b)
{
    a->peer = b;
    b->peer = a;
    a->state = SOCK_ST_CONNECTED;
    b->state = SOCK_ST_CONNECTED;
    return 0;
}

int socket_bind(struct socket *s, const char *path)
{
    u32 i;

    if (!s || s->state != SOCK_ST_UNBOUND) {
        return -EINVAL;
    }
    if (!path || !path[0] || strlen(path) >= (int)sizeof(s->path)) {
        return -EINVAL;
    }
    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].state != SOCK_ST_UNBOUND &&
                socks[i].state != SOCK_ST_CONNECTED &&
                !strcmp(socks[i].path, (char *)path)) {
            return -EADDRINUSE;
        }
    }
    strcpy(s->path, (char *)path);
    s->state = SOCK_ST_BOUND;
    return 0;
}

int socket_listen(struct socket *s, int backlog)
{
    if (!s || (s->state != SOCK_ST_BOUND && s->state != SOCK_ST_LISTEN)) {
        return -EINVAL;
    }
    s->state = SOCK_ST_LISTEN;
    s->backlog = backlog <= 0 ? 1 : (backlog > 16 ? 16 : backlog);
    return 0;
}

int socket_connect(struct socket *s, const char *path)
{
    struct socket *listen = 0;
    struct socket *server;
    int i;

    if (!s || s->state == SOCK_ST_LISTEN || s->state == SOCK_ST_CONNECTED) {
        return -EINVAL;
    }
    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].state == SOCK_ST_LISTEN &&
                !strcmp(socks[i].path, (char *)path)) {
            listen = &socks[i];
            break;
        }
    }
    if (!listen) {
        return -ECONNREFUSED;
    }

    server = socket_new();
    if (!server) {
        return -ENOMEM;
    }
    strcpy(server->path, listen->path);
    server->state = SOCK_ST_CONNECTED;
    s->state = SOCK_ST_CONNECTED;
    s->peer = server;
    server->peer = s;

    server->accept_next = 0;
    if (listen->accept_tail) {
        listen->accept_tail->accept_next = server;
    } else {
        listen->accept_head = server;
    }
    listen->accept_tail = server;
    return 0;
}

int socket_accept(struct socket *s, int nonblock, struct socket **out)
{
    struct socket *c;

    *out = 0;
    if (!s || s->state != SOCK_ST_LISTEN) {
        return -EINVAL;
    }
    for (;;) {
        c = s->accept_head;
        if (c) {
            s->accept_head = c->accept_next;
            if (!s->accept_head) {
                s->accept_tail = 0;
            }
            c->accept_next = 0;
            *out = c;
            return 0;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        sti();
        hlt();
        cli();
        if (!s->used) {
            return -ENOTCONN;
        }
    }
}

static u16 alloc_port(void)
{
    u16 p = next_ephemeral++;
    if (next_ephemeral >= 60000) {
        next_ephemeral = 40000;
    }
    return p;
}

int socket_bind_inet(struct socket *s, u32 addr, u16 port)
{
    int i;

    if (!s || s->state != SOCK_ST_UNBOUND) {
        return -EINVAL;
    }
    if (port == 0) {
        port = alloc_port();
    }
    for (i = 0; i < SOCK_MAX; i++) {
        if (&socks[i] != s && socks[i].used && socks[i].family == AF_INET &&
                socks[i].lport == port && socks[i].state != SOCK_ST_UNBOUND) {
            return -EADDRINUSE;
        }
    }
    s->family = AF_INET;
    s->laddr = addr;
    s->lport = port;
    s->state = SOCK_ST_BOUND;
    return 0;
}

int socket_connect_inet(struct socket *s, u32 addr, u16 port)
{
    struct socket *listen = 0;
    struct socket *server;
    int i;

    if (!s || s->type != SOCK_STREAM || s->state == SOCK_ST_LISTEN ||
            s->state == SOCK_ST_CONNECTED) {
        return -EINVAL;
    }
    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].family == AF_INET &&
                socks[i].state == SOCK_ST_LISTEN &&
                socks[i].lport == port &&
                (socks[i].laddr == INADDR_ANY || socks[i].laddr == addr)) {
            listen = &socks[i];
            break;
        }
    }
    if (!listen) {
        return -ECONNREFUSED;
    }
    if (s->lport == 0) {
        s->lport = alloc_port();
        s->laddr = INADDR_LOOPBACK;
    }
    server = socket_new();
    if (!server) {
        return -ENOMEM;
    }
    server->family = AF_INET;
    server->type = SOCK_STREAM;
    server->laddr = listen->laddr;
    server->lport = listen->lport;
    server->raddr = s->laddr;
    server->rport = s->lport;
    server->state = SOCK_ST_CONNECTED;
    s->state = SOCK_ST_CONNECTED;
    s->raddr = listen->laddr;
    s->rport = listen->lport;
    s->peer = server;
    server->peer = s;

    server->accept_next = 0;
    if (listen->accept_tail) {
        listen->accept_tail->accept_next = server;
    } else {
        listen->accept_head = server;
    }
    listen->accept_tail = server;
    return 0;
}

int socket_connect_real(struct socket *s, u32 addr, u16 port)
{
    int r;
    u32 start;

    if (!s || s->type != SOCK_STREAM) {
        return -EINVAL;
    }
    if (s->lport == 0) {
        s->lport = alloc_port();
        s->laddr = net_ip();
    }
    s->family = AF_INET;
    s->real_tcp = 1;
    s->raddr = addr;
    s->rport = port;
    s->state = SOCK_ST_CONNECTED;
    s->tcp_state = TS_SYN_SENT;
    s->tcp_iss = (u32)tick * 2654435761u + s->lport;
    s->snd_nxt = s->tcp_iss + 1;
    s->snd_una = s->tcp_iss;
    s->rcv_nxt = 0;

    r = net_send_tcp(addr, port, s->lport, s->tcp_iss, 0, TCP_SYN, 0, 0);
    if (r < 0) {
        s->tcp_state = TS_CLOSED;
        return r;
    }
    start = tick;
    while (s->tcp_state == TS_SYN_SENT && tick - start < 3 * TIMER_HZ) {
        if (current->pending_sig & ~current->blocked_sig) {
            return -EINTR;
        }
        net_poll();
        sti();
        hlt();
        cli();
    }
    return s->tcp_state == TS_ESTABLISHED ? 0 : -ETIMEDOUT;
}

void socket_tcp_input(u32 dst_ip, u16 dst_port, u32 src_ip, u16 src_port,
                      u32 seq, u32 ack, u8 flags, const u8 *data, u32 len)
{
    int i;
    struct socket *s = 0;
    u32 fl = local_irq_save();

    (void)dst_ip;
    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].real_tcp &&
                socks[i].lport == dst_port &&
                socks[i].raddr == src_ip && socks[i].rport == src_port) {
            s = &socks[i];
            break;
        }
    }
    if (!s) {
        local_irq_restore(fl);
        return;
    }
    if (flags & TCP_RST) {
        s->tcp_state = TS_CLOSED;
        s->peer_closed = 1;
        local_irq_restore(fl);
        return;
    }
    if (s->tcp_state == TS_SYN_SENT) {
        if ((flags & TCP_SYN) && (flags & TCP_ACK) && ack == s->snd_nxt) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->tcp_state = TS_ESTABLISHED;
            net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt,
                         s->rcv_nxt, TCP_ACK, 0, 0);
        }
        local_irq_restore(fl);
        return;
    }

    if (flags & TCP_ACK) {
        if (ack > s->snd_una) {
            s->snd_una = ack;
        }
    }
    if (len) {
        if (seq == s->rcv_nxt) {
            u32 space = SOCK_BUF - s->rbuf.count;
            u32 n = len < space ? len : space;
            u32 k;
            for (k = 0; k < n; k++) {
                s->rbuf.data[s->rbuf.head] = data[k];
                s->rbuf.head = (s->rbuf.head + 1) % SOCK_BUF;
                s->rbuf.count++;
            }
            s->rcv_nxt += len;
        }
        net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt, s->rcv_nxt,
                     TCP_ACK, 0, 0);
    }
    if (flags & TCP_FIN) {
        s->rcv_nxt++;
        s->peer_closed = 1;
        net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt, s->rcv_nxt,
                     TCP_ACK, 0, 0);
        s->tcp_state = (s->tcp_state == TS_FIN_WAIT) ?
            TS_TIME_WAIT : TS_CLOSE_WAIT;
    }
    local_irq_restore(fl);
}

static int tcp_write(struct socket *s, const u8 *buf, u32 len, int nonblock)
{
    u32 off = 0;
    u32 target;
    u32 start;

    if (s->tcp_state != TS_ESTABLISHED && s->tcp_state != TS_CLOSE_WAIT) {
        return -ENOTCONN;
    }
    if (s->shutdown_w) {
        return -EPIPE;
    }
    while (off < len) {
        u32 n = len - off > 1400 ? 1400 : len - off;
        int r = net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt,
                             s->rcv_nxt, TCP_ACK | TCP_PSH, buf + off, n);
        if (r < 0) {
            return off ? (int)off : r;
        }
        s->snd_nxt += n;
        off += n;
    }
    target = s->snd_nxt;
    start = tick;
    while (s->snd_una < target) {
        if (tick - start >= 3 * TIMER_HZ) {
            return -ETIMEDOUT;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        net_poll();
        sti();
        hlt();
        cli();
    }
    return (int)len;
}

int socket_sendto(struct socket *s, const u8 *buf, u32 len, u32 addr, u16 port)
{
    struct socket *dst = 0;
    struct dgram *d;
    int i;

    if (!s || s->type != SOCK_DGRAM) {
        return -EINVAL;
    }
    if (len > 1472) {
        len = 1472;
    }
    if (s->lport == 0) {
        s->lport = alloc_port();
        s->laddr = (addr == INADDR_LOOPBACK) ? INADDR_LOOPBACK : net_ip();
        s->state = SOCK_ST_BOUND;
    }

    if (addr != INADDR_LOOPBACK) {
        if (!net_ready()) {
            return -ENETDOWN;
        }
        return net_send_udp(addr, port, s->lport, buf, len);
    }

    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].family == AF_INET &&
                socks[i].type == SOCK_DGRAM &&
                socks[i].lport == port &&
                (socks[i].laddr == INADDR_ANY || socks[i].laddr == addr)) {
            dst = &socks[i];
            break;
        }
    }
    if (!dst) {
        return -ECONNREFUSED;
    }
    d = (struct dgram *)kmalloc(sizeof(struct dgram));
    if (!d) {
        return -ENOMEM;
    }
    d->next = 0;
    d->addr = (s->lport == 0) ? INADDR_LOOPBACK : s->laddr;
    d->port = s->lport;
    d->len = (u16)len;
    memcpy(d->data, (u8 *)buf, len);
    if (dst->dtail) {
        dst->dtail->next = d;
    } else {
        dst->dhead = d;
    }
    dst->dtail = d;
    return (int)len;
}

void socket_udp_deliver(u32 dst_ip, u16 dst_port, u32 src_ip, u16 src_port,
                        const u8 *data, u32 len)
{
    int i;
    u32 flags;
    struct socket *dst = 0;
    struct dgram *d;

    if (len > 1472) {
        len = 1472;
    }
    flags = local_irq_save();
    for (i = 0; i < SOCK_MAX; i++) {
        if (socks[i].used && socks[i].family == AF_INET &&
                socks[i].type == SOCK_DGRAM &&
                socks[i].lport == dst_port &&
                (socks[i].laddr == INADDR_ANY || socks[i].laddr == dst_ip ||
                 socks[i].laddr == INADDR_LOOPBACK)) {
            dst = &socks[i];
            break;
        }
    }
    if (!dst) {
        local_irq_restore(flags);
        return;
    }
    d = (struct dgram *)kmalloc(sizeof(struct dgram));
    if (!d) {
        local_irq_restore(flags);
        return;
    }
    d->next = 0;
    d->addr = src_ip;
    d->port = src_port;
    d->len = (u16)len;
    memcpy(d->data, (u8 *)data, len);
    if (dst->dtail) {
        dst->dtail->next = d;
    } else {
        dst->dhead = d;
    }
    dst->dtail = d;
    local_irq_restore(flags);
}

int socket_recvfrom(struct socket *s, u8 *buf, u32 len, u32 *addr, u16 *port,
                    int nonblock)
{
    struct dgram *d;
    u32 n;

    if (!s || s->type != SOCK_DGRAM) {
        return -EINVAL;
    }
    for (;;) {
        d = s->dhead;
        if (d) {
            break;
        }
        if (nonblock) {
            return -EAGAIN;
        }
        net_poll();
        sti();
        hlt();
        cli();
    }
    s->dhead = d->next;
    if (!s->dhead) {
        s->dtail = 0;
    }
    if (addr) {
        *addr = d->addr;
    }
    if (port) {
        *port = d->port;
    }
    n = d->len < len ? d->len : len;
    memcpy(buf, d->data, n);
    kfree(d);
    return (int)n;
}

u32 socket_local_addr(struct socket *s, u16 *port)
{
    if (port) {
        *port = s ? s->lport : 0;
    }
    return s ? s->laddr : 0;
}

u32 socket_peer_addr(struct socket *s, u16 *port)
{
    if (port) {
        *port = s ? s->rport : 0;
    }
    return s ? s->raddr : 0;
}

int socket_read(struct socket *s, u8 *buf, u32 len, int nonblock)
{
    u32 done = 0;

    if (!s) {
        return -EBADF;
    }
    if (s->type == SOCK_DGRAM) {
        return socket_recvfrom(s, buf, len, 0, 0, nonblock);
    }
    if (s->real_tcp) {
        for (;;) {
            while (done < len && s->rbuf.count > 0) {
                buf[done++] = s->rbuf.data[s->rbuf.tail];
                s->rbuf.tail = (s->rbuf.tail + 1) % SOCK_BUF;
                s->rbuf.count--;
            }
            if (done > 0) {
                return (int)done;
            }
            if (s->peer_closed || s->shutdown_r ||
                    s->tcp_state == TS_CLOSED) {
                return 0;
            }
            if (nonblock) {
                return -EAGAIN;
            }
            net_poll();
            sti();
            hlt();
            cli();
        }
    }
    if (s->state != SOCK_ST_CONNECTED) {
        return -ENOTCONN;
    }
    while (done < len) {
        while (s->rbuf.count == 0) {
            if (s->shutdown_r || !s->peer || s->peer->peer != s) {
                return (int)done;
            }
            if (nonblock) {
                return done ? (int)done : -EAGAIN;
            }
            sti();
            hlt();
            cli();
        }
        buf[done++] = s->rbuf.data[s->rbuf.tail];
        s->rbuf.tail = (s->rbuf.tail + 1) % SOCK_BUF;
        s->rbuf.count--;
    }
    return (int)done;
}

int socket_write(struct socket *s, const u8 *buf, u32 len, int nonblock)
{
    u32 done = 0;
    struct sockbuf *dst;

    if (!s || s->type == SOCK_DGRAM) {
        return -EINVAL;
    }
    if (s->real_tcp) {
        return tcp_write(s, buf, len, nonblock);
    }
    if (s->state != SOCK_ST_CONNECTED || !s->peer || s->shutdown_w) {
        return -EPIPE;
    }
    dst = &s->peer->rbuf;
    while (done < len) {
        while (dst->count == SOCK_BUF) {
            if (!s->peer || s->peer->peer != s) {
                return done ? (int)done : -EPIPE;
            }
            if (nonblock) {
                return done ? (int)done : -EAGAIN;
            }
            sti();
            hlt();
            cli();
        }
        dst->data[dst->head] = buf[done++];
        dst->head = (dst->head + 1) % SOCK_BUF;
        dst->count++;
    }
    return (int)done;
}

int socket_shutdown(struct socket *s, int how)
{
    if (!s) {
        return -EBADF;
    }
    if (how == SHUT_RD || how == SHUT_RDWR) {
        s->shutdown_r = 1;
    }
    if (how == SHUT_WR || how == SHUT_RDWR) {
        s->shutdown_w = 1;
        if (s->real_tcp && !s->peer_closed &&
                (s->tcp_state == TS_ESTABLISHED ||
                 s->tcp_state == TS_CLOSE_WAIT)) {
            net_send_tcp(s->raddr, s->rport, s->lport, s->snd_nxt,
                         s->rcv_nxt, TCP_ACK | TCP_FIN, 0, 0);
            s->snd_nxt++;
            s->tcp_state = TS_FIN_WAIT;
        }
    }
    return 0;
}

int socket_readable(struct socket *s)
{
    if (!s) {
        return 0;
    }
    if (s->type == SOCK_DGRAM) {
        return s->dhead != 0;
    }
    if (s->real_tcp) {
        return s->rbuf.count > 0 || s->peer_closed ||
               s->tcp_state == TS_CLOSED;
    }
    if (s->state == SOCK_ST_LISTEN) {
        return s->accept_head != 0;
    }
    return s->rbuf.count > 0 || s->shutdown_r || !s->peer ||
           s->peer->peer != s || s->peer->shutdown_w;
}

int socket_writable(struct socket *s)
{
    if (!s) {
        return 0;
    }
    if (s->type == SOCK_DGRAM) {
        return 1;
    }
    if (s->real_tcp) {
        return s->tcp_state == TS_ESTABLISHED && !s->shutdown_w;
    }
    if (s->state != SOCK_ST_CONNECTED || !s->peer || s->shutdown_w) {
        return 0;
    }
    return s->peer->rbuf.count < SOCK_BUF;
}

int socket_hup(struct socket *s)
{
    if (!s) {
        return 0;
    }
    if (s->real_tcp) {
        return s->peer_closed;
    }
    return s->state == SOCK_ST_CONNECTED &&
           (!s->peer || s->peer->peer != s);
}

int socket_state(struct socket *s)
{
    return s ? s->state : -1;
}

int socket_name(struct socket *s, char *out, u32 max)
{
    u32 i;
    if (!s) {
        return -EBADF;
    }
    for (i = 0; i + 1 < max && s->path[i]; i++) {
        out[i] = s->path[i];
    }
    out[i] = 0;
    return 0;
}

int socket_peer_name(struct socket *s, char *out, u32 max)
{
    if (!s || !s->peer) {
        return -ENOTCONN;
    }
    return socket_name(s->peer, out, max);
}
