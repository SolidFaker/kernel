#ifndef BOOTINFO_H
#define BOOTINFO_H

#include "common.h"

/* The 16-bit loader writes this block at physical 0x7600 after setting
   a VBE linear-framebuffer mode. The kernel reads it through the
   PAGE_OFFSET alias. */
#define BOOTINFO_ADDR  0x7600
#define BOOTINFO_MAGIC 0x31454256   /* "VBE1" */

struct bootinfo {
    u32 magic;
    u32 fb_addr;
    u16 width;
    u16 height;
    u16 pitch;
    u8  bpp;
    u8  reserved;
} __PACKED;

#endif
