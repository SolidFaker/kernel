// Filesystem bootstrap: import the read-only myfs image into the writable
// in-memory ramfs, then expose that as the VFS root.

#include "vfs.h"
#include "debug.h"
#include "fs.h"
#include "fs/myfs.h"
#include "drivers/hd.h"
#include "mm.h"
#include "string.h"
#include "tools.h"
#include "drivers/display.h"
#include "errno.h"

#define SECTOR_SIZE 512

static u8 sector_buffer[SECTOR_SIZE];

static u32 myfs_read_entry(struct myfs_entry *e, u32 offset, u32 size,
                           u8 *buffer)
{
    u32 done = 0;

    if (offset >= e->data_size) {
        return 0;
    }
    if (size > e->data_size - offset) {
        size = e->data_size - offset;
    }
    while (done < size) {
        u32 sec = e->data_sector + (offset + done) / SECTOR_SIZE;
        u32 in_off = (offset + done) % SECTOR_SIZE;
        u32 chunk = SECTOR_SIZE - in_off;
        if (chunk > size - done) {
            chunk = size - done;
        }
        readseg(sector_buffer, SECTOR_SIZE, sec * SECTOR_SIZE);
        memcpy(buffer + done, sector_buffer + in_off, chunk);
        done += chunk;
    }
    return size;
}

int vfs_resolve(const char *path, struct fs_node **out, int follow_final)
{
    char work[256], next[256], cwd[256], target[160];
    u32 links = 0;

    if (!path || !out || path[0] != '/') {
        return -EINVAL;
    }
    if ((u32)strlen(path) >= sizeof(work)) {
        return -ERANGE;
    }
    strcpy(work, (char *)path);

    for (;;) {
        struct fs_node *cur = fs_root;
        char *p = work;
        int restart = 0;
        cwd[0] = '/';
        cwd[1] = 0;

        while (*p) {
            char name[128];
            char *rest;
            struct fs_node *child;
            u32 n = 0;

            while (*p == '/') p++;
            if (!*p) break;
            while (p[n] && p[n] != '/' && n + 1 < sizeof(name)) n++;
            if (p[n] == '/' || p[n]) {
                if (n + 1 >= sizeof(name) && p[n] && p[n] != '/') {
                    return -ERANGE;
                }
            }
            memcpy((u8 *)name, (const u8 *)p, n);
            name[n] = 0;
            p += n;
            rest = p;
            while (*rest == '/') rest++;

            if (!strcmp(name, ".")) {
                continue;
            }
            if (!strcmp(name, "..")) {
                u32 l;
                if (strcmp(cwd, "/") != 0) {
                    char *slash = cwd + strlen(cwd);
                    while (slash > cwd + 1 && slash[-1] != '/') slash--;
                    if (slash > cwd + 1) slash--;
                    *slash = 0;
                    if (!cwd[0]) {
                        cwd[0] = '/';
                        cwd[1] = 0;
                    }
                }
                l = (u32)strlen(cwd);
                if (*rest) {
                    if (l + 1 + strlen(rest) + 1 > sizeof(next)) return -ERANGE;
                    if (l > 1) strcat(cwd, "/");
                    strcat(cwd, rest);
                }
                strcpy(work, cwd);
                restart = 1;
                break;
            }
            if (!cur || (cur->flags & 0x7) != FS_DIRECTORY) {
                return -ENOTDIR;
            }
            {
                int perm = fs_permission(cur, X_OK);
                if (perm < 0) return perm;
            }
            child = finddir_fs(cur, name);
            if (!child) {
                return -ENOENT;
            }
            if ((child->flags & 0x7) == FS_SYMLINK &&
                    (*rest || follow_final)) {
                u32 len;
                if (++links > 16) {
                    return -ELOOP;
                }
                len = read_fs(child, 0, sizeof(target) - 1, (u8 *)target);
                target[len] = 0;
                next[0] = 0;
                if (target[0] == '/') {
                    if (strlen(target) >= sizeof(next)) return -ERANGE;
                    strcpy(next, target);
                } else {
                    if (strlen(cwd) + strlen(target) + 2 > sizeof(next)) {
                        return -ERANGE;
                    }
                    strcpy(next, cwd);
                    if (strlen(next) > 1) strcat(next, "/");
                    strcat(next, target);
                }
                if (*rest) {
                    if (strlen(next) + strlen(rest) + 2 > sizeof(next)) {
                        return -ERANGE;
                    }
                    if (strlen(next) > 1 || next[0] != '/') strcat(next, "/");
                    strcat(next, rest);
                }
                strcpy(work, next);
                restart = 1;
                break;
            }
            cur = child;
            if (*rest) {
                if ((cur->flags & 0x7) != FS_DIRECTORY) {
                    return -ENOTDIR;
                }
                if (strlen(cwd) + strlen(name) + 2 > sizeof(cwd)) {
                    return -ERANGE;
                }
                if (strlen(cwd) > 1) strcat(cwd, "/");
                strcat(cwd, name);
            }
        }
        if (!restart) {
            *out = cur;
            return 0;
        }
    }
}

static int vfs_parent(const char *path, struct fs_node **dir, char *name)
{
    char abs[256];
    char *last;
    u32 len = (u32)strlen(path);

    if (len == 0 || len >= sizeof(abs)) {
        return -ERANGE;
    }
    memcpy((u8 *)abs, (const u8 *)path, len + 1);
    last = abs + len;
    while (last > abs && *last != '/') last--;
    if (!*last) {
        return -EINVAL;
    }
    if (!last[1]) {
        return -EINVAL;
    }
    strcpy(name, last + 1);
    if (last == abs) {
        return vfs_resolve("/", dir, 1);
    }
    *last = 0;
    return vfs_resolve(abs, dir, 1);
}

int vfs_mkdir_p(const char *path, u32 mode)
{
    char partial[256];
    char *p;

    if (!path || path[0] != '/' || strlen(path) >= sizeof(partial)) {
        return -EINVAL;
    }
    strcpy(partial, (char *)path);
    if (!strcmp(partial, "/")) {
        return 0;
    }
    for (p = partial + 1; ; p++) {
        if (*p == '/' || !*p) {
            char saved = *p;
            struct fs_node *node = 0;
            *p = 0;
            if (vfs_resolve(partial, &node, 1) < 0) {
                struct fs_node *parent;
                char name[128];
                int r = vfs_parent(partial, &parent, name);
                if (r < 0) return r;
                node = create_fs(parent, name, 1);
                if (!node) return -EIO;
                node->mask = mode;
            }
            if ((node->flags & 0x7) != FS_DIRECTORY) {
                return -ENOTDIR;
            }
            *p = saved;
            if (!saved) break;
        }
    }
    return 0;
}

int vfs_symlink(const char *path, const char *target)
{
    struct fs_node *parent, *node;
    char name[128];
    int r;

    if (!path || !target || !*target) {
        return -EINVAL;
    }
    if (vfs_resolve(path, &node, 0) == 0) {
        return -EEXIST;
    }
    r = vfs_parent(path, &parent, name);
    if (r < 0) return r;
    if (parent->flags & FS_READONLY) return -EROFS;
    node = symlink_fs(parent, name, target);
    return node ? 0 : -EIO;
}

static int path_in(const char *path, const char *area)
{
    while (*area) {
        if (*path++ != *area++) return 0;
    }
    return *path == 0 || *path == '/';
}

int vfs_noexec_path(const char *path)
{
    return path_in(path, "/config") || path_in(path, "/state") ||
           path_in(path, "/log") || path_in(path, "/cache") ||
           path_in(path, "/tmp") || path_in(path, "/run");
}

int vfs_no_persist_path(const char *path)
{
    return path_in(path, "/tmp") || path_in(path, "/run") ||
           path_in(path, "/dev") || path_in(path, "/proc") ||
           path_in(path, "/sys");
}

static int vfs_create_base_tree(void)
{
    static const char *const dirs[] = {
        "/boot", "/efi", "/store/system-profiles/0/bin",
        "/store/system-profiles/0/lib", "/store/system-profiles/0/include",
        "/store/system-profiles/0/share",
        "/store/system-profiles/0/share/tests",
        "/store/system-profiles/0/share/doc", "/profiles/users/root/bin",
        "/profiles/apps", "/config/system", "/config/users/root",
        "/state/system", "/state/users/root", "/state/users/alice",
        "/log/system", "/log/users/root", "/log/users/alice",
        "/cache/system", "/cache/users/root", "/cache/users/alice",
        "/profiles/users/alice/bin", "/config/users/alice",
        "/home/root", "/home/alice", "/run", "/tmp", "/mounts", "/dev", "/proc", "/sys"
    };
    static const char *const special[] = {
        "/home/root", "/config/users/root", "/state/users/root",
        "/log/users/root", "/cache/users/root", "/home/alice",
        "/state/users/alice", "/log/users/alice", "/cache/users/alice"
    };
    u32 i;
    int r;
    struct fs_node *node;

    for (i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++) {
        r = vfs_mkdir_p(dirs[i], 0755);
        if (r < 0) return r;
    }
    for (i = 0; i < sizeof(special) / sizeof(special[0]); i++) {
        struct fs_node *node;
        if (vfs_resolve(special[i], &node, 1) == 0) {
            node->mask = 0700;
        }
    }
    if (vfs_resolve("/tmp", &node, 1) == 0) {
        node->mask = 01777;
    }
    if (vfs_resolve("/run", &node, 1) == 0) {
        node->mask = 01777;
    }
    return vfs_symlink("/system", "/store/system-profiles/0");
}

struct fs_node *init_vfs_empty(void)
{
    fs_root = init_ramfs();
    if (!fs_root) {
        ERROR("RAMFS INIT FAILED!");
        return 0;
    }
    if (vfs_create_base_tree() < 0) {
        ERROR("ROOT TREE INIT FAILED!");
        return 0;
    }
    return fs_root;
}

static int path_join(char *out, u32 size, const char *base, const char *name)
{
    u32 bl = (u32)strlen(base), nl = (u32)strlen(name);
    if (bl + nl + 2 > size) return -ERANGE;
    strcpy(out, (char *)base);
    if (bl && out[bl - 1] != '/') strcat(out, "/");
    strcat(out, (char *)name);
    return 0;
}

static u32 vfs_crc32(const u8 *data, u32 len)
{
    u32 crc = 0xffffffffu, i;
    while (len--) {
        crc ^= *data++;
        for (i = 0; i < 8; i++) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1)));
        }
    }
    return ~crc;
}

static void vfs_hex8(u32 value, char *out)
{
    static const char hex[] = "0123456789abcdef";
    int i;
    for (i = 7; i >= 0; i--) {
        out[i] = hex[value & 15];
        value >>= 4;
    }
    out[8] = 0;
}

static int vfs_write_file(const char *path, const u8 *data, u32 size, u32 mode)
{
    struct fs_node *parent, *node;
    char name[128];
    int r = vfs_parent(path, &parent, name);
    if (r < 0) return r;
    node = create_fs(parent, name, 0);
    if (!node) return -EIO;
    node->mask = mode;
    if (size && write_fs(node, 0, size, (u8 *)data) != (int)size) {
        return -EIO;
    }
    return 0;
}

static void vfs_set_owner(const char *path, u32 uid, u32 gid)
{
    struct fs_node *node;
    if (vfs_resolve(path, &node, 1) == 0) {
        node->uid = uid;
        node->gid = gid;
    }
}

static void vfs_init_users(void)
{
    static const char root_record[] =
        "name=root\nuid=0\ngid=0\nhome=/home/root\nshell=/system/bin/bash\n";
    static const char alice_record[] =
        "name=alice\nuid=1000\ngid=1000\nhome=/home/alice\nshell=/system/bin/bash\n";
    struct fs_node *node;

    if (vfs_resolve("/config/users/root/user", &node, 1) < 0) {
        vfs_write_file("/config/users/root/user", (const u8 *)root_record,
                       sizeof(root_record) - 1, 0644);
    }
    if (vfs_resolve("/config/users/alice/user", &node, 1) < 0) {
        vfs_write_file("/config/users/alice/user", (const u8 *)alice_record,
                       sizeof(alice_record) - 1, 0644);
    }
    vfs_set_owner("/home/alice", 1000, 1000);
    vfs_set_owner("/state/users/alice", 1000, 1000);
    vfs_set_owner("/log/users/alice", 1000, 1000);
    vfs_set_owner("/cache/users/alice", 1000, 1000);
}

static void vfs_expose(const char *profile_dir, const char *name,
                       const char *target, int user_visible)
{
    char path[256];
    if (path_join(path, sizeof(path), profile_dir, name) == 0) {
        vfs_symlink(path, target);
    }
    if (user_visible) {
        if (path_join(path, sizeof(path), "/profiles/users/root/bin", name) == 0) {
            vfs_symlink(path, target);
        }
    }
}

static int vfs_make_package(const char *name, const char *version, u32 crc,
                            const char *subdir, char *pkg, u32 pkg_size)
{
    char hash[9], leaf[160], dir[256];
    int r;

    vfs_hex8(crc, hash);
    if (strlen(hash) + strlen(name) + strlen(version) + 3 > sizeof(leaf)) {
        return -ERANGE;
    }
    strcpy(leaf, hash);
    strcat(leaf, "-");
    strcat(leaf, name);
    strcat(leaf, "-");
    strcat(leaf, version);
    r = path_join(pkg, pkg_size, "/store", leaf);
    if (r < 0) return r;
    r = vfs_mkdir_p(pkg, 0555);
    if (r < 0) return r;
    if (subdir) {
        r = path_join(dir, sizeof(dir), pkg, subdir);
        if (r < 0) return r;
        r = vfs_mkdir_p(dir, 0555);
    }
    return r;
}

static char coreutils_package[256];
static char userctl_package[256];

static int vfs_has_prefix(const u8 *data, u32 size, const char *prefix)
{
    u32 len = (u32)strlen(prefix), i;
    if (size < len) return 0;
    for (i = 0; i < len; i++) {
        if (data[i] != (u8)prefix[i]) return 0;
    }
    return 1;
}

static int vfs_import_entry(struct myfs_entry *ent)
{
    char name[sizeof(ent->entry_name) + 1];
    char package_name[sizeof(ent->entry_name) + 1];
    char pkg[256], path[256], profile_dir[256];
    const char *package, *version, *subdir = "bin", *output = name;
    u32 mode = 0755;
    int user_visible = 1;
    u8 *data;
    u32 n, crc;
    int r;

    memcpy((u8 *)name, (const u8 *)ent->entry_name,
           sizeof(ent->entry_name));
    name[sizeof(ent->entry_name)] = 0;
    data = (u8 *)kmalloc(ent->data_size ? ent->data_size : 1);
    if (!data) return -ENOMEM;
    n = myfs_read_entry(ent, 0, ent->data_size, data);

    if (!strcmp(name, "loader.bin") || !strcmp(name, "kernel.elf")) {
        r = path_join(path, sizeof(path), "/boot", name);
        if (r == 0) r = vfs_write_file(path, data, n, 0755);
        kfree(data);
        return r;
    }

    if (coreutils_package[0] &&
            vfs_has_prefix(data, n, "#!/system/bin/coreutils")) {
        r = path_join(path, sizeof(path), coreutils_package, "bin");
        if (r == 0) r = path_join(path, sizeof(path), path, name);
        if (r == 0) r = vfs_write_file(path, data, n, 0755);
        if (r == 0) {
            vfs_expose("/store/system-profiles/0/bin", name, path, 1);
        }
        kfree(data);
        return r;
    }

    if (!strcmp(name, "bash.elf")) {
        package = "bash"; version = "5.2.37"; output = "bash";
    } else if (!strcmp(name, "coreutils.elf")) {
        package = "coreutils"; version = "9.5"; output = "coreutils";
    } else if (!strcmp(name, "posix-test.sh") ||
               !strcmp(name, "users-test.sh")) {
        package = "posix-tests"; version = "1"; subdir = "share/tests";
        mode = 0644; user_visible = 0;
    } else if (!strcmp(name, "README")) {
        package = "docs"; version = "1"; subdir = "share/doc";
        mode = 0644; user_visible = 0;
    } else {
        char *dot;
        strcpy(package_name, name);
        dot = strchr(package_name, '.');
        if (dot) *dot = 0;
        package = package_name;
        version = "0";
        output = name;
    }

    crc = vfs_crc32(data, n);
    r = vfs_make_package(package, version, crc, subdir, pkg, sizeof(pkg));
    if (r < 0) {
        kfree(data);
        return r;
    }
    if (!strcmp(package, "coreutils")) {
        strcpy(coreutils_package, pkg);
    } else if (!strcmp(package, "userctl")) {
        strcpy(userctl_package, pkg);
    }
    r = path_join(path, sizeof(path), pkg, subdir);
    if (r == 0) r = path_join(path, sizeof(path), path, output);
    if (r == 0) r = vfs_write_file(path, data, n, mode);
    if (r == 0) {
        if (!strcmp(subdir, "bin")) {
            strcpy(profile_dir, "/store/system-profiles/0/bin");
        } else if (!strcmp(subdir, "share/tests")) {
            strcpy(profile_dir, "/store/system-profiles/0/share/tests");
        } else {
            strcpy(profile_dir, "/store/system-profiles/0/share/doc");
        }
        vfs_expose(profile_dir, output, path, user_visible);
    }
    kfree(data);
    return r;
}

static void vfs_replace_symlink(const char *path, const char *target)
{
    struct fs_node *node, *parent;
    char name[128];
    if (vfs_resolve(path, &node, 0) == 0 &&
            vfs_parent(path, &parent, name) == 0) {
        unlink_fs(parent, name);
    }
    vfs_symlink(path, target);
}

void vfs_import_myfs(void)
{
    u8 first[SECTOR_SIZE];
    struct myfs_struct *hdr;
    struct myfs_entry *ent;
    u32 total, i;
    u8 *image;

    if (!fs_root) {
        return;
    }
    readseg(first, SECTOR_SIZE, 10 * SECTOR_SIZE);
    hdr = (struct myfs_struct *)first;
    if (hdr->magic != MYFS_MAGIC) {
        ERROR("RAMDISK INIT FAILED!");
        return;
    }
    total = sizeof(struct myfs_struct) +
            hdr->entry_num * sizeof(struct myfs_entry);
    image = (u8 *)kmalloc(total);
    if (!image) {
        ERROR("RAMDISK OUT OF MEMORY!");
        return;
    }
    readseg(image, total, 10 * SECTOR_SIZE);
    hdr = (struct myfs_struct *)image;
    ent = (struct myfs_entry *)(image + sizeof(struct myfs_struct));

    for (i = 1; i < hdr->entry_num; i++) {
        if (ent[i].entry_type == MYFS_FILE) {
            vfs_import_entry(&ent[i]);
        }
    }
    kfree(image);
    vfs_symlink("/store/system-profiles/0/bin/sh", "bash");
    vfs_symlink("/profiles/users/root/bin/sh",
                "/store/system-profiles/0/bin/bash");
    if (userctl_package[0]) {
        static const char *const commands[] = {
            "login", "su", "adduser", "id", "whoami"
        };
        char target[256], link[256];
        u32 i;
        path_join(target, sizeof(target), userctl_package, "bin/userctl.elf");
        for (i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
            path_join(link, sizeof(link), "/store/system-profiles/0/bin",
                      commands[i]);
            vfs_replace_symlink(link, target);
            path_join(link, sizeof(link), "/profiles/users/root/bin",
                      commands[i]);
            vfs_replace_symlink(link, target);
        }
    }
}

static void vfs_mark_tree(struct fs_node *node, u32 flag)
{
    u32 i;
    if (!node) return;
    node->flags |= flag;
    for (i = 0;; i++) {
        struct dirent *de = readdir_fs(node, i);
        struct fs_node *child;
        if (!de) break;
        child = finddir_fs(node, de->name);
        if (child) vfs_mark_tree(child, flag);
    }
}

void vfs_apply_policies(void)
{
    struct fs_node *node;
    if (vfs_resolve("/store", &node, 1) == 0) {
        vfs_mark_tree(node, FS_READONLY);
    }
    if (vfs_resolve("/log", &node, 1) == 0) {
        vfs_mark_tree(node, FS_APPEND_ONLY);
    }
}

struct fs_node *init_vfs()
{
    if (!init_vfs_empty()) {
        return 0;
    }
    vfs_init_users();
    vfs_import_myfs();
    return fs_root;
}
