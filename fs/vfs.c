#include "vfs.h"
#include "debug.h"
#include "tools.h"
#include "mm.h"
#include "string.h"
#include "fs.h"
#include "fs/myfs.h"
#include "drivers/hd.h"

#define DIRINIT(dir) \
    node_##dir->mask = 0;\
    node_##dir->uid = 0;\
    node_##dir->gid = 0;\
    node_##dir->inode = 0;\
    node_##dir->length = 0;\
    node_##dir->flags = FS_DIRECTORY;\
    node_##dir->read = 0;\
    node_##dir->write = 0;\
    node_##dir->open = 0;\
    node_##dir->close = 0;\
    node_##dir->readdir = &readdir;\
    node_##dir->finddir = &finddir;\
    node_##dir->ptr = 0;\
    node_##dir->impl = 0

struct myfs_struct *header;
struct myfs_entry  *entries;

struct fs_node *node_root;
struct fs_node *node_dev;
struct fs_node *nodes;   // List of file nodes.
u32 node_num;            // Number of file nodes.

struct dirent dirent;
static u8 sector_buffer[SECTOR_SIZE];

static u32 read(struct fs_node *node, u32 offset, u32 size, u8 *buffer)
{
    struct myfs_entry entry = entries[node->inode];

    if (offset >= entry.data_size) {
        return 0;
    }
    if (size > entry.data_size - offset) {
        size = entry.data_size - offset;
    }

    // files span whole sectors; copy the requested range chunk by chunk
    u32 done = 0;
    while (done < size) {
        u32 sec = entry.data_sector + (offset + done) / SECTOR_SIZE;
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

static struct dirent *readdir(struct fs_node *node, u32 index)
{
    if (node == node_root && index == 0)
    {
        strcpy(dirent.name, "dev");
        dirent.name[3] = 0;
        dirent.ino = 0;
        return &dirent;
    }

    if (index-1 >= node_num)
        return 0;
    strcpy(dirent.name, nodes[index-1].name);
    dirent.name[strlen(nodes[index-1].name)] = 0;
    dirent.ino = nodes[index-1].inode;
    return &dirent;
}

static struct fs_node *finddir(struct fs_node *node, char *name)
{
    if (node == node_root && !strcmp(name, "dev"))
        return node_dev;

    u32 i;
    for (i = 0; i < node_num; i++)
        if (!strcmp(name, nodes[i].name))
            return &nodes[i];
    return 0;
}


struct fs_node *init_vfs()
{
    void *first_sector = kmalloc(SECTOR_SIZE);
    readseg((u8 *)first_sector, SECTOR_SIZE, 10 * SECTOR_SIZE);

    header = (struct myfs_struct *)(first_sector);

    if (header->magic != MYFS_MAGIC){
        ERROR("RAMDISK INIT FAILED!");
    }

    entries = (struct myfs_entry *)((u32)first_sector + sizeof(struct myfs_struct));

    // Initialise the root directory.
    node_root = (struct fs_node *)kmalloc(sizeof(struct fs_node));
    strcpy(node_root->name, "root");
    DIRINIT(root);

    // Initialise the /dev directory (required!)
    node_dev = (struct fs_node *)kmalloc(sizeof(struct fs_node));
    strcpy(node_dev->name, "dev");
    DIRINIT(dev);

    nodes = (struct fs_node *)kmalloc(sizeof(struct fs_node) * header->entry_num);
    node_num = header->entry_num;
    // For every file...
    u32 i;
    for (i = 0; i < node_num; i++)
    {
        // Create a new file node.
        strcpy(nodes[i].name, entries[i].entry_name);
        nodes[i].mask = 0;
        nodes[i].uid = 0;
        nodes[i].gid = 0;
        nodes[i].length = entries[i].data_size;
        nodes[i].inode = i;
        switch (entries[i].entry_type)
        {
            case MYFS_FILE:
                nodes[i].flags = FS_FILE;
                break;
            case MYFS_DIR:
                nodes[i].flags = FS_DIRECTORY;
                break;
        }
        nodes[i].read = &read;
        nodes[i].write = 0;
        nodes[i].readdir = 0;
        nodes[i].finddir = 0;
        nodes[i].open = 0;
        nodes[i].close = 0;
        nodes[i].impl = 0;
    }
    return node_root;
}
