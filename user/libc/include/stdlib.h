#ifndef _LIBC_STDLIB_H
#define _LIBC_STDLIB_H
#include <stddef.h>

void *malloc(size_t n);
void *calloc(size_t n, size_t size);
void *realloc(void *p, size_t n);
void free(void *p);
void exit(int status) __attribute__((noreturn));
void _Exit(int status) __attribute__((noreturn));
void abort(void) __attribute__((noreturn));
int atoi(const char *s);
long atol(const char *s);
int abs(int v);
extern char **environ;

char *getenv(const char *name);
int setenv(const char *name, const char *value, int overwrite);
int unsetenv(const char *name);
int putenv(char *string);
int system(const char *cmd);

void qsort(void *base, size_t n, size_t size,
           int (*cmp)(const void *, const void *));
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
int rand(void);
void srand(unsigned int seed);
#define RAND_MAX 0x7fffffff

void *bsearch(const void *key, const void *base, size_t n, size_t size,
              int (*cmp)(const void *, const void *));
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#endif
