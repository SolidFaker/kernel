#ifndef SIGNAL_H
#define SIGNAL_H

#include "common.h"

struct task_struct;
struct trap_frame;

void signal_init_task(struct task_struct *t);
void signal_fork(struct task_struct *parent, struct task_struct *child);
void signal_exec(struct task_struct *t);
void signal_deliver(struct trap_frame *frame);
int signal_sigreturn(struct trap_frame *frame);
int signal_sigreturn_linux(struct trap_frame *frame, int rt);

int signal_kill(int pid, int sig);
int signal_alarm(int seconds);
void signal_check_alarm(void);

struct task_struct *signal_find_task(u32 pid);

#endif
