#include "drivers/hd.h"

static void waitdisk(void)
{
    // Wait for disk ready.
    while((inb(0x1F7) & 0xC0) != 0x40) ;
}

// Read a single sector at offset into dst.
static void readsect(void *dst, u32 offset)
{
    // Issue command.
    waitdisk();
    outb(0x1F2, 1); // count = 1
    outb(0x1F3, offset);
    outb(0x1F4, offset >> 8);
    outb(0x1F5, offset >> 16);
    outb(0x1F6, (offset >> 24) | 0xE0);
    outb(0x1F7, 0x20); // cmd 0x20 − read sectors
    // Read data.
    waitdisk();
    insl(0x1F0, dst, SECTOR_SIZE/4);
}

// Read ’count’ byte at ’offset’ sector from kernel into physical address ’pa’.
// Might copy more than asked.
void readseg(u8* pa, u32 count, u32 offset)
{
    u8* epa;
    epa = pa + count;
    pa -= offset % SECTOR_SIZE;
    offset = (offset / SECTOR_SIZE) ;
    // If this is too slow, we could read lots of sectors at a time.
    // We’d write more to memory than asked, but it doesn’t matter −−
    // we load in increasing order.
    for(; pa < epa; pa += SECTOR_SIZE, offset++)
        readsect(pa, offset);
}

static void writesect(const void *src, u32 offset)
{
    u32 i;
    waitdisk();
    outb(0x1F2, 1);
    outb(0x1F3, offset);
    outb(0x1F4, offset >> 8);
    outb(0x1F5, offset >> 16);
    outb(0x1F6, (offset >> 24) | 0xE0);
    outb(0x1F7, 0x30); /* write sectors */
    while ((inb(0x1F7) & 0x88) != 0x08) {
        if (inb(0x1F7) & 0x01) {
            return;
        }
    }
    for (i = 0; i < SECTOR_SIZE / 2; i++) {
        outw(0x1F0, ((const u16 *)src)[i]);
    }
}

void writeseg(const u8 *pa, u32 count, u32 offset)
{
    const u8 *epa = pa + count;
    pa -= offset % SECTOR_SIZE;
    offset /= SECTOR_SIZE;
    for (; pa < epa; pa += SECTOR_SIZE, offset++) {
        writesect(pa, offset);
    }
}

void hd_flush(void)
{
    waitdisk();
    outb(0x1F7, 0xE7);
    waitdisk();
}
