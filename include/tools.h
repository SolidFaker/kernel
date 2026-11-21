#ifndef TOOLS_H
#define TOOLS_H 

#include "common.h"
#include "drivers/display.h"

typedef __builtin_va_list va_list;

#define va_start(ap, last) (__builtin_va_start(ap, last))
#define va_arg(ap, type) (__builtin_va_arg(ap, type))
#define va_end(ap)

#define is_digit(c)	((c) >= '0' && (c) <= '9')

#define ZEROPAD		1   	// pad with zero
#define SIGN	 	  2   	// unsigned/signed long
#define PLUS    	4	    // show plus
#define SPACE	   	8   	// space if plus
#define LEFT	  	16  	// left justified
#define SPECIAL		32  	// 0x
#define SMALL	  	64  	// use 'abcdef' instead of 'ABCDEF'

#define do_div(n,base) ({ \
        int __res = (n) % (base); \
        n = (n) / (base); \
        __res; })


// 内核的打印函数
void printk(const char *format, ...);
int vsprintf(char *buff, const char *format, va_list args);
// 内核的打印函数带颜色
void printk_color(u8 fg, u8 bg, const char *format, ...);

#endif
