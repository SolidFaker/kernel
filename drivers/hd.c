#include "drivers/hd.h"
#include "string.h"

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

void readseg(u8 *pa, u32 count, u32 offset)
{
    u8 sector[SECTOR_SIZE];
    u32 done = 0;

    while (done < count) {
        u32 current = offset + done;
        u32 in_sector = current % SECTOR_SIZE;
        u32 chunk = SECTOR_SIZE - in_sector;
        if (chunk > count - done) {
            chunk = count - done;
        }
        if (in_sector == 0 && chunk == SECTOR_SIZE) {
            readsect(pa + done, current / SECTOR_SIZE);
        } else {
            readsect(sector, current / SECTOR_SIZE);
            memcpy(pa + done, sector + in_sector, chunk);
        }
        done += chunk;
    }
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
