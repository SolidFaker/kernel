#ifndef COMMON_H
#define COMMON_H

#define NULL 0
#define __UNUSED__    __attribute__((unused))

#define __INIT        __attribute__((section(".init.text")))
#define __INITDATA    __attribute__((section(".init.data")))
#define __PACKED      __attribute__((packed))

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;
typedef __UINT64_TYPE__ u64;
typedef __INT64_TYPE__ s64;
typedef int s32;
typedef __SIZE_TYPE__ usize;
typedef __UINTPTR_TYPE__ uaddr;
typedef __UINTPTR_TYPE__ reg_t;
typedef __INTPTR_TYPE__ saddr;

#include "arch/cpu.h"

#endif
