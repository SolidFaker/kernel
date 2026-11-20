// Persistent log-structured filesystem layer for the in-memory tree.
//
// Records are appended to one of two 8 MiB journal regions. At boot the
// active region is replayed; when a region fills up (or on the first
// boot) the current tree is checkpointed into the other region, so the
// journal becomes the authoritative on-disk filesystem.

#include "jfs.h"
#include "errno.h"
#include "drivers/hd.h"
#include "fs.h"
#include "mm.h"
#include "string.h"
#include "tools.h"
#include "vfs.h"

#define JFS_REGION_CAP (JFS_SECTORS - 1)

struct jfs_super {
    u32 magic;
    u32 version;
    u32 active;
    u32 head;
    u32 seq;
    u32 snapshot;
    u32 crc;
    u8  pad[484];
} __PACKED;

static u32 jfs_active;
static u32 jfs_head;
static u32 jfs_seq = 1;
static int jfs_ready;

static u32 region_base(u32 active)
{
    return JFS_LBA + 1 + active * JFS_SECTORS;
}

static void jfs_read_sector(u32 sec, u8 *buf)
{
    readseg(buf, SECTOR_SIZE, sec * SECTOR_SIZE);
}

static void jfs_write_sector(u32 sec, const u8 *buf)
{
    writeseg(buf, SECTOR_SIZE, sec * SECTOR_SIZE);
}

static int read_super(struct jfs_super *sb)
{
    jfs_read_sector(JFS_LBA, (u8 *)sb);
    return sb->magic == JFS_SUPER_MAGIC && sb->version == 2;
}

static void write_super(void)
{
    u8 sector[SECTOR_SIZE];
    struct jfs_super *sb = (struct jfs_super *)sector;

    memset(sector, 0, SECTOR_SIZE);
    sb->magic = JFS_SUPER_MAGIC;
    sb->version = 2;
    sb->active = jfs_active;
    sb->head = jfs_head;
    sb->seq = jfs_seq;
    sb->snapshot = 1;
    jfs_write_sector(JFS_LBA, sector);
    hd_flush();
}

int jfs_has_snapshot(void)
{
    struct jfs_super sb;
    return read_super(&sb) && sb.snapshot != 0;
}

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

static struct fs_node *lookup_path(const char *path)
{
    struct fs_node *node = 0;
    if (!path || vfs_resolve(path, &node, 1) < 0) {
        return 0;
    }
    return node;
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
            node = create_fs(dir, name, r->type == JFS_CREATE_DIR);
            if (node && (r->offset & 0x80000000u)) {
                node->mask = r->offset & 07777;
            }
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
    case JFS_CHMOD:
        node = lookup_path(r->path);
        if (node) {
            node->mask = r->offset & 07777;
        }
        break;
    case JFS_SYMLINK:
        if (lookup_path(r->path)) {
            return;
        }
        if (split_parent(r->path, &dir, name) == 0 && data &&
                r->data_len < 160) {
            char target[160];
            memcpy((u8 *)target, data, r->data_len);
            target[r->data_len] = 0;
            symlink_fs(dir, name, target);
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

static int jfs_emit(u32 base, u32 *hp, u32 *sp, u32 type,
                    const char *path, const char *path2, u32 offset,
                    const u8 *data, u32 len)
{
    u8 sector[SECTOR_SIZE];
    struct jfs_rec *r = (struct jfs_rec *)sector;
    u32 sectors, done, c;

    if (!path || strlen(path) >= sizeof(r->path) ||
            (path2 && strlen(path2) >= sizeof(r->path2)) ||
            len > 65536) {
        return -EINVAL;
    }
    sectors = 1 + (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (*hp + sectors > JFS_REGION_CAP) {
        return -ENOSPC;
    }

    memset(sector, 0, SECTOR_SIZE);
    jfs_write_sector(base + *hp, sector);

    done = 0;
    while (done < len) {
        u32 chunk = SECTOR_SIZE;
        if (chunk > len - done) {
            chunk = len - done;
        }
        memset(sector, 0, SECTOR_SIZE);
        memcpy(sector, (const u8 *)data + done, chunk);
        jfs_write_sector(base + *hp + 1 + done / SECTOR_SIZE, sector);
        done += chunk;
    }

    memset(sector, 0, SECTOR_SIZE);
    r->type = type;
    r->seq = *sp;
    r->offset = offset;
    r->data_len = len;
    memcpy((u8 *)r->path, (const u8 *)path, strlen(path) + 1);
    if (path2) {
        memcpy((u8 *)r->path2, (const u8 *)path2, strlen(path2) + 1);
    }
    c = rec_crc(r, data ? data : (u8 *)"");
    r->crc = c;
    r->magic = JFS_REC_MAGIC;
    jfs_write_sector(base + *hp, sector);
    hd_flush();

    *hp += sectors;
    (*sp)++;
    return 0;
}

static int snapshot_dir(struct fs_node *dir, const char *prefix,
                        u32 base, u32 *hp, u32 *sp, u8 *data)
{
    u32 index;
    struct dirent *de;

    for (index = 0;; index++) {
        struct fs_node *child;
        char path[160];
        u32 pl, nl;

        de = readdir_fs(dir, index);
        if (!de) {
            break;
        }
        child = finddir_fs(dir, de->name);
        if (!child) {
            continue;
        }
        pl = (u32)strlen(prefix);
        nl = (u32)strlen(de->name);
        if (pl + nl + 2 > sizeof(path)) {
            return -ERANGE;
        }
        if (pl == 0) {
            path[0] = '/';
            memcpy((u8 *)(path + 1), (const u8 *)de->name, nl + 1);
        } else {
            memcpy((u8 *)path, (const u8 *)prefix, pl);
            path[pl] = '/';
            memcpy((u8 *)(path + pl + 1), (const u8 *)de->name, nl + 1);
        }
        if (vfs_no_persist_path(path)) {
            continue;
        }

        if ((child->flags & 0x7) == FS_DIRECTORY) {
            int r = jfs_emit(base, hp, sp, JFS_CREATE_DIR, path, 0,
                             0x80000000u | (child->mask & 07777), 0, 0);
            if (r < 0) {
                return r;
            }
            r = snapshot_dir(child, path, base, hp, sp, data);
            if (r < 0) {
                return r;
            }
        } else if ((child->flags & 0x7) == FS_SYMLINK) {
            char target[160];
            u32 n = read_fs(child, 0, sizeof(target) - 1, (u8 *)target);
            target[n] = 0;
            int r = jfs_emit(base, hp, sp, JFS_SYMLINK, path, 0,
                             0777, (u8 *)target, n);
            if (r < 0) {
                return r;
            }
        } else {
            u32 off = 0;
            int r = jfs_emit(base, hp, sp, JFS_CREATE_FILE, path, 0,
                             0x80000000u | (child->mask & 07777), 0, 0);
            if (r < 0) {
                return r;
            }
            while (off < child->length) {
                u32 n = child->length - off;
                if (n > 32768) {
                    n = 32768;
                }
                n = read_fs(child, off, n, data);
                if (n == 0) {
                    break;
                }
                r = jfs_emit(base, hp, sp, JFS_WRITE, path, 0, off,
                             data, n);
                if (r < 0) {
                    return r;
                }
                off += n;
            }
        }
    }
    return 0;
}

static int jfs_compact(void)
{
    u8 *buf;
    u32 target = jfs_active ^ 1;
    u32 head = 0, seq = jfs_seq;
    int r;

    buf = (u8 *)kmalloc(32768);
    if (!buf) {
        return -ENOMEM;
    }
    r = snapshot_dir(fs_root, "", region_base(target), &head, &seq, buf);
    kfree(buf);
    if (r < 0) {
        return r;
    }
    jfs_active = target;
    jfs_head = head;
    jfs_seq = seq;
    write_super();
    return 0;
}

void jfs_init(void)
{
    struct jfs_super sb;
    u32 base, sec, replayed = 0, limit;

    if (!read_super(&sb)) {
        jfs_active = 0;
        jfs_head = 0;
        jfs_seq = 1;
        jfs_ready = 1;
        write_super();
        jfs_compact();
        return;
    }

    jfs_active = sb.active & 1;
    jfs_seq = sb.seq ? sb.seq : 1;
    limit = sb.head;
    if (limit > JFS_REGION_CAP) {
        limit = JFS_REGION_CAP;
    }
    base = region_base(jfs_active);

    sec = 0;
    while (sec < limit) {
        u8 sector[SECTOR_SIZE];
        struct jfs_rec hdr;
        struct jfs_rec *r = (struct jfs_rec *)sector;
        u8 *data = 0;
        u32 sectors, done;

        jfs_read_sector(base + sec, sector);
        if (r->magic != JFS_REC_MAGIC) {
            break;
        }
        hdr = *r;
        if (hdr.data_len > 65536) {
            break;
        }
        sectors = 1 + (hdr.data_len + SECTOR_SIZE - 1) / SECTOR_SIZE;
        if (sec + sectors > limit) {
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
                jfs_read_sector(base + sec + 1 + done / SECTOR_SIZE,
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
        if (data) kfree(data);
        sec += sectors;
    }
    jfs_head = sec;
    jfs_ready = 1;

    if (sb.snapshot && replayed == 0) {
        /* Snapshot metadata says there should be records; fall back. */
        vfs_import_myfs();
        jfs_compact();
        return;
    }
    if (!sb.snapshot || jfs_head * 4 > JFS_REGION_CAP * 3) {
        jfs_compact();
    }
}

int jfs_log(u32 type, const char *path, const char *path2, u32 offset,
            const u8 *data, u32 len)
{
    u32 flags;
    u32 sectors;
    int r;

    if (!jfs_ready || vfs_no_persist_path(path) ||
            (path2 && vfs_no_persist_path(path2))) {
        return 0;
    }
    flags = local_irq_save();
    sectors = 1 + (len + SECTOR_SIZE - 1) / SECTOR_SIZE;
    if (jfs_head + sectors > JFS_REGION_CAP) {
        r = jfs_compact();
        if (r < 0) {
            local_irq_restore(flags);
            return -ENOSPC;
        }
    }
    r = jfs_emit(region_base(jfs_active), &jfs_head, &jfs_seq,
                 type, path, path2, offset, data, len);
    if (r == 0) {
        write_super();
    }
    local_irq_restore(flags);
    return r;
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
