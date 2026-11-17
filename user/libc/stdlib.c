#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

int atoi(const char *s)
{
    int v = 0, neg = 0;
    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s++ - '0');
    }
    return neg ? -v : v;
}

long atol(const char *s)
{
    return (long)atoi(s);
}

int abs(int v)
{
    return v < 0 ? -v : v;
}

static int env_index(const char *name)
{
    size_t len = strlen(name);
    int i;

    if (!environ) {
        return -1;
    }
    for (i = 0; environ[i]; i++) {
        if (!strncmp(environ[i], name, len) && environ[i][len] == '=') {
            return i;
        }
    }
    return -1;
}

static int env_append(char *entry)
{
    int i, n = 0;
    char **na;

    if (environ) {
        while (environ[n]) {
            n++;
        }
    }
    na = malloc(sizeof(char *) * (n + 2));
    if (!na) {
        return -1;
    }
    for (i = 0; i < n; i++) {
        na[i] = environ[i];
    }
    na[n] = entry;
    na[n + 1] = 0;
    environ = na;
    return 0;
}

char *getenv(const char *name)
{
    int i = env_index(name);
    return i < 0 ? 0 : environ[i] + strlen(name) + 1;
}

int setenv(const char *name, const char *value, int overwrite)
{
    int i;
    size_t nl, vl;
    char *entry;

    if (!name || !*name || strchr(name, '=')) {
        errno = EINVAL;
        return -1;
    }
    i = env_index(name);
    if (i >= 0 && !overwrite) {
        return 0;
    }
    nl = strlen(name);
    vl = strlen(value);
    entry = malloc(nl + vl + 2);
    if (!entry) {
        return -1;
    }
    memcpy(entry, name, nl);
    entry[nl] = '=';
    memcpy(entry + nl + 1, value, vl);
    entry[nl + vl + 1] = 0;
    if (i >= 0) {
        environ[i] = entry;
        return 0;
    }
    return env_append(entry);
}

int unsetenv(const char *name)
{
    int i = env_index(name);
    if (i < 0) {
        return 0;
    }
    while (environ[i]) {
        environ[i] = environ[i + 1];
        i++;
    }
    return 0;
}

int putenv(char *string)
{
    char *eq = strchr(string, '=');
    char name[128];
    size_t nl;
    int i;

    if (!eq) {
        return unsetenv(string);
    }
    nl = (size_t)(eq - string);
    if (nl >= sizeof(name)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(name, string, nl);
    name[nl] = 0;
    i = env_index(name);
    if (i >= 0) {
        environ[i] = string;
        return 0;
    }
    return env_append(string);
}

void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *))
{
    char *a = base;
    char *tmp;
    size_t i, j;

    if (n < 2 || size == 0) {
        return;
    }
    tmp = malloc(size);
    if (!tmp) {
        return;
    }
    for (i = 1; i < n; i++) {
        memcpy(tmp, a + i * size, size);
        j = i;
        while (j > 0 && cmp(a + (j - 1) * size, tmp) > 0) {
            memcpy(a + j * size, a + (j - 1) * size, size);
            j--;
        }
        memcpy(a + j * size, tmp, size);
    }
    free(tmp);
}

static int digit_val(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    return -1;
}

unsigned long strtoul(const char *s, char **end, int base)
{
    unsigned long v = 0;
    int neg = 0, any = 0, d;

    while (*s == ' ' || *s == '\t' || *s == '\n') {
        s++;
    }
    if (*s == '-') {
        neg = 1;
        s++;
    } else if (*s == '+') {
        s++;
    }
    if (base == 0) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        } else if (s[0] == '0') {
            base = 8;
        } else {
            base = 10;
        }
    } else if (base == 16 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s += 2;
    }
    while ((d = digit_val(*s)) >= 0 && d < base) {
        v = v * (unsigned long)base + (unsigned long)d;
        any = 1;
        s++;
    }
    if (end) {
        *end = (char *)(any ? s : s);
    }
    return neg ? (unsigned long)(-(long)v) : v;
}

long strtol(const char *s, char **end, int base)
{
    return (long)strtoul(s, end, base);
}

static unsigned int rand_state = 1;

void srand(unsigned int seed)
{
    rand_state = seed;
}

int rand(void)
{
    rand_state = rand_state * 1103515245u + 12345u;
    return (int)((rand_state >> 1) & 0x7fffffff);
}

int system(const char *cmd)
{
    (void)cmd;
    return -1;
}

void abort(void)
{
    _exit(134);
}

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *))
{
    const char *b = base;
    size_t lo = 0, hi = n;

    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int c = cmp(key, b + mid * size);
        if (c == 0) {
            return (void *)(b + mid * size);
        }
        if (c < 0) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return 0;
}
