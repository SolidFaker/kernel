#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct __libc_file {
    int fd;
    int eof;
    int error;
};

static FILE f_stdin = { 0, 0, 0 };
static FILE f_stdout = { 1, 0, 0 };
static FILE f_stderr = { 2, 0, 0 };

FILE *stdin = &f_stdin;
FILE *stdout = &f_stdout;
FILE *stderr = &f_stderr;

struct sink {
    int fd;
    char *buf;
    size_t cap;
    size_t len;
};

static void emit(struct sink *s, char c)
{
    if (s->buf) {
        if (s->len < s->cap) {
            s->buf[s->len] = c;
        }
    } else {
        write(s->fd, &c, 1);
    }
    s->len++;
}

static void emit_repeat(struct sink *s, char c, int n)
{
    while (n-- > 0) {
        emit(s, c);
    }
}

static void fmt_str(struct sink *s, const char *str, int width, int left,
                    int prec)
{
    int n, pad;
    if (!str) {
        str = "(null)";
    }
    n = (int)strlen(str);
    if (prec >= 0 && n > prec) {
        n = prec;
    }
    pad = width > n ? width - n : 0;
    if (!left) {
        emit_repeat(s, ' ', pad);
    }
    while (n-- > 0) {
        emit(s, *str++);
    }
    if (left) {
        emit_repeat(s, ' ', pad);
    }
}

static void fmt_num(struct sink *s, unsigned long v, int base, int upper,
                    int neg, int width, int zero, int left, int plus,
                    int space, int alt, int prec)
{
    char tmp[72];
    char prefix[3];
    const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, plen = 0, signlen, pad, i;

    if (v == 0 && prec != 0) {
        tmp[n++] = '0';
    } else {
        while (v) {
            tmp[n++] = dig[v % (unsigned)base];
            v /= (unsigned)base;
        }
    }
    while (n < prec) {
        tmp[n++] = '0';
    }
    if (prec > 64) {
        prec = 64;
    }
    if (alt && base == 16) {
        prefix[0] = '0';
        prefix[1] = upper ? 'X' : 'x';
        plen = 2;
    } else if (alt && base == 8) {
        prefix[0] = '0';
        plen = 1;
    }
    signlen = neg || plus || space;
    pad = width - (n + plen + signlen);
    if (pad < 0) {
        pad = 0;
    }
    if (!left && !(zero && prec < 0)) {
        emit_repeat(s, ' ', pad);
    }
    if (neg) {
        emit(s, '-');
    } else if (plus) {
        emit(s, '+');
    } else if (space) {
        emit(s, ' ');
    }
    for (i = 0; i < plen; i++) {
        emit(s, prefix[i]);
    }
    if (!left && zero && prec < 0) {
        emit_repeat(s, '0', pad);
    }
    while (n > 0) {
        emit(s, tmp[--n]);
    }
    if (left) {
        emit_repeat(s, ' ', pad);
    }
}

static int format(struct sink *s, const char *fmt, va_list ap)
{
    while (*fmt) {
        int left = 0, zero = 0, plus = 0, space = 0, alt = 0;
        int width = 0, prec = -1, lcount = 0, conv;

        if (*fmt != '%') {
            emit(s, *fmt++);
            continue;
        }
        fmt++;

        for (;;) {
            if (*fmt == '-') {
                left = 1;
            } else if (*fmt == '0') {
                zero = 1;
            } else if (*fmt == '+') {
                plus = 1;
            } else if (*fmt == ' ') {
                space = 1;
            } else if (*fmt == '#') {
                alt = 1;
            } else {
                break;
            }
            fmt++;
        }
        if (*fmt == '*') {
            width = va_arg(ap, int);
            if (width < 0) {
                left = 1;
                width = -width;
            }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt++ - '0');
            }
        }
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            if (*fmt == '*') {
                prec = va_arg(ap, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    prec = prec * 10 + (*fmt++ - '0');
                }
            }
        }
        while (*fmt == 'l') {
            lcount++;
            fmt++;
        }
        while (*fmt == 'h') {
            fmt++;
        }
        conv = *fmt ? *fmt++ : 0;

        switch (conv) {
        case 'd':
        case 'i': {
            long sv;
            if (lcount >= 1) {
                sv = va_arg(ap, long);
            } else {
                sv = va_arg(ap, int);
            }
            if (sv < 0) {
                fmt_num(s, (unsigned long)(-sv), 10, 0, 1,
                        width, zero, left, plus, space, alt, prec);
            } else {
                fmt_num(s, (unsigned long)sv, 10, 0, 0,
                        width, zero, left, plus, space, alt, prec);
            }
            break;
        }
        case 'u':
        case 'o':
        case 'x':
        case 'X': {
            unsigned long uv;
            int base = conv == 'o' ? 8 : (conv == 'u' ? 10 : 16);
            if (lcount >= 1) {
                uv = va_arg(ap, unsigned long);
            } else {
                uv = va_arg(ap, unsigned int);
            }
            fmt_num(s, uv, base, conv == 'X', 0, width, zero, left,
                    0, 0, alt, prec);
            break;
        }
        case 'p':
            fmt_num(s, (unsigned long)(unsigned int)va_arg(ap, void *),
                    16, 0, 0, width, zero, left, 0, 0, 1, prec);
            break;
        case 'c': {
            char c = (char)va_arg(ap, int);
            if (!left) {
                emit_repeat(s, ' ', width > 1 ? width - 1 : 0);
            }
            emit(s, c);
            if (left) {
                emit_repeat(s, ' ', width > 1 ? width - 1 : 0);
            }
            break;
        }
        case 's':
            fmt_str(s, va_arg(ap, const char *), width, left, prec);
            break;
        case '%':
            emit(s, '%');
            break;
        default:
            if (conv) {
                emit(s, '%');
                emit(s, (char)conv);
            }
            break;
        }
    }
    return (int)s->len;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
    struct sink s;
    int n;

    s.fd = -1;
    s.buf = buf;
    s.cap = size;
    s.len = 0;
    n = format(&s, fmt, ap);
    if (size > 0) {
        size_t end = s.len < size ? s.len : size - 1;
        buf[end] = 0;
    }
    return n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

int sprintf(char *buf, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vsnprintf(buf, (size_t)-1, fmt, ap);
    va_end(ap);
    return n;
}

static int vfprintf_fd(int fd, const char *fmt, va_list ap)
{
    struct sink s;
    s.fd = fd;
    s.buf = 0;
    s.cap = 0;
    s.len = 0;
    return format(&s, fmt, ap);
}

int printf(const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf_fd(1, fmt, ap);
    va_end(ap);
    return n;
}

int fprintf(FILE *f, const char *fmt, ...)
{
    va_list ap;
    int n;
    va_start(ap, fmt);
    n = vfprintf_fd(f->fd, fmt, ap);
    va_end(ap);
    return n;
}

int putchar(int c)
{
    char ch = (char)c;
    return write(1, &ch, 1) == 1 ? (unsigned char)ch : EOF;
}

int fputc(int c, FILE *f)
{
    char ch = (char)c;
    return write(f->fd, &ch, 1) == 1 ? (unsigned char)ch : EOF;
}

int fputs(const char *s, FILE *f)
{
    int n = (int)strlen(s);
    return write(f->fd, s, (size_t)n) == n ? 0 : EOF;
}

int puts(const char *s)
{
    if (fputs(s, stdout) == EOF) {
        return EOF;
    }
    return putchar('\n') == EOF ? EOF : 0;
}

FILE *fopen(const char *path, const char *mode)
{
    int flags = O_RDONLY;
    int fd;
    FILE *f;

    if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    if (strchr(mode, '+')) {
        flags = (flags & ~3) | O_RDWR;
    }
    fd = open(path, flags, 0644);
    if (fd < 0) {
        return 0;
    }
    f = malloc(sizeof(FILE));
    if (!f) {
        close(fd);
        return 0;
    }
    f->fd = fd;
    f->eof = 0;
    f->error = 0;
    return f;
}

int fclose(FILE *f)
{
    if (!f) {
        return EOF;
    }
    if (f == stdin || f == stdout || f == stderr) {
        return 0;
    }
    close(f->fd);
    free(f);
    return 0;
}

int fflush(FILE *f)
{
    (void)f;
    return 0;
}

int feof(FILE *f)
{
    return f->eof;
}

int ferror(FILE *f)
{
    return f->error;
}

int fileno(FILE *f)
{
    return f->fd;
}

int fgetc(FILE *f)
{
    unsigned char c;
    int n = read(f->fd, &c, 1);
    if (n <= 0) {
        f->eof = 1;
        return EOF;
    }
    return c;
}

char *fgets(char *buf, int size, FILE *f)
{
    int i = 0;
    if (size <= 0) {
        return 0;
    }
    while (i < size - 1) {
        int c = fgetc(f);
        if (c == EOF) {
            break;
        }
        buf[i++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    buf[i] = 0;
    return i > 0 ? buf : 0;
}

size_t fread(void *buf, size_t size, size_t n, FILE *f)
{
    size_t total;
    if (size == 0 || n == 0) {
        return 0;
    }
    total = size * n;
    int r = read(f->fd, buf, total);
    if (r < 0) {
        f->error = 1;
        return 0;
    }
    if ((size_t)r < total) {
        f->eof = 1;
    }
    return (size_t)r / size;
}

size_t fwrite(const void *buf, size_t size, size_t n, FILE *f)
{
    size_t total;
    if (size == 0 || n == 0) {
        return 0;
    }
    total = size * n;
    int r = write(f->fd, buf, total);
    if (r < 0) {
        f->error = 1;
        return 0;
    }
    return (size_t)r / size;
}

int fseek(FILE *f, long off, int whence)
{
    return lseek(f->fd, (int)off, whence) < 0 ? -1 : 0;
}

long ftell(FILE *f)
{
    return (long)lseek(f->fd, 0, SEEK_CUR);
}

void perror(const char *s)
{
    int e = errno;
    if (s && *s) {
        write(2, s, strlen(s));
        write(2, ": ", 2);
    }
    write(2, strerror(e), strlen(strerror(e)));
    write(2, "\n", 1);
}
