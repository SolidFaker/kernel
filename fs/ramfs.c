// A small writable in-memory filesystem. The read-only myfs image is
// imported into it at boot, so existing files are visible and writable
// while new files/directories live purely in RAM.

#include "fs.h"
#include "errno.h"
#include "mm.h"
#include "string.h"

struct ramfs_inode {
    struct fs_node node;
    struct ramfs_inode *parent;
    struct ramfs_inode *child;
    struct ramfs_inode *sibling;
    u8 *data;
    u32 size;
    u32 cap;
};

static struct dirent ramfs_dirent;
static u32 ramfs_next_ino = 1;

static struct fs_node *ri_create(struct fs_node *dir, char *name, u32 is_dir);
static int ri_unlink(struct fs_node *dir, char *name);
static int ri_rename(struct fs_node *olddir, char *name,
                     struct fs_node *newdir, char *newname);

static u32 ri_read(struct fs_node *node, u32 offset, u32 size, u8 *buffer)
{
    struct ramfs_inode *ri = (struct ramfs_inode *)node;
    u32 n = 0;

    if (offset >= ri->size) {
        return 0;
    }
    if (size > ri->size - offset) {
        size = ri->size - offset;
    }
    while (n < size) {
        buffer[n] = ri->data[offset + n];
        n++;
    }
    return n;
}

static int ri_grow(struct ramfs_inode *ri, u32 need)
{
    u32 ncap;
    u8 *nd;

    if (need <= ri->cap) {
        return 0;
    }
    ncap = ri->cap ? ri->cap : 512;
    while (ncap < need) {
        ncap *= 2;
    }
    nd = (u8 *)kmalloc(ncap);
    if (!nd) {
        return -ENOMEM;
    }
    if (ri->data && ri->size) {
        memcpy(nd, ri->data, ri->size);
    }
    if (ri->data) {
        kfree(ri->data);
    }
    ri->data = nd;
    ri->cap = ncap;
    return 0;
}

static int ri_write(struct fs_node *node, u32 offset, u32 size, u8 *buffer)
{
    struct ramfs_inode *ri = (struct ramfs_inode *)node;
    u32 i, end = offset + size;

    if ((node->flags & 0x7) != FS_FILE) {
        return -EISDIR;
    }
    if (ri_grow(ri, end) < 0) {
        return -ENOMEM;
    }
    if (offset > ri->size) {
        for (i = ri->size; i < offset; i++) {
            ri->data[i] = 0;
        }
    }
    for (i = 0; i < size; i++) {
        ri->data[offset + i] = buffer[i];
    }
    if (end > ri->size) {
        ri->size = end;
    }
    node->length = ri->size;
    return (int)size;
}

static int ri_truncate(struct fs_node *node, u32 length)
{
    struct ramfs_inode *ri = (struct ramfs_inode *)node;
    u32 i;

    if ((node->flags & 0x7) != FS_FILE) {
        return -EISDIR;
    }
    if (length > ri->size) {
        if (ri_grow(ri, length) < 0) {
            return -ENOMEM;
        }
        for (i = ri->size; i < length; i++) {
            ri->data[i] = 0;
        }
    }
    ri->size = length;
    node->length = length;
    return 0;
}

static struct ramfs_inode *ri_find_child(struct ramfs_inode *dir,
                                         const char *name)
{
    struct ramfs_inode *c = dir->child;
    while (c) {
        if (!strcmp(c->node.name, (char *)name)) {
            return c;
        }
        c = c->sibling;
    }
    return 0;
}

static struct dirent *ri_readdir(struct fs_node *node, u32 index)
{
    struct ramfs_inode *dir = (struct ramfs_inode *)node;
    struct ramfs_inode *c = dir->child;

    while (c && index > 0) {
        c = c->sibling;
        index--;
    }
    if (!c) {
        return 0;
    }
    strcpy(ramfs_dirent.name, c->node.name);
    ramfs_dirent.ino = c->node.inode;
    return &ramfs_dirent;
}

static struct fs_node *ri_finddir(struct fs_node *node, char *name)
{
    struct ramfs_inode *c = ri_find_child((struct ramfs_inode *)node, name);
    return c ? &c->node : 0;
}

static void ri_init_node(struct ramfs_inode *ri, const char *name,
                         int is_dir, struct ramfs_inode *parent)
{
    memset((u8 *)ri, 0, sizeof(*ri));
    strcpy(ri->node.name, (char *)name);
    ri->node.inode = ramfs_next_ino++;
    ri->parent = parent;
    if (is_dir) {
        ri->node.flags = FS_DIRECTORY;
        ri->node.readdir = &ri_readdir;
        ri->node.finddir = &ri_finddir;
        ri->node.create = &ri_create;
        ri->node.unlink = &ri_unlink;
        ri->node.rename = &ri_rename;
    } else {
        ri->node.flags = FS_FILE;
        ri->node.read = &ri_read;
        ri->node.write = &ri_write;
        ri->node.truncate = &ri_truncate;
    }
}

static void ri_detach(struct ramfs_inode *ri)
{
    struct ramfs_inode **pp;
    if (!ri->parent) {
        return;
    }
    pp = &ri->parent->child;
    while (*pp && *pp != ri) {
        pp = &(*pp)->sibling;
    }
    if (*pp == ri) {
        *pp = ri->sibling;
    }
    ri->parent = 0;
    ri->sibling = 0;
}

static struct fs_node *ri_create(struct fs_node *dir, char *name, u32 is_dir)
{
    struct ramfs_inode *d = (struct ramfs_inode *)dir;
    struct ramfs_inode *ri;

    if (!name || !*name || strlen(name) >= (int)sizeof(ri->node.name)) {
        return 0;
    }
    if (ri_find_child(d, name)) {
        return 0;
    }
    ri = (struct ramfs_inode *)kmalloc(sizeof(struct ramfs_inode));
    if (!ri) {
        return 0;
    }
    ri_init_node(ri, name, is_dir != 0, d);
    ri->sibling = d->child;
    d->child = ri;
    return &ri->node;
}

static int ri_unlink(struct fs_node *dir, char *name)
{
    struct ramfs_inode *d = (struct ramfs_inode *)dir;
    struct ramfs_inode *c = ri_find_child(d, name);

    if (!c) {
        return -ENOENT;
    }
    if ((c->node.flags & 0x7) == FS_DIRECTORY && c->child) {
        return -ENOTEMPTY;
    }
    ri_detach(c);
    if (c->data) {
        kfree(c->data);
    }
    kfree(c);
    return 0;
}

static int ri_rename(struct fs_node *olddir, char *name,
                     struct fs_node *newdir, char *newname)
{
    struct ramfs_inode *od = (struct ramfs_inode *)olddir;
    struct ramfs_inode *nd = (struct ramfs_inode *)newdir;
    struct ramfs_inode *c = ri_find_child(od, name);
    struct ramfs_inode *dst;
    struct ramfs_inode *p;

    if (!c) {
        return -ENOENT;
    }
    if (!newname || strlen(newname) >= (int)sizeof(c->node.name)) {
        return -EINVAL;
    }
    if (c == nd) {
        return -EINVAL;
    }
    for (p = nd; p; p = p->parent) {
        if (p == c) {
            return -EINVAL;  // cannot move a directory into itself
        }
    }

    dst = ri_find_child(nd, newname);
    if (dst) {
        if ((dst->node.flags & 0x7) == FS_DIRECTORY || dst->child) {
            return -EEXIST;
        }
        ri_detach(dst);
        if (dst->data) {
            kfree(dst->data);
        }
        kfree(dst);
    }

    ri_detach(c);
    strcpy(c->node.name, newname);
    c->parent = nd;
    c->sibling = nd->child;
    nd->child = c;
    return 0;
}

struct fs_node *init_ramfs(void)
{
    struct ramfs_inode *root =
        (struct ramfs_inode *)kmalloc(sizeof(struct ramfs_inode));

    if (!root) {
        return 0;
    }
    ri_init_node(root, "root", 1, 0);
    return &root->node;
}
