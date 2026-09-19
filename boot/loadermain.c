// Boot loader called by loaderasm.s

#include "common.h"
#include "elf.h"
#include "page.h"
#include "drivers/hd.h"
#include "fs/myfs.h"
#include "string.h"

struct myfs_struct *header = NULL;
struct myfs_entry *entries = NULL;

struct myfs_entry *find_file(const char *name)
{
    u8 i;
    for (i = 0; i < 64; i++) {
        if (entries[i].entry_type == MYFS_FILE) {
            if (strcmp(entries[i].entry_name, name) == 0) {
                return &entries[i];
            }
        }
    }
    return NULL;
}

void init_myfs(u32 header_offset)
{
    header = (struct myfs_struct *)(header_offset);
    readseg((u8 *)header, sizeof(struct myfs_struct), 10*SECTOR_SIZE);

    if (header->magic != MYFS_MAGIC){
        return;
    }

    entries = 
        (struct myfs_entry *)(header_offset + sizeof(struct myfs_struct));
    readseg(
            (u8 *)entries, 
            (sizeof(struct myfs_entry)*header->entry_num), 
            (10*SECTOR_SIZE + sizeof(struct myfs_struct))
           );
}

void loadermain(void)
{
    elf_header_t *elf;
    void (*entry)(void);

    elf = (elf_header_t *)(0x10000); // scratch space
    init_myfs(0x20000);
    if (entries == NULL) {
        return; // no valid myfs on disk, let loaderasm.s handle error
    }
    struct myfs_entry *tmp = find_file("kernel.elf");
    if (tmp == NULL) {
        return; // kernel.elf not found, let loaderasm.s handle error
    }

    u32 elf_sector = tmp->data_sector;

    // Read elf header off disk 8 * 512
    readseg((u8 *)elf, 4096, elf_sector * SECTOR_SIZE);

    // Is this an ELF executable?
    if(elf->magic != ELF_MAGIC) {
        return; // let loaderasm.S handle error
    }

    // Call the entry point from the ELF header.
    // Does not return!
    entry = (void(*)(void))disk_read_elf(elf, elf_sector * SECTOR_SIZE);
    entry();
}
