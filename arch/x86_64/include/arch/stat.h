#ifndef ARCH_STAT_H
#define ARCH_STAT_H
#include "common.h"

struct arch_linux_stat {
    u64 st_dev;
    u64 st_ino;
    u64 st_nlink;
    u32 st_mode;
    u32 st_uid;
    u32 st_gid;
    u32 pad0;
    u64 st_rdev;
    s64 st_size;
    s64 st_blksize;
    s64 st_blocks;
    u64 st_atime_sec;
    u64 st_atime_nsec;
    u64 st_mtime_sec;
    u64 st_mtime_nsec;
    u64 st_ctime_sec;
    u64 st_ctime_nsec;
};
#endif
