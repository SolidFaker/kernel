#ifndef ARCH_STAT_H
#define ARCH_STAT_H
#include "common.h"
struct arch_linux_stat {
    u64 st_dev;
    u64 st_ino;
    u32 st_mode;
    u32 st_nlink;
    u32 st_uid;
    u32 st_gid;
    u64 st_rdev;
    u64 pad0;
    s64 st_size;
    s32 st_blksize;
    u32 pad1;
    s64 st_blocks;
    u64 st_atime_sec;
    u64 st_atime_nsec;
    u64 st_mtime_sec;
    u64 st_mtime_nsec;
    u64 st_ctime_sec;
    u64 st_ctime_nsec;
    u32 unused[2];
};
#endif
