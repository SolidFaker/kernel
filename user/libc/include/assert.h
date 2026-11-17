#ifndef _LIBC_ASSERT_H
#define _LIBC_ASSERT_H
void __assert_fail(const char *expr, const char *file, int line)
    __attribute__((noreturn));
#define assert(e) ((e) ? (void)0 : __assert_fail(#e, __FILE__, __LINE__))
#endif
