// Filesystem bootstrap: import the read-only myfs image into the writable
// in-memory ramfs, then expose that as the VFS root.

#include "vfs.h"
#include "debug.h"
#include "fs.h"
#include "fs/myfs.h"
#include "drivers/hd.h"
#include "mm.h"
#include "string.h"
#include "tools.h"
#include "drivers/display.h"

#define SECTOR_SIZE 512

static u8 sector_buffer[SECTOR_SIZE];

static u32 myfs_read_entry(struct myfs_entry *e, u32 offset, u32 size,
                           u8 *buffer)
{
    u32 done = 0;

    if (offset >= e->data_size) {
        return 0;
    }
    if (size > e->data_size - offset) {
        size = e->data_size - offset;
    }
    while (done < size) {
        u32 sec = e->data_sector + (offset + done) / SECTOR_SIZE;
        u32 in_off = (offset + done) % SECTOR_SIZE;
        u32 chunk = SECTOR_SIZE - in_off;
        if (chunk > size - done) {
            chunk = size - done;
        }
        readseg(sector_buffer, SECTOR_SIZE, sec * SECTOR_SIZE);
        memcpy(buffer + done, sector_buffer + in_off, chunk);
        done += chunk;
    }
    return size;
}

struct fs_node *init_vfs_empty(void)
{
    fs_root = init_ramfs();
    if (!fs_root) {
        ERROR("RAMFS INIT FAILED!");
        return 0;
    }
    create_fs(fs_root, "dev", 1);
    return fs_root;
}

void vfs_import_myfs(void)
{
    u8 first[SECTOR_SIZE];
    struct myfs_struct *hdr;
    struct myfs_entry *ent;
    u32 total, i;
    u8 *image;

    if (!fs_root) {
        return;
    }
    readseg(first, SECTOR_SIZE, 10 * SECTOR_SIZE);
    hdr = (struct myfs_struct *)first;
    if (hdr->magic != MYFS_MAGIC) {
        ERROR("RAMDISK INIT FAILED!");
        return;
    }
    total = sizeof(struct myfs_struct) +
            hdr->entry_num * sizeof(struct myfs_entry);
    image = (u8 *)kmalloc(total);
    if (!image) {
        ERROR("RAMDISK OUT OF MEMORY!");
        return;
    }
    readseg(image, total, 10 * SECTOR_SIZE);
    hdr = (struct myfs_struct *)image;
    ent = (struct myfs_entry *)(image + sizeof(struct myfs_struct));

    for (i = 1; i < hdr->entry_num; i++) {
        char name[sizeof(ent[i].entry_name) + 1];
        struct fs_node *node;
        u8 *data;
        u32 n;

        if (ent[i].entry_type != MYFS_FILE) {
            continue;
        }
        memcpy((u8 *)name, (const u8 *)ent[i].entry_name,
               sizeof(ent[i].entry_name));
        name[sizeof(ent[i].entry_name)] = 0;

        node = create_fs(fs_root, name, 0);
        if (!node || ent[i].data_size == 0) {
            continue;
        }
        data = (u8 *)kmalloc(ent[i].data_size);
        if (!data) {
            continue;
        }
        n = myfs_read_entry(&ent[i], 0, ent[i].data_size, data);
        write_fs(node, 0, n, data);
        kfree(data);
    }
    kfree(image);
}

struct fs_node *init_vfs()
{
    if (!init_vfs_empty()) {
        return 0;
    }
    vfs_import_myfs();
    return fs_root;
}
