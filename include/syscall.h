#ifndef SYSCALL_H
#define SYSCALL_H

#include "common.h"
#include "init.h"

#define SYSCALL_NUM 96

void init_syscall();
void syscall_handler(struct trap_frame *frame);

int print_str(const char *str);
int fork();
int execve(const char *path, char *const argv[], char *const envp[]);
int kernel_execve(const char *path, char *const argv[], char *const envp[],
                  struct trap_frame *frame);
void exit(int status);

#endif
