#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/resource.h>

long sysconf(int name)
{
    switch (name) {
    case 30: return 4096;       /* _SC_PAGESIZE */
    case 2:  return 2000;       /* _SC_CLK_TCK */
    case 4:  return 16;         /* _SC_OPEN_MAX */
    default: errno = EINVAL; return -1;
    }
}

int getpagesize(void)
{
    return 4096;
}

int getrlimit(int res, struct rlimit *rl)
{
    if (!rl) {
        errno = EFAULT;
        return -1;
    }
    rl->rlim_cur = (res == RLIMIT_NOFILE) ? 16 : 64u * 1024u * 1024u;
    rl->rlim_max = rl->rlim_cur;
    return 0;
}

int setrlimit(int res, const struct rlimit *rl)
{
    (void)res;
    (void)rl;
    return 0;
}

void __assert_fail(const char *expr, const char *file, int line)
{
    fprintf(stderr, "assertion failed: %s (%s:%d)\n", expr, file, line);
    abort();
}
