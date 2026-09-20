#ifndef FS_H
#define FS_H

#include "common.h"

#define FS_FILE        0x01
#define FS_DIRECTORY   0x02
#define FS_CHARDEVICE  0x03
#define FS_BLOCKDEVICE 0x04
#define FS_PIPE        0x05
#define FS_SYMLINK     0x06
#define FS_MOUNTPOINT  0x08 // Is the file an active mountpoint?

// Per-task file descriptors (see struct task_struct)
#define FD_MAX 16

#define FD_FREE    0
#define FD_TTY_IN  1   // fd 0: the keyboard
#define FD_TTY_OUT 2   // fd 1/2: the task's tty
#define FD_FILE    3   // a regular file on the (read-only) filesystem
#define FD_DIR     4   // a directory, iterable with getdents()

struct file {
    u8 used;
    u8 kind;
    struct fs_node *node;
    u32 offset;    // file position / getdents index
};

// Minimal POSIX-style stat (see fstat)
#define S_IFREG 0x8000
#define S_IFDIR 0x4000

struct stat {
    u32 st_mode;
    u32 st_size;
};

struct fs_node;

typedef              u32 (*fn_read_fs)    (struct fs_node*,u32,u32,u8*);
typedef              u32 (*fn_write_fs)   (struct fs_node*,u32,u32,u8*);
typedef             void (*fn_open_fs)    (struct fs_node*);
typedef             void (*fn_close_fs)   (struct fs_node*);
typedef  struct dirent * (*fn_readdir_fs) (struct fs_node*,u32);
typedef struct fs_node * (*fn_finddir_fs) (struct fs_node*,char *name);

struct fs_node {
    char name[128]; // The filename
    u32 mask;       // The permissions mask
    u32 uid;        // The owner's id
    u32 gid;        // The owning group
    u32 flags;      // Includes the node's type
    u32 inode;      // This is device-specific - provided a way for a fs to indentify files
    u32 length;     // Size of the file by byte
    u32 impl;       // An implementation-defined number
    fn_read_fs read;
    fn_write_fs write;
    fn_open_fs open;
    fn_close_fs close;
    fn_readdir_fs readdir;
    fn_finddir_fs finddir;
    struct fs_node *ptr; // used by mountpoints and symlinks
};

// One of these is returned by the readdir call, according to POSIX.
struct dirent
{
    char name[128]; // Filename.
    u32 ino;        // Inode number. Required by POSIX.
};

extern struct fs_node *fs_root; // The root of the filesystem.

// Standard read/write/open/close functions. Note that these are all suffixed with
// _fs to distinguish them from the read/write/open/close which deal with file descriptors
// not file nodes.
u32 read_fs(struct fs_node *node, u32 offset, u32 size, u8 *buffer);
u32 write_fs(struct fs_node *node, u32 offset, u32 size, u8 *buffer);
void open_fs(struct fs_node *node, u8 read, u8 write);
void close_fs(struct fs_node *node);
struct dirent *readdir_fs(struct fs_node *node, u32 index);
struct fs_node *finddir_fs(struct fs_node *node, char *name);

#endif
