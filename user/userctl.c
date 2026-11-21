#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <dirent.h>

#define USER_TEXT_SIZE 256

struct user_record {
    char name[32];
    char home[96];
    char shell[96];
    uid_t uid;
    gid_t gid;
};

static int read_text(const char *path, char *buf, size_t cap)
{
    int fd, n;
    size_t used = 0;
    if (cap == 0) return -1;
    fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    while (used + 1 < cap) {
        n = read(fd, buf + used, cap - used - 1);
        if (n < 0) {
            close(fd);
            return -1;
        }
        if (n == 0) break;
        used += (size_t)n;
    }
    close(fd);
    buf[used] = 0;
    return (int)used;
}

static int record_value(const char *text, const char *key, char *out, size_t cap)
{
    size_t keylen = strlen(key);
    const char *p = text;
    while (*p) {
        const char *end;
        size_t len;
        if (!strncmp(p, key, keylen)) {
            end = strchr(p, '\n');
            if (!end) end = p + strlen(p);
            len = (size_t)(end - (p + keylen));
            if (len >= cap) len = cap - 1;
            memcpy(out, p + keylen, len);
            out[len] = 0;
            return 1;
        }
        end = strchr(p, '\n');
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

static void parse_user(const char *text, struct user_record *rec)
{
    char value[32];
    memset(rec, 0, sizeof(*rec));
    record_value(text, "name=", rec->name, sizeof(rec->name));
    record_value(text, "home=", rec->home, sizeof(rec->home));
    record_value(text, "shell=", rec->shell, sizeof(rec->shell));
    if (record_value(text, "uid=", value, sizeof(value)))
        rec->uid = (uid_t)strtol(value, 0, 10);
    if (record_value(text, "gid=", value, sizeof(value)))
        rec->gid = (gid_t)strtol(value, 0, 10);
    if (!rec->shell[0]) strcpy(rec->shell, "/system/bin/bash");
}

static int load_user_path(const char *path, struct user_record *rec)
{
    char text[USER_TEXT_SIZE];
    if (read_text(path, text, sizeof(text)) < 0) return -1;
    parse_user(text, rec);
    if (!rec->name[0]) return -1;
    return 0;
}

static int load_user_name(const char *name, struct user_record *rec)
{
    char path[160];
    snprintf(path, sizeof(path), "/config/users/%s/user", name);
    return load_user_path(path, rec);
}

static int load_user_uid(uid_t uid, struct user_record *rec)
{
    DIR *dir = opendir("/config/users");
    struct dirent *de;
    int found = -1;
    if (!dir) return -1;
    while ((de = readdir(dir)) != 0) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        if (load_user_name(de->d_name, rec) == 0 && rec->uid == uid) {
            found = 0;
            break;
        }
    }
    closedir(dir);
    return found;
}

static void print_group(gid_t gid, int separator)
{
    struct user_record rec;
    if (separator) printf(",");
    if (load_user_uid(gid, &rec) == 0) {
        printf("%u(%s)", gid, rec.name);
    } else {
        printf("%u", gid);
    }
}

static int command_whoami(void)
{
    uid_t uid = getuid();
    struct user_record rec;
    if (load_user_uid(uid, &rec) == 0) {
        printf("%s\n", rec.name);
    } else {
        printf("%u\n", uid);
    }
    return 0;
}

static int command_id(void)
{
    uid_t uid = getuid();
    gid_t gid = getgid();
    struct user_record rec;
    gid_t groups[16];
    int ngroups = getgroups(16, groups);
    int i;

    if (load_user_uid(uid, &rec) == 0) {
        printf("uid=%u(%s) ", uid, rec.name);
    } else {
        printf("uid=%u ", uid);
    }
    if (load_user_uid(gid, &rec) == 0) {
        printf("gid=%u(%s) groups=", gid, rec.name);
    } else {
        printf("gid=%u groups=", gid);
    }
    if (ngroups <= 0) {
        print_group(gid, 0);
    } else {
        for (i = 0; i < ngroups; i++) print_group(groups[i], i != 0);
    }
    printf("\n");
    return 0;
}

static int valid_user_name(const char *name)
{
    size_t i, len = strlen(name);
    if (!len || len >= 32) return 0;
    for (i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return 0;
        }
    }
    return 1;
}

static int make_user_dir(const char *path, mode_t mode, uid_t uid, gid_t gid)
{
    if (mkdir(path, mode) < 0 && errno != EEXIST) return -1;
    if (chmod(path, mode) < 0) return -1;
    return chown(path, uid, gid);
}

static int write_user_record(const char *name, uid_t uid, gid_t gid)
{
    char path[160], text[USER_TEXT_SIZE];
    int fd, n;
    snprintf(text, sizeof(text),
             "name=%s\nuid=%u\ngid=%u\nhome=/home/%s\nshell=/system/bin/bash\n",
             name, uid, gid, name);
    snprintf(path, sizeof(path), "/config/users/%s/user", name);
    fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
    if (fd < 0) return -1;
    n = write(fd, text, strlen(text));
    close(fd);
    if (n != (int)strlen(text)) return -1;
    return chmod(path, 0644);
}

static int command_adduser(const char *name, const char *uid_text)
{
    struct user_record rec;
    uid_t uid = 1000;
    char path[160];
    int r = 0;

    if (geteuid() != 0 || !valid_user_name(name)) {
        printf("adduser: Permission denied or invalid user name\n");
        return 1;
    }
    if (load_user_name(name, &rec) == 0) {
        printf("adduser: user %s exists\n", name);
        return 1;
    }
    if (uid_text) {
        uid = (uid_t)strtol(uid_text, 0, 10);
    } else {
        while (load_user_uid(uid, &rec) == 0) uid++;
    }
    if (uid > 65535 || load_user_uid(uid, &rec) == 0) {
        printf("adduser: uid %u is invalid or already used\n", uid);
        return 1;
    }

    snprintf(path, sizeof(path), "/config/users/%s", name);
    r |= make_user_dir(path, 0755, 0, 0);
    snprintf(path, sizeof(path), "/home/%s", name);
    r |= make_user_dir(path, 0700, uid, uid);
    snprintf(path, sizeof(path), "/state/users/%s", name);
    r |= make_user_dir(path, 0700, uid, uid);
    snprintf(path, sizeof(path), "/log/users/%s", name);
    r |= make_user_dir(path, 0700, uid, uid);
    snprintf(path, sizeof(path), "/cache/users/%s", name);
    r |= make_user_dir(path, 0700, uid, uid);
    snprintf(path, sizeof(path), "/profiles/users/%s", name);
    r |= make_user_dir(path, 0755, 0, 0);
    snprintf(path, sizeof(path), "/profiles/users/%s/bin", name);
    r |= make_user_dir(path, 0755, 0, 0);
    if (!r) r = write_user_record(name, uid, uid);
    if (r) {
        printf("adduser: failed to create %s: %s\n", name, strerror(errno));
        return 1;
    }
    printf("added user %s (uid=%u, gid=%u)\n", name, uid, uid);
    return 0;
}

static int command_su(const char *name, const char *command)
{
    struct user_record rec;
    pid_t pid;
    int status = 0;

    if (geteuid() != 0) {
        printf("su: Permission denied\n");
        return 1;
    }
    if (load_user_name(name, &rec) < 0) {
        printf("su: unknown user %s\n", name);
        return 1;
    }
    pid = fork();
    if (pid < 0) return 1;
    if (pid == 0) {
        char *shell_argv[5];
        char home_env[128], user_env[96], logname_env[96];
        char shell_env[128], path_env[160];
        char *envp[8];
        gid_t gid = rec.gid;
        if (setgroups(1, &gid) < 0 || setgid(rec.gid) < 0 ||
                setuid(rec.uid) < 0 || chdir(rec.home) < 0) {
            _exit(1);
        }
        snprintf(home_env, sizeof(home_env), "HOME=%s", rec.home);
        snprintf(user_env, sizeof(user_env), "USER=%s", rec.name);
        snprintf(logname_env, sizeof(logname_env), "LOGNAME=%s", rec.name);
        snprintf(shell_env, sizeof(shell_env), "SHELL=%s", rec.shell);
        snprintf(path_env, sizeof(path_env),
                 "PATH=/system/bin:/profiles/users/%s/bin", rec.name);
        envp[0] = home_env;
        envp[1] = user_env;
        envp[2] = logname_env;
        envp[3] = shell_env;
        envp[4] = path_env;
        envp[5] = "TERM=linux";
        envp[6] = 0;
        if (command) {
            shell_argv[0] = "bash";
            shell_argv[1] = "--norc";
            shell_argv[2] = "-c";
            shell_argv[3] = (char *)command;
            shell_argv[4] = 0;
        } else {
            shell_argv[0] = "bash";
            shell_argv[1] = "--norc";
            shell_argv[2] = "-i";
            shell_argv[3] = 0;
        }
        execve(rec.shell, shell_argv, envp);
        _exit(1);
    }
    waitpid(pid, &status, 0);
    return status == 0 ? 0 : 1;
}

static const char *base_name(const char *path)
{
    const char *p = strrchr(path, '/');
    return p ? p + 1 : path;
}

int main(int argc, char **argv)
{
    const char *cmd = base_name(argv[0]);
    int arg = 1;
    if (!strcmp(cmd, "userctl") && argc > 1) {
        cmd = argv[1];
        arg = 2;
    }
    if (!strcmp(cmd, "whoami")) return command_whoami();
    if (!strcmp(cmd, "id")) return command_id();
    if (!strcmp(cmd, "adduser") && argc > arg)
        return command_adduser(argv[arg], argc > arg + 1 ? argv[arg + 1] : 0);
    if ((!strcmp(cmd, "su") || !strcmp(cmd, "login")) && argc > arg) {
        const char *command = 0;
        if (argc > arg + 2 && !strcmp(argv[arg + 1], "-c")) {
            command = argv[arg + 2];
        }
        return command_su(argv[arg], command);
    }
    printf("usage: adduser <name> [uid]; su|login <user> [-c command]; id; whoami\n");
    return 1;
}
