#ifndef NET_H
#define NET_H

#include "common.h"

int net_init(void);
int net_ready(void);
int net_ping(u32 ip, u32 timeout_ms);
u32 net_ip(void);
void net_poll(void);
int net_send_udp(u32 dst, u16 dport, u16 sport, const u8 *data, u32 len);
int net_send_tcp(u32 dst, u16 dport, u16 sport, u32 seq, u32 ack,
                 u8 flags, const u8 *data, u32 len);

#endif
