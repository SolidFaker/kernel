#ifndef _LIBC_DIRENT_H
#define _LIBC_DIRENT_H

#define NAME_MAX 255

struct dirent {
    unsigned int d_ino;
    char d_name[NAME_MAX + 1];
};

typedef struct __libc_dir DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif
