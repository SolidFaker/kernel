#ifndef STRING_H
#define STRING_H

#include "common.h"

void memcpy(u8 *dest, const u8 *src, u32 len);
void memset(u8 *dest, u8 val, u32 len);
void bzero(u8 *dest, u32 len);
int strcmp(const char *str1, const char *str2);
char *strchr(const char *s, int c);
char *strcpy(char *dest, const char *src);
char *strcat(char *dest, const char *src);
int strlen(const char *src);

#endif // INCLUDE_STRING_H_
