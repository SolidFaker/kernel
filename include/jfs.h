#ifndef JFS_H
#define JFS_H

#include "common.h"

#define JFS_LBA        8192          /* journal area start (4 MiB) */
#define JFS_SECTORS    16384         /* 8 MiB journal */
#define JFS_REC_MAGIC  0x4A465231    /* "JFR1" */
#define JFS_SUPER_MAGIC 0x4A465332   /* "JFS2" */

#define JFS_CREATE_FILE 1
#define JFS_CREATE_DIR  2
#define JFS_WRITE       3
#define JFS_TRUNCATE    4
#define JFS_UNLINK      5
#define JFS_RMDIR       6
#define JFS_RENAME      7
#define JFS_CHMOD       8
#define JFS_SYMLINK     9

struct jfs_rec {
    u32 magic;
    u32 type;
    u32 seq;
    u32 offset;
    u32 data_len;
    u32 crc;
    char path[160];
    char path2[160];
    u8 pad[168];
} __PACKED;

int jfs_has_snapshot(void);
void jfs_init(void);
int jfs_log(u32 type, const char *path, const char *path2, u32 offset,
            const u8 *data, u32 len);
int jfs_log_write(const char *path, u32 offset, const u8 *data, u32 len);

#endif
