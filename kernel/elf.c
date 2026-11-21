#include "elf.h"
#include "drivers/hd.h"
#include "string.h"

void *disk_read_elf(struct elf_header *header, u32 header_offset)
{
    struct elf_section_header *ph, *eph;
    u8 *pa;

    ph = (elf_section_header_t *)((u8 *)header + header->phoff);
    eph = ph + header->phnum;
    for (; ph < eph; ph++) {
        pa = (u8 *)(uaddr)ph->paddr;
        readseg(pa, ph->filesz, header_offset + ph->off);
        if (ph->memsz > ph->filesz) {
            memset(pa + ph->filesz, 0, ph->memsz - ph->filesz);
        }
    }
    return (void *)(uaddr)header->entry;
}
