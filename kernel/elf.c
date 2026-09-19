#include "elf.h"
#include "drivers/hd.h"

    void *
disk_read_elf(struct elf_header *header, u32 header_offset)
{
    struct elf_section_header *ph, *eph;
    u8 *pa;
    // Load each program segment (ignores ph flags).
    ph = (elf_section_header_t *)((u8*)header + header->phoff);
    eph = ph + header->phnum;
    for(; ph < eph; ph++){
        pa = (u8 *)ph->paddr;
        readseg(pa, ph->filesz,  header_offset + ph->off);
        if(ph->memsz > ph->filesz) {
            stosb(pa + ph->filesz, 0, ph->memsz - ph->filesz);
        }
    }
    return (void *)(header->entry);
}
