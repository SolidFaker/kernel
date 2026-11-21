#include "net.h"
#include "errno.h"
int net_init(void) { return 0; }
int net_ready(void) { return 0; }
int net_ping(u32 ip, u32 timeout_ms)
{ (void)ip; (void)timeout_ms; return -ENETDOWN; }
u32 net_ip(void) { return 0; }
void net_poll(void) { }
int net_send_udp(u32 dst, u16 dport, u16 sport, const u8 *data, u32 len)
{
    (void)dst; (void)dport; (void)sport; (void)data; (void)len;
    return -ENETDOWN;
}
int net_send_tcp(u32 dst, u16 dport, u16 sport, u32 seq, u32 ack,
                 u8 flags, const u8 *data, u32 len)
{
    (void)dst; (void)dport; (void)sport; (void)seq; (void)ack;
    (void)flags; (void)data; (void)len;
    return -ENETDOWN;
}
