#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"
#include "init.h"

#define SYSCALL_NUM 64

void init_syscall();

int print_str(const char *str);
int fork();
int exec(const char *path);
void exit();

#endif
