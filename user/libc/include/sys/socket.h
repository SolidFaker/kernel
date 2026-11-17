#ifndef _LIBC_SYS_SOCKET_H
#define _LIBC_SYS_SOCKET_H
#include <posix_abi.h>
#include <unistd.h>

typedef unsigned int socklen_t;

int socket(int domain, int type, int protocol);
int bind(int fd, const struct sockaddr *addr, socklen_t len);
int listen(int fd, int backlog);
int accept(int fd, struct sockaddr *addr, socklen_t *len);
int connect(int fd, const struct sockaddr *addr, socklen_t len);
int socketpair(int domain, int type, int protocol, int sv[2]);
int shutdown(int fd, int how);
int getsockname(int fd, struct sockaddr *addr, socklen_t *len);
int getpeername(int fd, struct sockaddr *addr, socklen_t *len);
long send(int fd, const void *buf, size_t len, int flags);
long recv(int fd, void *buf, size_t len, int flags);
long sendto(int fd, const void *buf, size_t len, int flags,
            const struct sockaddr *addr, socklen_t alen);
long recvfrom(int fd, void *buf, size_t len, int flags,
              struct sockaddr *addr, socklen_t *alen);
int setsockopt(int fd, int level, int name, const void *val, socklen_t len);
int getsockopt(int fd, int level, int name, void *val, socklen_t *len);

#endif
