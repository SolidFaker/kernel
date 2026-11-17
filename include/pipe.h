#ifndef PIPE_H
#define PIPE_H

#include "common.h"

#define PIPE_CAP 4096

struct pipe;

struct pipe *pipe_create(void);
void pipe_destroy(struct pipe *p);
void pipe_end_open(struct pipe *p, int write_end);
void pipe_end_close(struct pipe *p, int write_end);
int pipe_read(struct pipe *p, u8 *buf, u32 len, int nonblock);
int pipe_write(struct pipe *p, const u8 *buf, u32 len, int nonblock);
int pipe_readable(struct pipe *p);
int pipe_writable(struct pipe *p);
int pipe_hup_read(struct pipe *p);
int pipe_hup_write(struct pipe *p);
u32 pipe_count(struct pipe *p);

#endif
