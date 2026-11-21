#ifndef ARCH_ARCH_H
#define ARCH_ARCH_H

#include "common.h"

void arch_console_init(void);
void arch_console_write(const char *text, u32 len);
void arch_idle(void);

#endif
