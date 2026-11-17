#ifndef _LIBC_NETINET_IN_H
#define _LIBC_NETINET_IN_H
#include <posix_abi.h>

unsigned short htons(unsigned short v);
unsigned short ntohs(unsigned short v);
unsigned int htonl(unsigned int v);
unsigned int ntohl(unsigned int v);

#endif
