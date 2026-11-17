#ifndef _LIBC_ARPA_INET_H
#define _LIBC_ARPA_INET_H
#include <netinet/in.h>

unsigned int inet_addr(const char *s);
char *inet_ntoa(struct in_addr in);

#endif
