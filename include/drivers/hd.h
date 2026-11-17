#ifndef DRIVERS_HD_H
#define DRIVERS_HD_H

#include "common.h"

#define SECTOR_SIZE 512

void readseg(u8* pa, u32 count, u32 offset);
void writeseg(const u8 *pa, u32 count, u32 offset);
void hd_flush(void);

#endif
