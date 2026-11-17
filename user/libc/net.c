#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>

unsigned short htons(unsigned short v)
{
    return (unsigned short)((v << 8) | (v >> 8));
}

unsigned short ntohs(unsigned short v)
{
    return htons(v);
}

unsigned int htonl(unsigned int v)
{
    return ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) |
           ((v >> 8) & 0xff00u) | ((v >> 24) & 0xffu);
}

unsigned int ntohl(unsigned int v)
{
    return htonl(v);
}

unsigned int inet_addr(const char *s)
{
    unsigned int parts[4];
    int i, n = 0;
    unsigned int v = 0;

    for (i = 0; i < 4; i++) {
        v = 0;
        n = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + (unsigned int)(*s++ - '0');
            n++;
        }
        if (n == 0 || v > 255) {
            return 0xffffffffu;
        }
        parts[i] = v;
        if (i < 3) {
            if (*s != '.') {
                return 0xffffffffu;
            }
            s++;
        }
    }
    if (*s) {
        return 0xffffffffu;
    }
    return htonl((parts[0] << 24) | (parts[1] << 16) |
                 (parts[2] << 8) | parts[3]);
}

char *inet_ntoa(struct in_addr in)
{
    static char buf[16];
    unsigned int v = ntohl(in.s_addr);
    char *p = buf;
    unsigned int parts[4];
    int i;

    parts[0] = (v >> 24) & 0xff;
    parts[1] = (v >> 16) & 0xff;
    parts[2] = (v >> 8) & 0xff;
    parts[3] = v & 0xff;
    for (i = 0; i < 4; i++) {
        unsigned int x = parts[i];
        char tmp[4];
        int n = 0;
        do {
            tmp[n++] = (char)('0' + x % 10);
            x /= 10;
        } while (x);
        while (n > 0) {
            *p++ = tmp[--n];
        }
        if (i < 3) {
            *p++ = '.';
        }
    }
    *p = 0;
    return buf;
}

struct hostent *gethostbyname(const char *name)
{
    static char *aliases[1] = { 0 };
    static unsigned int addr;
    static char *addrs[2];
    static struct hostent he;

    if (strcmp(name, "localhost") != 0 && strcmp(name, "127.0.0.1") != 0) {
        return 0;
    }
    addr = htonl(INADDR_LOOPBACK);
    addrs[0] = (char *)&addr;
    addrs[1] = 0;
    he.h_name = "localhost";
    he.h_aliases = aliases;
    he.h_addrtype = AF_INET;
    he.h_length = 4;
    he.h_addr_list = addrs;
    return &he;
}
