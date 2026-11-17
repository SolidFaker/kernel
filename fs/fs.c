#include "fs.h"
#include "errno.h"

struct fs_node *fs_root = 0; // The root of the filesystem.

u32 read_fs(struct fs_node *node, u32 offset, u32 size, u8 *buffer)
{
    // Has the node got a read callback?
    if (node->read != 0) {
        return node->read(node, offset, size, buffer);
    } else {
        return 0;
    }
}

int write_fs(struct fs_node *node, u32 offset, u32 size, u8 *buffer)
{
    if (node->write != 0) {
        return node->write(node, offset, size, buffer);
    }
    return -EROFS;
}

void open_fs(struct fs_node *node, __UNUSED__ u8 read, __UNUSED__ u8 write)
{
    // Has the node got an open callback?
    if (node->open != 0) {
        return node->open(node);
    }
}

void close_fs(struct fs_node *node)
{
    // Has the node got a close callback?
    if (node->close != 0) {
        return node->close(node);
    }
}

struct dirent *readdir_fs(struct fs_node *node, u32 index)
{
    // Is the node a directory, and does it have a callback?
    if ( (node->flags&0x7) == FS_DIRECTORY && node->readdir != 0 ) {
        return node->readdir(node, index);
    } else {
        return 0;
    }
}

struct fs_node *finddir_fs(struct fs_node *node, char *name)
{
    // Is the node a directory, and does it have a callback?
    if ( (node->flags&0x7) == FS_DIRECTORY && node->finddir != 0 ) {
        return node->finddir(node, name);
    } else {
        return 0;
    }
}

struct fs_node *create_fs(struct fs_node *dir, const char *name, int is_dir)
{
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY || !dir->create) {
        return 0;
    }
    return dir->create(dir, (char *)name, (u32)is_dir);
}

int unlink_fs(struct fs_node *dir, const char *name)
{
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY || !dir->unlink) {
        return -EROFS;
    }
    return dir->unlink(dir, (char *)name);
}

int rename_fs(struct fs_node *olddir, const char *name,
              struct fs_node *newdir, const char *newname)
{
    if (!olddir || (olddir->flags & 0x7) != FS_DIRECTORY || !olddir->rename ||
            !newdir || (newdir->flags & 0x7) != FS_DIRECTORY) {
        return -EXDEV;
    }
    return olddir->rename(olddir, (char *)name, newdir, (char *)newname);
}

int truncate_fs(struct fs_node *node, u32 length)
{
    if (!node || !node->truncate) {
        return -EINVAL;
    }
    return node->truncate(node, length);
}
