// Minimal e1000 driver + ARP/IPv4/ICMP echo for QEMU user networking.
// Guest: 10.0.2.15/24, gateway 10.0.2.2.

#include "net.h"
#include "errno.h"
#include "page.h"
#include "mm.h"
#include "string.h"
#include "drivers/timer.h"
#include "socket.h"

#define NET_MMIO_VA  0xC8000000
#define NET_MMIO_LEN 0x20000

#define E1000_CTRL  0x0000
#define E1000_STATUS 0x0008
#define E1000_ICR   0x00C0
#define E1000_IMS   0x00D0
#define E1000_IMC   0x00D8
#define E1000_RCTL  0x0100
#define E1000_TCTL  0x0400
#define E1000_TIPG  0x0410
#define E1000_RDBAL 0x2800
#define E1000_RDBAH 0x2804
#define E1000_RDLEN 0x2808
#define E1000_RDH   0x2810
#define E1000_RDT   0x2818
#define E1000_TDBAL 0x3800
#define E1000_TDBAH 0x3804
#define E1000_TDLEN 0x3808
#define E1000_TDH   0x3810
#define E1000_TDT   0x3818
#define E1000_RAL   0x5400
#define E1000_RAH   0x5404

#define RX_N 32
#define TX_N 32
#define BUF_SIZE 2048

struct rx_desc {
    u32 addr_lo;
    u32 addr_hi;
    u16 len;
    u16 csum;
    u8 status;
    u8 errors;
    u16 special;
} __PACKED;

struct tx_desc {
    u32 addr_lo;
    u32 addr_hi;
    u16 len;
    u8 cso;
    u8 cmd;
    u8 status;
    u8 css;
    u16 special;
} __PACKED;

static volatile u32 *mmio;
static u8 mac[6];
static struct rx_desc *rx_ring;
static struct tx_desc *tx_ring;
static u8 *rx_buf[RX_N];
static u8 *tx_buf[TX_N];
static u32 rx_cur, tx_cur;
static u8 tx_used[TX_N];
static int up;

static u32 pci_read32(u8 bus, u8 slot, u8 func, u8 off)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) |
         ((u32)slot << 11) | ((u32)func << 8) | (off & 0xFC));
    return inl(0xCFC);
}

static void pci_write32(u8 bus, u8 slot, u8 func, u8 off, u32 v)
{
    outl(0xCF8, 0x80000000u | ((u32)bus << 16) |
         ((u32)slot << 11) | ((u32)func << 8) | (off & 0xFC));
    outl(0xCFC, v);
}

static inline u32 e1000_read(u32 reg)
{
    return mmio[reg / 4];
}

static inline void e1000_write(u32 reg, u32 v)
{
    mmio[reg / 4] = v;
}

static void delay(u32 loops)
{
    while (loops--) {
        asm volatile("nop");
    }
}

static u16 ip_checksum(const u8 *data, u32 len)
{
    u32 sum = 0;
    u32 i;
    for (i = 0; i + 1 < len; i += 2) {
        sum += ((u32)data[i] << 8) | data[i + 1];
    }
    if (len & 1) {
        sum += (u32)data[len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    return (u16)~sum;
}

static int e1000_send(const u8 *frame, u32 len)
{
    struct tx_desc *d;
    u32 idx = tx_cur;
    u32 spins = 200000;

    d = &tx_ring[idx];
    if (tx_used[idx]) {
        while (!(d->status & 0x01)) {
            if (spins-- == 0) {
                return -EIO;
            }
        }
    }
    memcpy(tx_buf[idx], frame, len);
    d->addr_lo = (u32)tx_buf[idx] - PAGE_OFFSET;
    d->addr_hi = 0;
    d->len = (u16)len;
    d->cso = 0;
    d->cmd = 0x0B;      /* EOP | IFCS | RS */
    d->status = 0;
    d->css = 0;
    d->special = 0;
    asm volatile("" ::: "memory");
    e1000_write(E1000_TDT, (idx + 1) % TX_N);
    tx_used[idx] = 1;
    tx_cur = (idx + 1) % TX_N;
    return 0;
}

static int e1000_recv(u8 *buf, u32 *len)
{
    struct rx_desc *d = &rx_ring[rx_cur];

    if (!(d->status & 0x01)) {
        return 0;
    }
    if (!(d->errors)) {
        *len = d->len;
        if (*len > BUF_SIZE) {
            *len = BUF_SIZE;
        }
        memcpy(buf, rx_buf[rx_cur], *len);
    } else {
        *len = 0;
    }
    d->status = 0;
    asm volatile("" ::: "memory");
    e1000_write(E1000_RDT, rx_cur);
    rx_cur = (rx_cur + 1) % RX_N;
    return *len ? 1 : 0;
}

static void build_eth(u8 *f, const u8 *dst, u16 type)
{
    memcpy(f, dst, 6);
    memcpy(f + 6, mac, 6);
    f[12] = (u8)(type >> 8);
    f[13] = (u8)type;
}

static int send_arp(u32 target)
{
    u8 f[42];
    u8 *a = f + 14;
    u32 src = net_ip();
    int i;

    build_eth(f, (const u8 *)"\xff\xff\xff\xff\xff\xff", 0x0806);
    a[0] = 0; a[1] = 1;             /* htype */
    a[2] = 0x08; a[3] = 0x00;       /* ptype */
    a[4] = 6; a[5] = 4;
    a[6] = 0; a[7] = 1;             /* request */
    for (i = 0; i < 6; i++) a[8 + i] = mac[i];
    a[14] = (u8)(src >> 24); a[15] = (u8)(src >> 16);
    a[16] = (u8)(src >> 8);  a[17] = (u8)src;
    for (i = 0; i < 6; i++) a[18 + i] = 0;
    a[24] = (u8)(target >> 24); a[25] = (u8)(target >> 16);
    a[26] = (u8)(target >> 8);  a[27] = (u8)target;
    return e1000_send(f, 42);
}

static int arp_lookup(u32 ip, u8 *out)
{
    u32 spins = 2000000;

    if (send_arp(ip) < 0) {
        return -EIO;
    }
    static u8 f[1600];
    while (spins--) {
        u32 len = 0;
        if (e1000_recv(f, &len)) {
            if (len >= 42 && f[12] == 0x08 && f[13] == 0x06 &&
                    f[21] == 2) {
                u32 sip = ((u32)f[28] << 24) | ((u32)f[29] << 16) |
                          ((u32)f[30] << 8) | f[31];
                if (sip == ip) {
                    memcpy(out, f + 22, 6);
                    return 0;
                }
            }
        }
    }
    return -ETIMEDOUT;
}

static u32 arp_ip;
static u8 arp_mac[6];
static int arp_valid;

static int get_mac(u32 ip, u8 *out)
{
    if (arp_valid && arp_ip == ip) {
        memcpy(out, arp_mac, 6);
        return 0;
    }
    if (arp_lookup(ip, out) == 0) {
        arp_ip = ip;
        memcpy(arp_mac, out, 6);
        arp_valid = 1;
        return 0;
    }
    return -EHOSTUNREACH;
}

int net_ready(void)
{
    return up;
}

u32 net_ip(void)
{
    return 0x0A00020F;  /* 10.0.2.15 */
}

static int send_icmp_echo(u32 dst, u16 seq)
{
    u8 f[14 + 20 + 8 + 32];
    u8 *ip = f + 14;
    u8 *icmp = ip + 20;
    u8 dmac[6];
    u32 n = 20 + 8 + 32;
    u32 src = net_ip();
    u16 csum;
    int i;

    if (get_mac(dst, dmac) < 0) {
        return -EHOSTUNREACH;
    }
    build_eth(f, dmac, 0x0800);
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (u8)(n >> 8); ip[3] = (u8)n;
    ip[8] = 64;
    ip[9] = 1;                      /* ICMP */
    ip[12] = (u8)(src >> 24); ip[13] = (u8)(src >> 16);
    ip[14] = (u8)(src >> 8);  ip[15] = (u8)src;
    ip[16] = (u8)(dst >> 24); ip[17] = (u8)(dst >> 16);
    ip[18] = (u8)(dst >> 8);  ip[19] = (u8)dst;
    csum = ip_checksum(ip, 20);
    ip[10] = (u8)(csum >> 8); ip[11] = (u8)csum;

    memset(icmp, 0, 8 + 32);
    icmp[0] = 8;                    /* echo request */
    icmp[4] = 0x12; icmp[5] = 0x34;
    icmp[6] = (u8)(seq >> 8); icmp[7] = (u8)seq;
    for (i = 0; i < 32; i++) {
        icmp[8 + i] = (u8)('a' + (i % 26));
    }
    csum = ip_checksum(icmp, 8 + 32);
    icmp[2] = (u8)(csum >> 8); icmp[3] = (u8)csum;
    return e1000_send(f, 14 + n);
}

int net_send_udp(u32 dst, u16 dport, u16 sport, const u8 *data, u32 len)
{
    static u8 f[14 + 20 + 8 + 1472];
    u8 *ip = f + 14;
    u8 *udp = ip + 20;
    u8 dmac[6];
    u32 next = ((dst & 0xFFFFFF00u) == 0x0A000200u) ? dst : 0x0A000202u;
    u32 n = 20 + 8 + len;
    u32 src = net_ip();
    u16 csum;

    if (!up) {
        return -ENETDOWN;
    }
    if (len > 1472) {
        len = 1472;
        n = 20 + 8 + len;
    }
    if (get_mac(next, dmac) < 0) {
        return -EHOSTUNREACH;
    }
    build_eth(f, dmac, 0x0800);
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (u8)(n >> 8); ip[3] = (u8)n;
    ip[8] = 64;
    ip[9] = 17;             /* UDP */
    ip[12] = (u8)(src >> 24); ip[13] = (u8)(src >> 16);
    ip[14] = (u8)(src >> 8);  ip[15] = (u8)src;
    ip[16] = (u8)(dst >> 24); ip[17] = (u8)(dst >> 16);
    ip[18] = (u8)(dst >> 8);  ip[19] = (u8)dst;
    csum = ip_checksum(ip, 20);
    ip[10] = (u8)(csum >> 8); ip[11] = (u8)csum;

    memset(udp, 0, 8);
    udp[0] = (u8)(sport >> 8); udp[1] = (u8)sport;
    udp[2] = (u8)(dport >> 8); udp[3] = (u8)dport;
    udp[4] = (u8)((8 + len) >> 8); udp[5] = (u8)(8 + len);
    memcpy(udp + 8, (u8 *)data, len);
    {
        int r = e1000_send(f, 14 + n);
        return r < 0 ? r : (int)len;
    }
}

static u32 tcp_sum(u32 src, u32 dst, const u8 *seg, u32 len)
{
    u32 sum = 0;
    u32 i;
    u16 v;

    sum += (src >> 16) & 0xffff; sum += src & 0xffff;
    sum += (dst >> 16) & 0xffff; sum += dst & 0xffff;
    sum += 6;                      /* protocol */
    sum += len;
    for (i = 0; i + 1 < len; i += 2) {
        sum += ((u32)seg[i] << 8) | seg[i + 1];
    }
    if (len & 1) {
        sum += (u32)seg[len - 1] << 8;
    }
    while (sum >> 16) {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    v = (u16)~sum;
    return v;
}

int net_send_tcp(u32 dst, u16 dport, u16 sport, u32 seq, u32 ack,
                 u8 flags, const u8 *data, u32 len)
{
    static u8 f[14 + 20 + 20 + 1400];
    u8 *ip = f + 14;
    u8 *tcp = ip + 20;
    u8 dmac[6];
    u32 next = ((dst & 0xFFFFFF00u) == 0x0A000200u) ? dst : 0x0A000202u;
    u32 n = 20 + 20 + len;
    u32 src = net_ip();
    u16 csum;

    if (!up) {
        return -ENETDOWN;
    }
    if (len > 1400) {
        len = 1400;
        n = 20 + 20 + len;
    }
    if (get_mac(next, dmac) < 0) {
        return -EHOSTUNREACH;
    }
    build_eth(f, dmac, 0x0800);
    memset(ip, 0, 20);
    ip[0] = 0x45;
    ip[2] = (u8)(n >> 8); ip[3] = (u8)n;
    ip[8] = 64;
    ip[9] = 6;              /* TCP */
    ip[12] = (u8)(src >> 24); ip[13] = (u8)(src >> 16);
    ip[14] = (u8)(src >> 8);  ip[15] = (u8)src;
    ip[16] = (u8)(dst >> 24); ip[17] = (u8)(dst >> 16);
    ip[18] = (u8)(dst >> 8);  ip[19] = (u8)dst;
    csum = ip_checksum(ip, 20);
    ip[10] = (u8)(csum >> 8); ip[11] = (u8)csum;

    memset(tcp, 0, 20);
    tcp[0] = (u8)(sport >> 8); tcp[1] = (u8)sport;
    tcp[2] = (u8)(dport >> 8); tcp[3] = (u8)dport;
    tcp[4] = (u8)(seq >> 24); tcp[5] = (u8)(seq >> 16);
    tcp[6] = (u8)(seq >> 8);  tcp[7] = (u8)seq;
    tcp[8] = (u8)(ack >> 24); tcp[9] = (u8)(ack >> 16);
    tcp[10] = (u8)(ack >> 8); tcp[11] = (u8)ack;
    tcp[12] = 5 << 4;
    tcp[13] = flags;
    tcp[14] = 0x20; tcp[15] = 0x00;   /* window */
    if (len) {
        memcpy(tcp + 20, (u8 *)data, len);
    }
    {
        u16 tc = tcp_sum(src, dst, tcp, 20 + len);
        tcp[16] = (u8)(tc >> 8);
        tcp[17] = (u8)tc;
    }
    {
        int r = e1000_send(f, 14 + n);
        return r < 0 ? r : (int)len;
    }
}

void net_poll(void)
{
    static u8 f[1600];
    u32 len;
    int n = 16;

    if (!up) {
        return;
    }
    while (n-- > 0 && e1000_recv(f, &len)) {
        if (len >= 42 && f[12] == 0x08 && f[13] == 0x06 && f[21] == 2) {
            u32 sip = ((u32)f[28] << 24) | ((u32)f[29] << 16) |
                      ((u32)f[30] << 8) | f[31];
            arp_ip = sip;
            memcpy(arp_mac, f + 22, 6);
            arp_valid = 1;
        } else if (len >= 14 + 28 && f[12] == 0x08 && f[13] == 0x00) {
            u8 *p = f + 14;
            u32 ihl = (p[0] & 0x0f) * 4;
            u32 tot = ((u32)p[2] << 8) | p[3];
            if (ihl >= 20 && tot >= ihl + 8 && len >= 14 + tot &&
                    p[9] == 17) {
                u8 *udp = p + ihl;
                u32 sip = ((u32)p[12] << 24) | ((u32)p[13] << 16) |
                          ((u32)p[14] << 8) | p[15];
                u32 dip = ((u32)p[16] << 24) | ((u32)p[17] << 16) |
                          ((u32)p[18] << 8) | p[19];
                u16 sport = (u16)((udp[0] << 8) | udp[1]);
                u16 dport = (u16)((udp[2] << 8) | udp[3]);
                u32 ulen = ((u32)udp[4] << 8) | udp[5];
                if (ulen >= 8 && ulen <= tot - ihl) {
                    socket_udp_deliver(dip, dport, sip, sport,
                                       udp + 8, ulen - 8);
                }
            } else if (ihl >= 20 && tot >= ihl + 20 && len >= 14 + tot &&
                    p[9] == 6) {
                u8 *tcp = p + ihl;
                u32 sip = ((u32)p[12] << 24) | ((u32)p[13] << 16) |
                          ((u32)p[14] << 8) | p[15];
                u32 dip = ((u32)p[16] << 24) | ((u32)p[17] << 16) |
                          ((u32)p[18] << 8) | p[19];
                u16 sport = (u16)((tcp[0] << 8) | tcp[1]);
                u16 dport = (u16)((tcp[2] << 8) | tcp[3]);
                u32 seq = ((u32)tcp[4] << 24) | ((u32)tcp[5] << 16) |
                          ((u32)tcp[6] << 8) | tcp[7];
                u32 ack = ((u32)tcp[8] << 24) | ((u32)tcp[9] << 16) |
                          ((u32)tcp[10] << 8) | tcp[11];
                u32 hl = (tcp[12] >> 4) * 4;
                u8 flags = tcp[13];
                if (hl >= 20 && ihl + hl <= tot) {
                    socket_tcp_input(dip, dport, sip, sport, seq, ack,
                                     flags, tcp + hl, tot - ihl - hl);
                }
            }
        }
    }
}

int net_ping(u32 ip, u32 timeout_ms)
{
    u32 start, limit, seq = 1;

    if (!up) {
        return -ENETDOWN;
    }
    if (send_icmp_echo(ip, (u16)seq) < 0) {
        return -EHOSTUNREACH;
    }
    start = tick;
    limit = timeout_ms * (TIMER_HZ / 1000);
    if (limit == 0) {
        limit = 1;
    }
    static u8 f[1600];
    while (tick - start < limit) {
        u32 len = 0;
        sti();
        e1000_recv(f, &len);
        cli();
        if (len >= 42 + 20 + 8 && f[12] == 0x08 && f[13] == 0x00) {
            u8 *p = f + 14;
            u32 proto = p[9];
            u32 ihl = (p[0] & 0x0f) * 4;
            u8 *ic = p + ihl;
            if (proto == 1 && ic[0] == 0 && ic[4] == 0x12 &&
                    ic[5] == 0x34) {
                return (int)((tick - start) * 1000 / TIMER_HZ);
            }
        }
        sti();
        hlt();
        cli();
    }
    return -ETIMEDOUT;
}

int net_init(void)
{
    int bus, slot, func, i;
    u32 bar;
    u32 pa;

    for (bus = 0; bus < 2; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (func = 0; func < 8; func++) {
                u32 id = pci_read32(bus, slot, func, 0);
                if ((id & 0xFFFF) == 0x8086 &&
                        ((id >> 16) == 0x100E || (id >> 16) == 0x100F)) {
                    goto found;
                }
            }
        }
    }
    return -ENODEV;

found:
    pci_write32(bus, slot, func, 4,
                pci_read32(bus, slot, func, 4) | 0x6);
    bar = pci_read32(bus, slot, func, 0x10) & ~0xFu;
    if (!bar) {
        return -ENODEV;
    }
    for (i = 0; i < NET_MMIO_LEN / PAGE_SIZE; i++) {
        map(pdt_kernel, NET_MMIO_VA + i * PAGE_SIZE,
            bar + i * PAGE_SIZE, PG_WRITE | PG_PRESENT);
    }
    mmio = (volatile u32 *)NET_MMIO_VA;

    e1000_write(E1000_IMC, 0xFFFFFFFF);
    e1000_write(E1000_IMS, 0);
    e1000_write(E1000_CTRL, e1000_read(E1000_CTRL) | 0x04000000);
    delay(1000000);
    e1000_write(E1000_CTRL, (e1000_read(E1000_CTRL) & ~0x04000000) | 0x40);
    delay(1000000);

    {
        u32 ral = e1000_read(E1000_RAL);
        u32 rah = e1000_read(E1000_RAH);
        mac[0] = (u8)ral;
        mac[1] = (u8)(ral >> 8);
        mac[2] = (u8)(ral >> 16);
        mac[3] = (u8)(ral >> 24);
        mac[4] = (u8)rah;
        mac[5] = (u8)(rah >> 8);
    }
    if ((mac[0] | mac[1] | mac[2] | mac[3] | mac[4] | mac[5]) == 0) {
        static const u8 fallback[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
        memcpy(mac, (const u8 *)fallback, 6);
    }

    pa = page_alloc();
    rx_ring = (struct rx_desc *)(pa + PAGE_OFFSET);
    bzero((u8 *)rx_ring, PAGE_SIZE);
    for (i = 0; i < RX_N; i++) {
        u32 bpa = page_alloc();
        rx_buf[i] = (u8 *)(bpa + PAGE_OFFSET);
        rx_ring[i].addr_lo = bpa;
        rx_ring[i].addr_hi = 0;
        rx_ring[i].status = 0;
    }
    e1000_write(E1000_RDBAL, pa);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, RX_N * sizeof(struct rx_desc));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, RX_N - 1);
    e1000_write(E1000_RCTL, 0x00000002 | 0x00000008 | 0x00000010 | 0x00008000 | 0x04000000);

    pa = page_alloc();
    tx_ring = (struct tx_desc *)(pa + PAGE_OFFSET);
    bzero((u8 *)tx_ring, PAGE_SIZE);
    for (i = 0; i < TX_N; i++) {
        u32 bpa = page_alloc();
        tx_buf[i] = (u8 *)(bpa + PAGE_OFFSET);
        tx_ring[i].addr_lo = bpa;
        tx_ring[i].addr_hi = 0;
        tx_ring[i].status = 0;
        tx_used[i] = 0;
    }
    e1000_write(E1000_TDBAL, pa);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, TX_N * sizeof(struct tx_desc));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000_write(E1000_TIPG, 10 | (8 << 10) | (6 << 20));
    e1000_write(E1000_TCTL,
                0x00000002 | 0x00000008 | (0x10 << 4) | (0x40 << 12));

    up = 1;
    return 0;
}
