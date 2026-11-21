#include "fs.h"
#include "errno.h"
#include "task.h"

static int fs_group_member(u32 gid)
{
    u32 i;
    if (current->egid == gid) return 1;
    for (i = 0; i < current->ngroups && i < 16; i++) {
        if (current->groups[i] == gid) return 1;
    }
    return 0;
}

int fs_permission(struct fs_node *node, u32 mode)
{
    u32 shift, allowed;
    if (!node) return -ENOENT;
    if (!current || current->euid == 0) return 0;
    if (current->euid == node->uid) {
        shift = 6;
    } else if (fs_group_member(node->gid)) {
        shift = 3;
    } else {
        shift = 0;
    }
    allowed = (node->mask >> shift) & 7;
    return (mode & ~allowed) ? -EACCES : 0;
}

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
    if (!node || (node->flags & FS_READONLY)) {
        return -EROFS;
    }
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
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY || !dir->create ||
            (dir->flags & FS_READONLY)) {
        return 0;
    }
    if (fs_permission(dir, W_OK | X_OK) < 0) {
        return 0;
    }
    return dir->create(dir, (char *)name, (u32)is_dir);
}

int unlink_fs(struct fs_node *dir, const char *name)
{
    struct fs_node *target;
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY || !dir->unlink ||
            (dir->flags & FS_READONLY)) {
        return -EROFS;
    }
    if (fs_permission(dir, W_OK | X_OK) < 0) {
        return -EACCES;
    }
    target = finddir_fs(dir, (char *)name);
    if (target && (target->flags & FS_APPEND_ONLY)) {
        return -EPERM;
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
    if ((olddir->flags & FS_READONLY) || (newdir->flags & FS_READONLY)) {
        return -EROFS;
    }
    if (fs_permission(olddir, W_OK | X_OK) < 0 ||
            fs_permission(newdir, W_OK | X_OK) < 0) {
        return -EACCES;
    }
    {
        struct fs_node *target = finddir_fs(olddir, (char *)name);
        if (target && (target->flags & FS_APPEND_ONLY)) {
            return -EPERM;
        }
    }
    return olddir->rename(olddir, (char *)name, newdir, (char *)newname);
}

int truncate_fs(struct fs_node *node, u32 length)
{
    if (!node || !node->truncate) {
        return -EINVAL;
    }
    if (node->flags & (FS_READONLY | FS_APPEND_ONLY)) {
        return -EPERM;
    }
    if (fs_permission(node, W_OK) < 0) {
        return -EACCES;
    }
    return node->truncate(node, length);
}

struct fs_node *symlink_fs(struct fs_node *dir, const char *name,
                           const char *target)
{
    if (!dir || (dir->flags & 0x7) != FS_DIRECTORY || !dir->symlink ||
            (dir->flags & FS_READONLY)) {
        return 0;
    }
    if (fs_permission(dir, W_OK | X_OK) < 0) {
        return 0;
    }
    return dir->symlink(dir, (char *)name, (char *)target);
}
