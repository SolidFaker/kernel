#include "common.h"
#include "elf.h"
#include "drivers/hd.h"
#include "fs/myfs.h"
#include "string.h"

struct myfs_struct *header = NULL;
struct myfs_entry *entries = NULL;

extern void enter_long_mode(u32 entry);

static struct myfs_entry *find_file(const char *name)
{
    u8 i;
    for (i = 0; header && i < header->entry_num && i < MYFS_ENTRY_NUM; i++) {
        if (entries[i].entry_type == MYFS_FILE &&
                strcmp(entries[i].entry_name, name) == 0) {
            return &entries[i];
        }
    }
    return NULL;
}

static void init_myfs(u32 header_offset)
{
    header = (struct myfs_struct *)header_offset;
    readseg((u8 *)header, sizeof(struct myfs_struct), 10 * SECTOR_SIZE);
    if (header->magic != MYFS_MAGIC) {
        header = NULL;
        return;
    }
    entries = (struct myfs_entry *)(header_offset + sizeof(struct myfs_struct));
    readseg((u8 *)entries, sizeof(struct myfs_entry) * header->entry_num,
            10 * SECTOR_SIZE + sizeof(struct myfs_struct));
}

void loadermain(void)
{
    struct myfs_entry *tmp;
    u32 sector;

    init_myfs(0x20000);
    if (!header) return;
    tmp = find_file("kernel.bin");
    if (!tmp) return;
    sector = tmp->data_sector;
    readseg((u8 *)0x100000, tmp->data_size, sector * SECTOR_SIZE);
    enter_long_mode(0x100000);
}
