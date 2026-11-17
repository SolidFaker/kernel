#ifndef SOCKET_H
#define SOCKET_H

#include "common.h"

#define SOCK_STREAM_K 1
#define SOCK_DGRAM_K  2

#define SOCK_ST_UNBOUND   0
#define SOCK_ST_BOUND     1
#define SOCK_ST_LISTEN    2
#define SOCK_ST_CONNECTED 3

struct socket;

struct socket *socket_new(void);
void socket_set_type(struct socket *s, int family, int type);
int socket_family(struct socket *s);
int socket_type(struct socket *s);
int socket_bind_inet(struct socket *s, u32 addr, u16 port);
int socket_connect_inet(struct socket *s, u32 addr, u16 port);
int socket_connect_real(struct socket *s, u32 addr, u16 port);
void socket_tcp_input(u32 dst_ip, u16 dst_port, u32 src_ip, u16 src_port,
                      u32 seq, u32 ack, u8 flags, const u8 *data, u32 len);
int socket_sendto(struct socket *s, const u8 *buf, u32 len,
                  u32 addr, u16 port);
int socket_recvfrom(struct socket *s, u8 *buf, u32 len,
                    u32 *addr, u16 *port, int nonblock);
u32 socket_local_addr(struct socket *s, u16 *port);
u32 socket_peer_addr(struct socket *s, u16 *port);
void socket_put(struct socket *s);
int socket_pair(struct socket *a, struct socket *b);
int socket_bind(struct socket *s, const char *path);
int socket_listen(struct socket *s, int backlog);
int socket_connect(struct socket *s, const char *path);
int socket_accept(struct socket *s, int nonblock, struct socket **out);
int socket_read(struct socket *s, u8 *buf, u32 len, int nonblock);
int socket_write(struct socket *s, const u8 *buf, u32 len, int nonblock);
int socket_shutdown(struct socket *s, int how);
int socket_readable(struct socket *s);
int socket_writable(struct socket *s);
int socket_hup(struct socket *s);
int socket_state(struct socket *s);
int socket_name(struct socket *s, char *out, u32 max);
int socket_peer_name(struct socket *s, char *out, u32 max);
void socket_udp_deliver(u32 dst_ip, u16 dst_port, u32 src_ip, u16 src_port,
                        const u8 *data, u32 len);

#endif
