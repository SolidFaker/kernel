#ifndef VFS_H
#define VFS_H

#include "common.h"

struct fs_node;

struct fs_node *init_vfs();
struct fs_node *init_vfs_empty(void);
void vfs_import_myfs(void);

#endif
