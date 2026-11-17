// Persistent write-ahead journal for the in-memory ramfs.
//
// The read-only myfs image is imported into ramfs at boot, then committed
// journal records are replayed in order. This makes user changes survive a
// reboot without needing an in-place on-disk filesystem.

#include "jfs.h"
#include "errno.h"
#include "drivers/hd.h"
#include "drivers/tty.h"
#include "fs.h"
#include "mm.h"
#include "string.h"
#include "tools.h"

#define JFS_MAX_DATA    65536

struct jfs_super {
    u32 magic;
    u32 version;
    u32 head;
    u32 seq;
    u32 crc;
    u8  pad[492];
} __PACKED;

static u32 jfs_head = 1;
static u32 jfs_seq = 1;
static int jfs_ready;

static u32 crc32_update(u32 crc, const u8 *p, u32 len)
{
    u32 i;

    crc = ~crc;
    while (len--) {
        crc ^= *p++;
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

static u32 rec_crc(struct jfs_rec *r, const u8 *data)
{
    u32 magic = r->magic;
    u32 stored = r->crc;
    u32 c;

    r->magic = 0;
    r->crc = 0;
    c = crc32_update(0, (const u8 *)r, sizeof(*r));
    c = crc32_update(c, data, r->data_len);
    r->magic = magic;
    r->crc = stored;
    return c;
}

static void jfs_read_sector(u32 sec, u8 *buf)
{
    readseg(buf, SECTOR_SIZE, sec * SECTOR_SIZE);
}

static void jfs_write_sector(u32 sec, const u8 *buf)
{
    writeseg(buf, SECTOR_SIZE, sec * SECTOR_SIZE);
}

static struct fs_node *lookup_path(const char *path)
{
    char abs[160];
    char *p;
    struct fs_node *cur = fs_root;
    u32 len = (u32)strlen(path);

    if (!path || !*path) {
        return 0;
    }
    if (path[0] != '/') {
        abs[0] = '/';
        if (len > sizeof(abs) - 2) {
            return 0;
        }
        memcpy((u8 *)(abs + 1), (const u8 *)path, len + 1);
    } else {
        if (len >= sizeof(abs)) {
            return 0;
        }
        memcpy((u8 *)abs, (const u8 *)path, len + 1);
    }

    p = abs;
    while (*p) {
        char name[128];
        u32 n = 0;
        while (*p == '/') p++;
        if (!*p) break;
        while (*p && *p != '/' && n < sizeof(name) - 1) {
            name[n++] = *p++;
        }
        name[n] = 0;
        if (!strcmp(name, ".")) continue;
        if (!strcmp(name, "..")) { cur = fs_root; continue; }
        if (!cur || (cur->flags & 0x7) != FS_DIRECTORY || !cur->finddir) {
            return 0;
        }
        cur = cur->finddir(cur, name);
        if (!cur) return 0;
    }
    return cur;
}

static int split_parent(const char *path, struct fs_node **dir, char *name)
{
    char abs[160];
    char *p, *last = 0;
    struct fs_node *d;
    u32 len = (u32)strlen(path);

    if (!path || path[0] != '/') {
        return -EINVAL;
    }
    if (len >= sizeof(abs)) {
        return -ERANGE;
    }
    memcpy((u8 *)abs, (const u8 *)path, len + 1);
    for (p = abs; *p; p++) {
        if (*p == '/') last = p;
    }
    if (!last || !last[1]) {
        return -EINVAL;
    }
    memcpy((u8 *)name, (const u8 *)(last + 1),
           (u32)strlen(last + 1) + 1);
    if (last == abs) {
        *dir = fs_root;
        return 0;
    }
    *last = 0;
    d = lookup_path(abs);
    *last = '/';
    if (!d) {
        return -ENOENT;
    }
    *dir = d;
    return 0;
}

static void jfs_apply(struct jfs_rec *r, u8 *data)
{
    struct fs_node *node, *dir;
    char name[128];

    switch (r->type) {
    case JFS_CREATE_FILE:
    case JFS_CREATE_DIR:
        if (lookup_path(r->path)) {
            return;
        }
        if (split_parent(r->path, &dir, name) == 0) {
            create_fs(dir, name, r->type == JFS_CREATE_DIR);
        }
        break;
    case JFS_WRITE:
        node = lookup_path(r->path);
        if (node && r->data_len) {
            write_fs(node, r->offset, r->data_len, data);
        }
        break;
    case JFS_TRUNCATE:
        node = lookup_path(r->path);
        if (node) {
            truncate_fs(node, r->offset);
        }
        break;
    case JFS_UNLINK:
    case JFS_RMDIR:
        if (split_parent(r->path, &dir, name) == 0) {
            unlink_fs(dir, name);
        }
        break;
    case JFS_RENAME:
        {
            struct fs_node *nd;
            char nname[128];
            if (split_parent(r->path, &dir, name) == 0 &&
                    split_parent(r->path2, &nd, nname) == 0) {
                rename_fs(dir, name, nd, nname);
            }
        }
        break;
    }
}

void jfs_init(void)
{
    u8 *sector = (u8 *)kmalloc(SECTOR_SIZE);
    struct jfs_super *sb = (struct jfs_super *)sector;
    u32 sec, replayed = 0;

    if (!sector) {
        return;
    }
    jfs_read_sector(JFS_LBA, sector);
    if (sb->magic != JFS_SUPER_MAGIC) {
        memset(sector, 0, SECTOR_SIZE);
        sb->magic = JFS_SUPER_MAGIC;
        sb->version = 1;
        sb->head = 1;
        sb->seq = 1;
        jfs_write_sector(JFS_LBA, sector);
        hd_flush();
        jfs_head = 1;
        jfs_seq = 1;
        jfs_ready = 1;
        kfree(sector);
        return;
    }

    sec = 1;
    while (sec < JFS_SECTORS) {
        struct jfs_rec hdr;
        struct jfs_rec *r = (struct jfs_rec *)sector;
        u8 *data = 0;
        u32 sectors, done;

        jfs_read_sector(JFS_LBA + sec, sector);
        if (r->magic != JFS_REC_MAGIC) {
            break;
        }
        hdr = *r;
        if (hdr.data_len > JFS_MAX_DATA) {
            break;
        }
        sectors = 1 + (hdr.data_len + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (sec + sectors > JFS_SECTORS) {
            break;
        }
        if (hdr.data_len) {
            data = (u8 *)kmalloc(hdr.data_len);
            if (!data) {
                break;
            }
            done = 0;
            while (done < hdr.data_len) {
                u32 chunk = SECTOR_SIZE;
                if (chunk > hdr.data_len - done) {
                    chunk = hdr.data_len - done;
                }
                jfs_read_sector(JFS_LBA + sec + 1 + done / SECTOR_SIZE,
                                sector);
                memcpy(data + done, sector, chunk);
                done += chunk;
            }
        }
        if (rec_crc(&hdr, data ? data : (u8 *)"") != hdr.crc) {
            if (data) kfree(data);
            break;
        }
        jfs_apply(&hdr, data);
        jfs_seq = hdr.seq + 1;
        replayed++;
        if (data) {
            kfree(data);
        }
        sec += sectors;
    }
    jfs_head = sec;
    jfs_ready = 1;
    if (replayed) {
        printk("jfs: replayed %d records\n", (int)replayed);
    }
    kfree(sector);
}

int jfs_log(u32 type, const char *path, const char *path2, u32 offset,
            const u8 *data, u32 len)
{
    u8 sector[SECTOR_SIZE];
    struct jfs_rec *r = (struct jfs_rec *)sector;
    u32 sectors, done;
    u32 c;

    if (!jfs_ready) {
        return 0;
    }
    if (!path || strlen(path) >= sizeof(r->path) ||
            (path2 && strlen(path2) >= sizeof(r->path2)) ||
            len > JFS_MAX_DATA) {
        return -EINVAL;
    }
    sectors = 1 + (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (jfs_head + sectors > JFS_SECTORS) {
        return -ENOSPC;
    }

    memset(sector, 0, SECTOR_SIZE);
    jfs_write_sector(JFS_LBA + jfs_head, sector);
    done = 0;
    while (done < len) {
        u32 chunk = SECTOR_SIZE;
        if (chunk > len - done) {
            chunk = len - done;
        }
        memset(sector, 0, SECTOR_SIZE);
        memcpy(sector, (const u8 *)data + done, chunk);
        jfs_write_sector(JFS_LBA + jfs_head + 1 + done / SECTOR_SIZE, sector);
        done += chunk;
    }

    memset(sector, 0, SECTOR_SIZE);
    r->type = type;
    r->seq = jfs_seq;
    r->offset = offset;
    r->data_len = len;
    memcpy((u8 *)r->path, (const u8 *)path, strlen(path) + 1);
    if (path2) {
        memcpy((u8 *)r->path2, (const u8 *)path2, strlen(path2) + 1);
    }
    c = rec_crc(r, data ? data : (u8 *)"");
    r->crc = c;
    r->magic = JFS_REC_MAGIC;
    jfs_write_sector(JFS_LBA + jfs_head, sector);
    hd_flush();

    jfs_head += sectors;
    jfs_seq++;
    return 0;
}

int jfs_log_write(const char *path, u32 offset, const u8 *data, u32 len)
{
    u32 done = 0;

    while (done < len) {
        u32 chunk = len - done > 32768 ? 32768 : len - done;
        int r = jfs_log(JFS_WRITE, path, 0, offset + done,
                        data + done, chunk);
        if (r < 0) {
            return r;
        }
        done += chunk;
    }
    return 0;
}
