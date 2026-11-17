#ifndef _LIBC_NETDB_H
#define _LIBC_NETDB_H
#include <netinet/in.h>

struct hostent {
    char *h_name;
    char **h_aliases;
    int h_addrtype;
    int h_length;
    char **h_addr_list;
};

struct hostent *gethostbyname(const char *name);

#endif
