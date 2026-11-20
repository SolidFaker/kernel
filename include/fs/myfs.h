#ifndef FS_MYFS_H
#define FS_MYFS_H

#include "common.h"

#define CLUSTER_SIZE    512

#define MYFS_MAGIC      0xEB001234
#define MYFS_DIR        1
#define MYFS_FILE       2
#define MYFS_ENTRY_NUM  128

struct myfs_struct {
    u32 magic;
    u32 entry_num;
};

struct myfs_entry {
    u8   entry_type;        // 0 = Unused otherwise 1 = Dir or 2 = File
    char entry_name[16];
    u32  entry_attribute;
    u32  child_head_index;
    u32  next_index;
    u32  data_sector;
    u32  data_count;
    u32  data_size;
};

#endif
