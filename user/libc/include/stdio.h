#ifndef _LIBC_STDIO_H
#define _LIBC_STDIO_H
#include <stddef.h>
#include <stdarg.h>

#define EOF (-1)
#define BUFSIZ 1024
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

typedef struct __libc_file FILE;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t size, const char *fmt, ...);
int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int puts(const char *s);
int fputs(const char *s, FILE *f);
int putchar(int c);
int fputc(int c, FILE *f);
int fgetc(FILE *f);
char *fgets(char *buf, int size, FILE *f);
size_t fread(void *buf, size_t size, size_t n, FILE *f);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *f);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *f);
int fflush(FILE *f);
int feof(FILE *f);
int ferror(FILE *f);
int fileno(FILE *f);
int fseek(FILE *f, long off, int whence);
long ftell(FILE *f);
void perror(const char *s);

#endif
