#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"
#include "init.h"

#define SYSCALL_NUM 64

void init_syscall();

int print_str(const char *str);
int fork();
int execve(const char *path, char *const argv[], char *const envp[]);
void exit(int status);

#endif
