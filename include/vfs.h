#ifndef VFS_H
#define VFS_H

#include "common.h"

struct fs_node;

struct fs_node *init_vfs();
struct fs_node *init_vfs_empty(void);
void vfs_import_myfs(void);
int vfs_resolve(const char *path, struct fs_node **out, int follow_final);
int vfs_mkdir_p(const char *path, u32 mode);
int vfs_symlink(const char *path, const char *target);
int vfs_noexec_path(const char *path);
int vfs_no_persist_path(const char *path);
void vfs_apply_policies(void);

#endif
