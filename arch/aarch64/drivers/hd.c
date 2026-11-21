#include "drivers/hd.h"
#include "string.h"

#define ARM_MYFS_ADDR 0x48000000u

void readseg(u8 *pa, u32 count, u32 offset)
{
    if (offset < 10 * SECTOR_SIZE) {
        memset(pa, 0, count);
        return;
    }
    memcpy(pa, (const u8 *)(uaddr)(ARM_MYFS_ADDR + offset - 10 * SECTOR_SIZE),
           count);
}

void writeseg(const u8 *pa, u32 count, u32 offset)
{
    (void)pa;
    (void)count;
    (void)offset;
}

void hd_flush(void)
{
}
