#ifndef DRIVERS_PL011_H
#define DRIVERS_PL011_H

#include "common.h"

void pl011_init(void);
void pl011_write(const char *text, u32 len);
int pl011_getchar(void);

#endif
