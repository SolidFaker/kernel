#include "string.h"
// Copy len bytes from src to dest.
inline void memcpy(u8 *dest, const u8 *src, u32 len)
{
    const u8 *sp = (const u8 *)src;
    u8 *dp = (u8 *)dest;
    for(; len != 0; len--) *dp++ = *sp++;
}

// Write len copies of val into dest.
inline void memset(u8 *dest, u8 val, u32 len)
{
    u8 *temp = (u8 *)dest;
    for ( ; len != 0; len--) *temp++ = val;
}

// Write len copies of val into dest.
inline void bzero(u8 *dest, u32 len)
{
    memset(dest, 0, len);
}

// Compare two strings. Should return -1 if
// str1 < str2, 0 if they are equal or 1 otherwise.
inline int strcmp(const char *str1, const char *str2)
{
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }

    if ((u8)*str1 < (u8)*str2) {
        return -1;
    }
    if ((u8)*str1 > (u8)*str2) {
        return 1;
    }

    return 0;
}

// Copy the NULL-terminated string src into dest, and
// return dest.
inline char *strcpy(char *dest, const char *src)
{
    char *tmp = dest;

    while (*src) {
        *dest++ = *src++;
    }

    *dest = '\0';
    return tmp;
}

// Concatenate the NULL-terminated string src onto
// the end of dest, and return dest.
inline char *strcat(char *dest, const char *src)
{
    char *cp = dest;

    while (*cp) {
        cp++;
    }

    while ((*cp++ = *src++)) ;

    return dest;
}

inline int strlen(const char *src)
{
    const char *eos = src;

    while(*eos)++eos;
    return (eos - src);
}

