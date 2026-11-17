#include <dirent.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>

struct __libc_dir {
    int fd;
    char buf[1024];
    int pos;
    int len;
    struct dirent ent;
};

DIR *opendir(const char *path)
{
    DIR *d;
    int fd = open(path, O_RDONLY);

    if (fd < 0) {
        return 0;
    }
    d = malloc(sizeof(DIR));
    if (!d) {
        close(fd);
        return 0;
    }
    d->fd = fd;
    d->pos = 0;
    d->len = 0;
    return d;
}

struct dirent *readdir(DIR *d)
{
    for (;;) {
        int i;
        if (d->pos >= d->len) {
            int n = getdents(d->fd, d->buf, sizeof(d->buf) - 1);
            if (n <= 0) {
                return 0;
            }
            d->buf[n] = 0;
            d->len = n;
            d->pos = 0;
        }
        i = 0;
        while (d->pos < d->len && d->buf[d->pos] != '\n' &&
                i < NAME_MAX) {
            d->ent.d_name[i++] = d->buf[d->pos++];
        }
        d->ent.d_name[i] = 0;
        if (d->pos < d->len && d->buf[d->pos] == '\n') {
            d->pos++;
        }
        if (i > 0) {
            d->ent.d_ino = 0;
            return &d->ent;
        }
    }
}

int closedir(DIR *d)
{
    if (!d) {
        return -1;
    }
    close(d->fd);
    free(d);
    return 0;
}
