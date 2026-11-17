#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/tty.h"
#include "elf.h"
#include "errno.h"
#include "fs.h"
#include "jfs.h"
#include "gfx.h"
#include "pipe.h"
#include "posix_abi.h"
#include "mm.h"
#include "page.h"
#include "string.h"
#include "tools.h"
#include "task.h"
#include "init.h"
#include "unistd.h"
#include "vfs.h"
#include "signal.h"
#include "socket.h"
#include "net.h"
#include "drivers/graphic.h"

typedef int (*sysc_func) (struct trap_frame *r);

static void syscall_handler(struct trap_frame *frame);
static void copy_string(char *dst, const char *src, u32 max);


static sysc_func syscalls[SYSCALL_NUM];

/* ------------------------------------------------------------------ */
/* POSIX path handling                                                */
/* ------------------------------------------------------------------ */

/* Turn `path` into a normalised absolute path stored in `out`. */
static int path_normalize(const char *path, char *out, u32 out_size)
{
    char tmp[256];
    char *comp[32];
    u32 i = 0, ncomp = 0, j;
    char *p;
    u32 len = (u32)strlen(path);

    if (path[0] != '/') {
        const char *c = current->cwd;
        while (*c && i + 1 < sizeof(tmp)) {
            tmp[i++] = *c++;
        }
        if (i == 0 || tmp[i - 1] != '/') {
            if (i + 1 < sizeof(tmp)) {
                tmp[i++] = '/';
            }
        }
    }
    if (i + len >= sizeof(tmp)) {
        return -ERANGE;
    }
    memcpy((u8 *)(tmp + i), (const u8 *)path, len);
    i += len;
    tmp[i] = 0;

    p = tmp;
    while (*p) {
        char *start;
        while (*p == '/') {
            p++;
        }
        if (!*p) {
            break;
        }
        start = p;
        while (*p && *p != '/') {
            p++;
        }
        if (*p) {
            *p++ = 0;
        }
        if (!strcmp(start, ".")) {
            continue;
        }
        if (!strcmp(start, "..")) {
            if (ncomp > 0) {
                ncomp--;
            }
            continue;
        }
        if (ncomp < 32) {
            comp[ncomp++] = start;
        }
    }

    i = 0;
    if (ncomp == 0) {
        if (out_size < 2) {
            return -ERANGE;
        }
        out[0] = '/';
        out[1] = 0;
        return 0;
    }
    for (j = 0; j < ncomp; j++) {
        u32 l = (u32)strlen(comp[j]);
        if (i + l + 2 > out_size) {
            return -ERANGE;
        }
        out[i++] = '/';
        memcpy((u8 *)(out + i), (const u8 *)comp[j], l);
        i += l;
    }
    out[i] = 0;
    return 0;
}

static int resolve_path(const char *path, struct fs_node **out)
{
    char abs[256];
    char *p;
    struct fs_node *cur;
    int r;

    if ((u32)path < 0x1000) {
        return -EFAULT;
    }
    r = path_normalize(path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }

    cur = fs_root;
    p = abs;
    while (*p == '/') {
        p++;
    }
    while (*p) {
        char name[128];
        u32 l = 0;

        while (*p && *p != '/' && l < sizeof(name) - 1) {
            name[l++] = *p++;
        }
        name[l] = 0;
        while (*p == '/') {
            p++;
        }
        if (!cur->finddir) {
            return -ENOTDIR;
        }
        cur = cur->finddir(cur, name);
        if (!cur) {
            return -ENOENT;
        }
    }
    *out = cur;
    return 0;
}

/* Split a path into parent directory + final component. */
static int split_path(const char *path, struct fs_node **dir,
                      char *name, u32 name_size)
{
    char abs[256];
    char *p;
    char *last = 0;
    int r;

    r = path_normalize(path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    for (p = abs; *p; p++) {
        if (*p == '/') {
            last = p;
        }
    }
    if (!last || !last[1]) {
        if (abs[0] == '/' && abs[1] == 0) {
            *dir = fs_root;
            name[0] = 0;
            return 0;
        }
        return -EINVAL;
    }
    if ((u32)strlen(last + 1) + 1 > name_size) {
        return -ERANGE;
    }
    {
        const char *b = last + 1;
        u32 i = 0;
        while (b[i]) {
            name[i] = b[i];
            i++;
        }
        name[i] = 0;
    }

    if (last == abs) {
        *dir = fs_root;
        return 0;
    }
    *last = 0;
    r = resolve_path(abs, dir);
    *last = '/';
    return r;
}

static void fill_stat(struct stat *st, struct fs_node *node)
{
    memset((u8 *)st, 0, sizeof(*st));
    if (!node) {
        return;
    }
    st->st_ino = node->inode;
    st->st_mode = ((node->flags & 0x7) == FS_DIRECTORY) ?
        (S_IFDIR | (node->mask & 07777)) :
        (S_IFREG | (node->mask & 07777));
    st->st_nlink = 1;
    st->st_uid = node->uid;
    st->st_gid = node->gid;
    st->st_size = node->length;
    st->st_blksize = 512;
    st->st_blocks = (node->length + 511) / 512;
    st->st_atime = tick / TIMER_HZ;
    st->st_mtime = st->st_atime;
    st->st_ctime = st->st_atime;
}


int fork()
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (1));
    return a;
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    int a;
    asm volatile("int $0x80" : "=a" (a)
                 : "0" (NR_exec), "b" (path), "c" (argv), "d" (envp)
                 : "memory");
    return a;
}

void exit(int status)
{
    asm volatile("int $0x80" : : "a" (NR_exit), "b" (status) : "memory");
}

int print_str(const char *str)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (0), "b"(str));
    return a;
}

// Write len bytes to a user virtual address of an mm being built (not
// yet loaded into CR3), through the physical memory alias.
static void copy_to_userva(struct mm_struct *mm, u32 va, const u8 *src, u32 len)
{
    u32 done = 0;
    while (done < len) {
        u32 pa;
        get_mapping(mm->pdt, va + done, &pa);
        u8 *dst = (u8 *)(pa + PAGE_OFFSET + ((va + done) & ~PAGE_MASK));
        u32 chunk = PAGE_SIZE - ((va + done) & ~PAGE_MASK);
        if (chunk > len - done) {
            chunk = len - done;
        }
        memcpy(dst, src + done, chunk);
        done += chunk;
    }
}

// Load the ELF program stored in `name` into a fresh address space and
// turn the current task into it. Never returns on success: the syscall
// return path irets straight into the program's entry point.
// execve semantics: argv==0 falls back to { path }; envp strings are
// copied onto the new user stack after argv.
static int sys_exec(struct trap_frame *frame)
{
    const char *name = (const char *)frame->ebx;
    char **argv_user = (char **)(frame->ecx);
    char **envp_user = (char **)(frame->edx);
    if ((u32)name < 0x1000) {
        return -1;
    }

    // gather the argv strings while the old address space is still alive
    char argv_buf[256];
    u32 argv_off[8];
    u32 argc = 0;
    u32 str_bytes = 0;
    if ((u32)argv_user >= 0x1000) {
        u32 i;
        for (i = 0; i < 8 && argv_user[i] != 0; i++) {
            const char *s = argv_user[i];
            if ((u32)s < 0x1000) {
                continue;
            }
            u32 len = (u32)strlen(s);
            if (str_bytes + len + 1 > sizeof(argv_buf)) {
                break;
            }
            memcpy((u8 *)(argv_buf + str_bytes), (const u8 *)s, len + 1);
            argv_off[argc] = str_bytes;
            str_bytes += len + 1;
            argc++;
        }
    }
    if (argc == 0) {
        u32 len = (u32)strlen(name);
        if (len > sizeof(argv_buf) - 1) {
            len = sizeof(argv_buf) - 1;
        }
        memcpy((u8 *)argv_buf, (const u8 *)name, len);
        argv_buf[len] = '\0';
        argv_off[0] = 0;
        str_bytes = len + 1;
        argc = 1;
    }

    // gather envp strings too
    char envp_buf[256];
    u32 envp_off[16];
    u32 envc = 0;
    u32 env_bytes = 0;
    if ((u32)envp_user >= 0x1000) {
        u32 i;
        for (i = 0; i < 16 && envp_user[i] != 0; i++) {
            const char *s = envp_user[i];
            u32 len;
            if ((u32)s < 0x1000) {
                continue;
            }
            len = (u32)strlen(s);
            if (env_bytes + len + 1 > sizeof(envp_buf)) {
                break;
            }
            memcpy((u8 *)(envp_buf + env_bytes), (const u8 *)s, len + 1);
            envp_off[envc] = env_bytes;
            env_bytes += len + 1;
            envc++;
        }
    }

    struct fs_node *node = 0;
    if (resolve_path(name, &node) < 0 ||
            node == 0 || (node->flags & 0x7) != FS_FILE) {
        printk("exec: %s not found\n", name);
        return -1;
    }

    // header + program headers of a small statically linked program
    u8 *eh = (u8 *)kmalloc(512);
    if (read_fs(node, 0, 512, eh) == 0) {
        kfree(eh);
        return -1;
    }
    elf_header_t *elf = (elf_header_t *)eh;
    if (elf->magic != ELF_MAGIC ||
            elf->phentsize != sizeof(elf_section_header_t)) {
        printk("exec: %s is not a loadable ELF\n", name);
        kfree(eh);
        return -1;
    }

    struct mm_struct *mm = mm_create();

    // map + fill every PT_LOAD segment; track the heap start
    u32 i;
    u32 brk_max = 0;
    elf_section_header_t *ph = (elf_section_header_t *)(eh + elf->phoff);
    for (i = 0; i < elf->phnum; i++, ph++) {
        if (ph->type != ELF_PROG_LOAD) {
            continue;
        }
        u32 seg_start = ph->vaddr & PAGE_MASK;
        u32 seg_end = ph->vaddr + ph->memsz;
        if (seg_end > brk_max) {
            brk_max = seg_end;
        }
        u32 va;

        // freshly allocated pages hold garbage: zero them via the
        // physical memory alias before copying anything in
        for (va = seg_start; va < seg_end; va += PAGE_SIZE) {
            u32 pa;
            if (!get_mapping(mm->pdt, va, &pa)) {
                pa = page_alloc();
                map(mm->pdt, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
            }
            bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
        }
        // file contents (bss is the zeroed tail of the last page)
        u32 done = 0;
        while (done < ph->filesz) {
            va = ph->vaddr + done;
            u32 pa;
            get_mapping(mm->pdt, va, &pa);
            u8 *dst = (u8 *)(pa + PAGE_OFFSET + (va & ~PAGE_MASK));
            u32 chunk = PAGE_SIZE - (va & ~PAGE_MASK);
            if (chunk > ph->filesz - done) {
                chunk = ph->filesz - done;
            }
            read_fs(node, ph->off + done, chunk, dst);
            done += chunk;
        }
    }

    // user stack
    u32 stack_top = USER_STACK_VA_TOP;
    u32 va;
    for (va = stack_top - USER_STACK_PAGES * PAGE_SIZE; va < stack_top;
            va += PAGE_SIZE) {
        u32 pa = page_alloc();
        map(mm->pdt, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
        bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
    }

    // build the initial stack: [argc][argv...][NULL][envp...][NULL]
    u32 sp = stack_top - (str_bytes + env_bytes);
    copy_to_userva(mm, sp, (const u8 *)argv_buf, str_bytes);
    if (env_bytes) {
        copy_to_userva(mm, sp + str_bytes, (const u8 *)envp_buf, env_bytes);
    }
    u32 str_base = sp;
    u32 env_base = sp + str_bytes;
    u32 nwords = argc + envc + 3 + 12;  /* + auxv pairs */
    u32 words[64];
    sp &= ~3u;
    sp -= 4 * nwords;
    words[0] = argc;
    for (i = 0; i < argc; i++) {
        words[1 + i] = str_base + argv_off[i];
    }
    words[1 + argc] = 0;  // argv terminator
    for (i = 0; i < envc; i++) {
        words[2 + argc + i] = env_base + envp_off[i];
    }
    words[2 + argc + envc] = 0;  // envp terminator
    {
        u32 ai = 2 + argc + envc + 1;
        words[ai++] = 6; words[ai++] = PAGE_SIZE;   /* AT_PAGESZ */
        words[ai++] = 11; words[ai++] = 0;          /* AT_UID */
        words[ai++] = 12; words[ai++] = 0;          /* AT_EUID */
        words[ai++] = 13; words[ai++] = 0;          /* AT_GID */
        words[ai++] = 14; words[ai++] = 0;          /* AT_EGID */
        words[ai++] = 0; words[ai++] = 0;           /* AT_NULL */
    }
    copy_to_userva(mm, sp, (const u8 *)words, 4 * nwords);

    u32 entry = elf->entry;
    u8 osabi = elf->elf[3];
    // report while the old address space (holding `name`) is still alive,
    // and adopt the program name as the task name
    printk("exec %s: entry 0x%x (argc %d)\n", name, entry, argc);
    {
        u32 k;
        for (k = 0; k < sizeof(current->name) - 1 && name[k]; k++) {
            current->name[k] = name[k];
        }
        current->name[k] = '\0';
    }
    kfree(eh);

    // commit: replace the old address space, then rewrite the trap
    // frame so int_ret_stub drops the task into the new program
    switch_pdt(mm->pdt_phys);
    if (current->mm) {
        mm_destroy(current->mm);
    }
    current->mm = mm;
    current->mmap_base = MMAP_BASE;
    signal_exec(current);
    // the user heap starts right after the last segment
    current->brk = (brk_max + PAGE_SIZE - 1) & PAGE_MASK;

    frame->gs = frame->fs = frame->es = frame->ds = __USER_DS;
    frame->ss = __USER_DS;
    frame->cs = __USER_CS;
    frame->eflags = 0x202;          // IF set, bit 1 always one
    current->linux_abi = (osabi == 0xff) ? 1 : 0;
    frame->eip = entry;
    frame->esp = sp;
    frame->useresp = sp;

    return 0;
}

static int sys_fork(struct trap_frame *frame)
{
    if (current->mm != NULL) {
        // real address space: duplicate it, child keeps the same
        // virtual esp/ebp because the pages were copied to the same
        // virtual addresses
        struct task_struct *new_task = alloc_task();
        new_task->mm = mm_copy(current->mm);
        new_task->context->esp -= FRAME_SIZE;
        new_task->frame = (struct trap_frame *)(new_task->context->esp);
        *new_task->frame = *frame;
        new_task->frame->eax = 0;
        new_task->context->eip = (u32)int_ret_stub;
        new_task->state = RUNNABLE;
        return new_task->pid;
    }

    if (current->user_stack == NULL) {
        return -1;  // kernel threads have no user space to duplicate
    }

    struct task_struct *new_task = alloc_task();
    new_task->context->esp -= FRAME_SIZE;
    new_task->frame = (struct trap_frame *)(new_task->context->esp);
    *new_task->frame = *frame;
    new_task->frame->eax = 0;

    // The child gets a private copy of the parent's user stack and keeps
    // running at the same stack offset, so locals and return addresses
    // survive the fork.
    memcpy(new_task->user_stack, current->user_stack, USER_STACK_SIZE);
    u32 parent_top = (u32)current->user_stack + USER_STACK_SIZE;
    if (frame->esp >= (u32)current->user_stack && frame->esp < parent_top) {
        new_task->frame->esp =
            (u32)new_task->user_stack + (frame->esp - (u32)current->user_stack);
    } else {
        new_task->frame->esp = (u32)new_task->user_stack + USER_STACK_SIZE;
    }
    if (frame->ebp >= (u32)current->user_stack && frame->ebp < parent_top) {
        new_task->frame->ebp =
            (u32)new_task->user_stack + (frame->ebp - (u32)current->user_stack);
    }

    new_task->context->eip = (u32)int_ret_stub;
    new_task->state = RUNNABLE;
    return new_task->pid;
}

static int sys_print(struct trap_frame *frame)
{
    char *str = (char *)(frame->ebx);
    // Reject null/near-null pointers. Full user/kernel validation needs real
    // user space: user code currently lives in kernel .rodata, so its string
    // literals legitimately sit above PAGE_OFFSET.
    if ((u32)str < 0x1000) {
        return -1;
    }
#ifdef DEBUG_E9
    {   /* mirror user output to the E9 debug port for headless runs */
        const char *c;
        for (c = str; *c; c++) {
            outb(0xE9, (u8)*c);
        }
    }
#endif
    display_print(str);
    return 0;
}

// Terminate the calling task: record the exit status, free its address
// space, become a zombie and switch away for good. Never returns; the
// task memory is reclaimed by the parent's waitpid().
static int sys_exit(struct trap_frame *frame)
{
    int i;

    current->exit_code = current->linux_abi ? ((frame->ebx & 0xff) << 8)
                                             : frame->ebx;
    if (!current->is_thread && current->parent) {
        current->parent->pending_sig |= 1u << SIGCHLD;
    }
    for (i = 0; i < FD_MAX; i++) {
        if (current->fds[i].used) {
            ofd_put(current->fds[i].ofd);
            memset((u8 *)&current->fds[i], 0, sizeof(current->fds[i]));
        }
    }
    switch_pdt(kernel_pdt_phys());
    if (current->mm) {
        mm_put(current->mm);
        current->mm = NULL;
    }
    current->state = ZOMBIE;
    // zero the slice so schedule() actually rotates away instead of
    // just decrementing it and iretting back into the freed space
    current->time_slice = 0;
    schedule();  // zombies are never scheduled again

    return 0;  // not reached
}

static struct task_struct *task_find_child(struct task_struct *parent,
                                           int pid, int zombie_only)
{
    struct task_list *node = running_task_head;
    do {
        struct task_struct *t = node->task;
        if (!t->is_thread && t->parent == parent &&
                (!zombie_only || t->state == ZOMBIE) &&
                (pid == -1 || (int)t->pid == pid)) {
            return t;
        }
        node = node->next;
    } while (node != running_task_head);
    return 0;
}

// Wait for a child to exit and reap it: pid > 0 selects one child,
// -1 any child. Blocks (sti + hlt) until a matching zombie exists,
// then unlinks it from the schedule ring and frees its memory.
static int sys_waitpid(struct trap_frame *frame)
{
    int pid = (int)frame->ebx;
    u32 *ustatus = (u32 *)(frame->ecx);
    u32 options = frame->edx;

    if (pid == 0 || pid < -1) {
        return -ENOSYS;  // process groups are not supported yet
    }
    if (options & ~(u32)WNOHANG) {
        return -EINVAL;
    }

    struct task_struct *z;
    for (;;) {
        z = task_find_child(current, pid, 1);
        if (z != 0) {
            break;
        }
        if (task_find_child(current, pid, 0) == 0) {
            return -ECHILD;  // no such child
        }
        if (options & WNOHANG) {
            return 0;
        }
        // no zombie yet: idle until the child runs and exits
        sti();
        hlt();
        cli();
    }

    if (ustatus != 0 && (u32)ustatus >= 0x1000) {
        u32 st = z->exit_code;
        if (current->linux_abi && !z->linux_abi) {
            st = (st & 0xff) << 8;
        }
        *ustatus = st;
    }

    // unlink the zombie's node from the ring and free the task
    struct task_list *prev = running_task_head;
    while (prev->next->task != z) {
        prev = prev->next;
    }
    struct task_list *node = prev->next;
    prev->next = node->next;
    u32 reaped = z->pid;
    kfree(z->context);
    kfree(z->kernel_stack);
    kfree(z->user_stack);
    kfree(node);
    kfree(z);
    return (int)reaped;
}

// Grow/shrink the calling task's user heap (brk(0) queries the break).
// Only newly mapped pages are zeroed; already-mapped pages keep their
// contents across partial grows.
static int sys_brk(struct trap_frame *frame)
{
    u32 addr = frame->ebx;
    if (current->mm == 0) {
        return -1;  // kernel threads have no user heap
    }
    u32 old = current->brk;
    if (addr == 0 || addr == old) {
        return (int)old;
    }
    // keep the heap clear of the user stack
    u32 heap_limit = USER_STACK_VA_TOP - USER_STACK_PAGES * PAGE_SIZE;
    if (addr > heap_limit) {
        return -1;
    }

    if (addr > old) {
        u32 va;
        for (va = old & PAGE_MASK; va < addr; va += PAGE_SIZE) {
            u32 pa;
            if (!get_mapping(current->mm->pdt, va, &pa)) {
                pa = page_alloc();
                map(current->mm->pdt, va, pa,
                    PG_PRESENT | PG_WRITE | PG_USER);
                bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
            }
        }
    } else {
        u32 va;
        for (va = (addr + PAGE_SIZE - 1) & PAGE_MASK; va < old;
                va += PAGE_SIZE) {
            u32 pa;
            if (get_mapping(current->mm->pdt, va, &pa)) {
                unmap(current->mm->pdt, va);
                page_free(pa);
            }
        }
    }
    current->brk = addr;
    return (int)addr;
}

// Sleep for {tv_sec, tv_nsec}. The task stays RUNNABLE and idles with
// sti+hlt until the deadline: going SLEEPING here would remove the only
// task that can complete the pending timer interrupt and freeze tick
// (see the NOTE in schedule()).
static int sys_nanosleep(struct trap_frame *frame)
{
    u32 *req = (u32 *)(frame->ebx);
    if ((u32)req < 0x1000) {
        return -1;
    }
    u32 ms = req[0] * 1000 + req[1] / 1000000;
    if (ms == 0) {
        return 0;
    }
    u32 deadline = tick + ms * TIMER_HZ / 1000;
    sti();
    while (tick < deadline) {
        hlt();
    }
    cli();
    return 0;
}

static int sys_time(__UNUSED__ struct trap_frame *frame)
{
    return (int)(tick / TIMER_HZ);
}

// Blocking read on the standard input: sleep (sti + hlt) until at least
// one key is available, then drain up to `count`. Timer preemption during
// the wait is fine. Regular files read through the VFS at f->offset.
static int sys_read(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u8 *ubuf = (u8 *)(frame->ecx);
    u32 count = frame->edx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (u32)ubuf < 0x1000 || count == 0) {
        return -1;
    }

    struct file *f = &current->fds[fd];
    if (f->kind == FD_TTY_IN) {
        int c = kbd_getchar();
        if (c < 0) {
            if (f->ofd && f->ofd->nonblock) {
                return -EAGAIN;
            }
            sti();
            while ((c = kbd_getchar()) < 0) {
                hlt();
            }
            cli();
        }
        u32 n = 0;
        ubuf[n++] = (u8)c;
        while (n < count && (c = kbd_getchar()) >= 0) {
            ubuf[n++] = (u8)c;
        }
        return (int)n;
    }
    if (f->kind == FD_PIPE) {
        return pipe_read(f->ofd->pipe, ubuf, count,
                         f->ofd ? f->ofd->nonblock : 0);
    }
    if (f->kind == FD_SOCKET) {
        return socket_read(f->ofd->sock, ubuf, count,
                           f->ofd ? f->ofd->nonblock : 0);
    }
    if (f->kind == FD_FILE) {
        u32 n = read_fs(f->node, f->ofd->offset, count, ubuf);
        f->ofd->offset += n;
        return (int)n;
    }
    return -EISDIR;  // directories are read with getdents()
}

// Write to the standard output/error; the filesystem is read-only.
static int sys_write(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    const char *ubuf = (const char *)(frame->ecx);
    u32 count = frame->edx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (u32)ubuf < 0x1000) {
        return -1;
    }

    struct file *f = &current->fds[fd];
    if (f->kind == FD_TTY_OUT) {
#ifdef DEBUG_E9
        {   /* mirror output to the E9 debug port for headless runs */
            const char *c;
            for (c = ubuf; c < ubuf + count; c++) {
                outb(0xE9, (u8)*c);
            }
        }
#endif
        display_write(current->tty, ubuf, count);
        return (int)count;
    }
    if (f->kind == FD_PIPE) {
        return pipe_write(f->ofd->pipe, (const u8 *)ubuf, count,
                          f->ofd ? f->ofd->nonblock : 0);
    }
    if (f->kind == FD_SOCKET) {
        return socket_write(f->ofd->sock, (const u8 *)ubuf, count,
                            f->ofd ? f->ofd->nonblock : 0);
    }
    if (f->kind == FD_FILE) {
        u32 off = f->ofd->offset;
        int n;
        if (f->ofd->append) {
            off = f->node->length;
        }
        if (f->ofd->path[0] && count) {
            int lr = jfs_log_write(f->ofd->path, off, (const u8 *)ubuf, count);
            if (lr < 0) {
                return lr;
            }
        }
        n = write_fs(f->node, off, count, (u8 *)ubuf);
        if (n < 0) {
            return n;
        }
        f->ofd->offset = off + (u32)n;
        return n;
    }
    if (f->kind == FD_DIR) {
        return -EISDIR;
    }
    return -EBADF;
}

static int sys_open(struct trap_frame *frame)
{
    const char *name = (const char *)(frame->ebx);
    u32 flags = frame->ecx;
    struct fs_node *parent = 0, *node = 0;
    struct ofd *o;
    char base[128];
    char abspath[160];
    int created = 0;
    u32 acc;
    u32 mode = current->linux_abi ? (frame->edx & 07777) : 0;
    int fd, r;

    if ((u32)name < 0x1000) {
        return -EFAULT;
    }
    r = split_path(name, &parent, base, sizeof(base));
    if (r < 0) {
        return r;
    }
    if (!parent || (parent->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }

    if (base[0]) {
        node = parent->finddir ? parent->finddir(parent, base) : 0;
        if (!node) {
            if (!(flags & O_CREAT)) {
                return -ENOENT;
            }
            if (!parent->create) {
                return -EACCES;
            }
            node = create_fs(parent, base, 0);
            if (!node) {
                return -ENOSPC;
            }
            created = 1;
        if (current->linux_abi) {
            node->mask = mode;
        }
        } else if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return -EEXIST;
        }
    } else {
        if ((flags & O_CREAT) && (flags & O_EXCL)) {
            return -EEXIST;
        }
        node = parent;
    }

    acc = flags & 3;
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if (acc != O_RDONLY) {
            return -EISDIR;
        }
    } else {
        if (acc != O_RDONLY && !node->write) {
            return -EROFS;
        }
        if ((flags & O_TRUNC) && acc != O_RDONLY) {
            if (!created && path_normalize(name, abspath, sizeof(abspath)) == 0) {
                jfs_log(JFS_TRUNCATE, abspath, 0, 0, 0, 0);
            }
            r = truncate_fs(node, 0);
            if (r < 0) {
                return r;
            }
        }
    }

    for (fd = 0; fd < FD_MAX; fd++) {
        if (!current->fds[fd].used) {
            break;
        }
    }
    if (fd == FD_MAX) {
        return -EMFILE;
    }
    o = ofd_alloc();
    if (!o) {
        return -ENOMEM;
    }
    o->append = (flags & O_APPEND) ? 1 : 0;
    o->nonblock = (flags & O_NONBLOCK) ? 1 : 0;
    if (path_normalize(name, abspath, sizeof(abspath)) == 0) {
        copy_string(o->path, abspath, sizeof(o->path));
    }
    if (created && o->path[0]) {
        jfs_log(JFS_CREATE_FILE, o->path, 0, 0x80000000u | (node->mask & 07777), 0, 0);
    }

    current->fds[fd].used = 1;
    current->fds[fd].flags = 0;
    current->fds[fd].kind =
        ((node->flags & 0x7) == FS_DIRECTORY) ? FD_DIR : FD_FILE;
    current->fds[fd].node = node;
    current->fds[fd].ofd = o;
    return fd;
}

static int sys_close(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    ofd_put(current->fds[fd].ofd);
    memset((u8 *)&current->fds[fd], 0, sizeof(current->fds[fd]));
    return 0;
}

static int sys_lseek(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    int offset = (int)frame->ecx;
    u32 whence = frame->edx;

    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    if (current->fds[fd].kind == FD_PIPE) {
        return -ESPIPE;
    }
    if (current->fds[fd].kind == FD_TTY_IN ||
            current->fds[fd].kind == FD_TTY_OUT) {
        return -ESPIPE;
    }
    if (current->fds[fd].kind != FD_FILE) {
        return -EINVAL;
    }

    struct file *f = &current->fds[fd];
    u32 base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = f->ofd->offset; break;
        case SEEK_END: base = f->node->length; break;
        default: return -EINVAL;
    }
    if (offset < 0 && (u32)(-offset) > base) {
        return -EINVAL;
    }
    f->ofd->offset = base + offset;
    return (int)f->ofd->offset;
}

static int sys_fstat(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct stat *ust = (struct stat *)(frame->ecx);
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (u32)ust < 0x1000) {
        return -EBADF;
    }

    struct file *f = &current->fds[fd];
    memset((u8 *)ust, 0, sizeof(*ust));

    if (f->kind == FD_TTY_IN || f->kind == FD_TTY_OUT) {
        ust->st_mode = S_IFCHR | 0600;
        ust->st_rdev = f->kind == FD_TTY_IN ? 0 : 1;
    } else if (f->kind == FD_PIPE) {
        ust->st_mode = S_IFIFO | 0600;
        ust->st_size = pipe_count(f->ofd->pipe);
    } else if (f->kind == FD_DIR) {
        ust->st_mode = S_IFDIR | 0755;
        ust->st_nlink = 1;
    } else {
        fill_stat(ust, f->node);
        ust->st_size = f->node ? f->node->length : 0;
    }
    return 0;
}

static int sys_getpid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->pid;
}

static int sys_getppid(__UNUSED__ struct trap_frame *frame)
{
    return current->parent ? (int)current->parent->pid : 0;
}

static int sys_dup(struct trap_frame *frame)
{
    int oldfd = (int)frame->ebx;
    int fd;

    if (oldfd < 0 || oldfd >= FD_MAX || !current->fds[oldfd].used) {
        return -EBADF;
    }
    for (fd = 0; fd < FD_MAX; fd++) {
        if (!current->fds[fd].used) {
            current->fds[fd] = current->fds[oldfd];
            current->fds[fd].flags = 0;
            ofd_get(current->fds[fd].ofd);
            return fd;
        }
    }
    return -EMFILE;
}

static int sys_dup2(struct trap_frame *frame)
{
    int oldfd = (int)frame->ebx;
    int newfd = (int)frame->ecx;

    if (oldfd < 0 || oldfd >= FD_MAX || !current->fds[oldfd].used) {
        return -EBADF;
    }
    if (newfd < 0 || newfd >= FD_MAX) {
        return -EBADF;
    }
    if (oldfd == newfd) {
        return newfd;
    }

    ofd_get(current->fds[oldfd].ofd);
    if (current->fds[newfd].used) {
        ofd_put(current->fds[newfd].ofd);
    }
    current->fds[newfd] = current->fds[oldfd];
    current->fds[newfd].flags = 0;
    return newfd;
}

static int sys_fcntl(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    int cmd = (int)frame->ecx;
    u32 arg = frame->edx;
    int newfd;

    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }

    switch (cmd) {
    case F_DUPFD:
        for (newfd = (int)arg; newfd < FD_MAX; newfd++) {
            if (!current->fds[newfd].used) {
                current->fds[newfd] = current->fds[fd];
                current->fds[newfd].flags = 0;
                ofd_get(current->fds[newfd].ofd);
                return newfd;
            }
        }
        return -EMFILE;
    case F_GETFD:
        return current->fds[fd].flags & FD_CLOEXEC;
    case F_SETFD:
        current->fds[fd].flags = (u8)(arg & FD_CLOEXEC);
        return 0;
    case F_GETFL: {
        int fl = O_RDONLY;
        struct ofd *o = current->fds[fd].ofd;
        if (current->fds[fd].kind == FD_PIPE && o->pipe_write) {
            fl = O_WRONLY;
        }
        if (o->append) {
            fl |= O_APPEND;
        }
        if (o->nonblock) {
            fl |= O_NONBLOCK;
        }
        return fl;
    }
    case F_SETFL: {
        struct ofd *o = current->fds[fd].ofd;
        if (!o) {
            return 0;
        }
        o->append = (arg & O_APPEND) ? 1 : 0;
        o->nonblock = (arg & O_NONBLOCK) ? 1 : 0;
        return 0;
    }
    default:
        return -EINVAL;
    }
}

static int sys_pipe(struct trap_frame *frame)
{
    int *ufds = (int *)(frame->ebx);
    struct pipe *p;
    struct ofd *ro, *wo;
    int rfd, wfd;

    if ((u32)ufds < 0x1000) {
        return -EFAULT;
    }
    p = pipe_create();
    if (!p) {
        return -ENFILE;
    }
    for (rfd = 0; rfd < FD_MAX; rfd++) {
        if (!current->fds[rfd].used) {
            break;
        }
    }
    if (rfd == FD_MAX) {
        pipe_destroy(p);
        return -EMFILE;
    }
    for (wfd = rfd + 1; wfd < FD_MAX; wfd++) {
        if (!current->fds[wfd].used) {
            break;
        }
    }
    if (wfd == FD_MAX) {
        pipe_destroy(p);
        return -EMFILE;
    }

    ro = ofd_alloc();
    wo = ofd_alloc();
    if (!ro || !wo) {
        if (ro) {
            kfree(ro);
        }
        if (wo) {
            kfree(wo);
        }
        pipe_destroy(p);
        return -ENOMEM;
    }

    ro->pipe = p;
    ro->pipe_write = 0;
    wo->pipe = p;
    wo->pipe_write = 1;
    pipe_end_open(p, 0);
    pipe_end_open(p, 1);

    current->fds[rfd].used = 1;
    current->fds[rfd].kind = FD_PIPE;
    current->fds[rfd].flags = 0;
    current->fds[rfd].node = 0;
    current->fds[rfd].ofd = ro;

    current->fds[wfd].used = 1;
    current->fds[wfd].kind = FD_PIPE;
    current->fds[wfd].flags = 0;
    current->fds[wfd].node = 0;
    current->fds[wfd].ofd = wo;

    ufds[0] = rfd;
    ufds[1] = wfd;
    return 0;
}

static int sys_isatty(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    return (current->fds[fd].kind == FD_TTY_IN ||
            current->fds[fd].kind == FD_TTY_OUT) ? 1 : 0;
}

static void copy_string(char *dst, const char *src, u32 max)
{
    u32 i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static int sys_uname(struct trap_frame *frame)
{
    struct utsname *u = (struct utsname *)(frame->ebx);
    if ((u32)u < 0x1000) {
        return -EFAULT;
    }
    copy_string(u->sysname, "a-mini-kernel", sizeof(u->sysname));
    copy_string(u->nodename, "mini", sizeof(u->nodename));
    copy_string(u->release, "0.2", sizeof(u->release));
    copy_string(u->version, "posix-phase1", sizeof(u->version));
    copy_string(u->machine, "i686", sizeof(u->machine));
    copy_string(u->domainname, "(none)", sizeof(u->domainname));
    return 0;
}

static int sys_clock_gettime(struct trap_frame *frame)
{
    u32 clk = frame->ebx;
    struct timespec *ts = (struct timespec *)(frame->ecx);

    if ((u32)ts < 0x1000) {
        return -EFAULT;
    }
    if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC) {
        return -EINVAL;
    }
    ts->tv_sec = tick / TIMER_HZ;
    ts->tv_nsec = (tick % TIMER_HZ) * (1000000000 / TIMER_HZ);
    return 0;
}

static int sys_gettimeofday(struct trap_frame *frame)
{
    struct timeval *tv = (struct timeval *)(frame->ebx);
    if (tv != 0) {
        if ((u32)tv < 0x1000) {
            return -EFAULT;
        }
        tv->tv_sec = tick / TIMER_HZ;
        tv->tv_usec = (tick % TIMER_HZ) * (1000000 / TIMER_HZ);
    }
    return 0;
}

static int sys_chdir(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    struct fs_node *node;
    char abs[128];
    int r;

    r = resolve_path(path, &node);
    if (r < 0) {
        return r;
    }
    if (!node || (node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    r = path_normalize(path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    copy_string(current->cwd, abs, sizeof(current->cwd));
    return 0;
}

static int sys_getcwd(struct trap_frame *frame)
{
    char *buf = (char *)(frame->ebx);
    u32 size = frame->ecx;
    u32 len = (u32)strlen(current->cwd) + 1;

    if ((u32)buf < 0x1000) {
        return -EFAULT;
    }
    if (size < len) {
        return -ERANGE;
    }
    memcpy((u8 *)buf, (const u8 *)current->cwd, len);
    return 0;
}

static int sys_stat(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    struct stat *ust = (struct stat *)(frame->ecx);
    struct fs_node *node;
    int r;

    if ((u32)ust < 0x1000) {
        return -EFAULT;
    }
    r = resolve_path(path, &node);
    if (r < 0) {
        return r;
    }
    fill_stat(ust, node);
    return 0;
}

static int sys_access(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    u32 mode = frame->ecx;
    struct fs_node *node;
    int r;

    r = resolve_path(path, &node);
    if (r < 0) {
        return r;
    }
    if ((mode & W_OK) && (node->flags & 0x7) == FS_FILE && !node->write) {
        return -EACCES;
    }
    if ((mode & X_OK) && (node->flags & 0x7) != FS_DIRECTORY) {
        return -EACCES;
    }
    return 0;
}

static int mmap_prot_flags(u32 prot)
{
    int f = PG_PRESENT;
    if (prot & PROT_WRITE) {
        f |= PG_WRITE;
    }
    if (prot & (PROT_READ | PROT_EXEC)) {
        f |= PG_USER;
    }
    return f;
}

static int sys_mmap(struct trap_frame *frame)
{
    struct mmap_args *ma = (struct mmap_args *)frame->ebx;
    u32 addr, len, prot, flags, off;
    int fd;
    u32 start, va, mapped_len;
    struct fs_node *node = 0;
    int pflags;

    if ((u32)ma < 0x1000) {
        return -EFAULT;
    }
    addr = ma->addr;
    len = ma->len;
    prot = ma->prot;
    flags = ma->flags;
    fd = ma->fd;
    off = ma->offset;

    if (!current->mm || len == 0) {
        return -EINVAL;
    }
    mapped_len = (len + PAGE_SIZE - 1) & PAGE_MASK;
    if (mapped_len == 0) {
        return -ENOMEM;
    }

    if (flags & MAP_FIXED) {
        if ((addr & ~PAGE_MASK) != 0 || addr < MMAP_BASE) {
            return -EINVAL;
        }
        start = addr;
    } else {
        start = current->mmap_base;
    }
    if ((off & ~PAGE_MASK) != 0) {
        return -EINVAL;
    }
    if (start + mapped_len < start || start + mapped_len > PAGE_OFFSET ||
            start < MMAP_BASE) {
        return -ENOMEM;
    }

    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
                current->fds[fd].kind != FD_FILE) {
            return -EBADF;
        }
        node = current->fds[fd].node;
    }

    pflags = mmap_prot_flags(prot);
    for (va = start; va < start + mapped_len; va += PAGE_SIZE) {
        u32 pa = page_alloc();
        u8 *kp = (u8 *)(pa + PAGE_OFFSET);

        bzero(kp, PAGE_SIZE);
        if (node) {
            read_fs(node, off + (va - start), PAGE_SIZE, kp);
        }
        map(current->mm->pdt, va, pa, pflags);
    }

    if (!(flags & MAP_FIXED)) {
        current->mmap_base = start + mapped_len;
    }
    return (int)start;
}

static int sys_munmap(struct trap_frame *frame)
{
    u32 addr = frame->ebx;
    u32 len = frame->ecx;
    u32 va;

    if (!current->mm || addr < MMAP_BASE || (addr & ~PAGE_MASK)) {
        return -EINVAL;
    }
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;
    if (len == 0 || addr + len < addr || addr + len > PAGE_OFFSET) {
        return -EINVAL;
    }
    for (va = addr; va < addr + len; va += PAGE_SIZE) {
        u32 pa;
        if (get_mapping(current->mm->pdt, va, &pa)) {
            unmap(current->mm->pdt, va);
            page_free(pa);
        }
    }
    return 0;
}

static int sys_mprotect(struct trap_frame *frame)
{
    u32 addr = frame->ebx;
    u32 len = frame->ecx;
    u32 prot = frame->edx;
    u32 va;
    int pflags;

    if (!current->mm || addr < MMAP_BASE || (addr & ~PAGE_MASK)) {
        return -EINVAL;
    }
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;
    if (len == 0 || addr + len < addr || addr + len > PAGE_OFFSET) {
        return -EINVAL;
    }
    pflags = mmap_prot_flags(prot);
    for (va = addr; va < addr + len; va += PAGE_SIZE) {
        u32 pa;
        if (get_mapping(current->mm->pdt, va, &pa)) {
            map(current->mm->pdt, va, pa, pflags);
        }
    }
    return 0;
}

static int sys_msync(__UNUSED__ struct trap_frame *frame)
{
    return 0;
}

static int sys_madvise(__UNUSED__ struct trap_frame *frame)
{
    return 0;
}

static int sys_ioctl(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u32 req = frame->ecx;
    void *arg = (void *)(frame->edx);
    struct file *f;
    struct tty *t;

    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    f = &current->fds[fd];

    if (req == FIOCLEX) {
        f->flags |= FD_CLOEXEC;
        return 0;
    }
    if (req == FIONCLEX) {
        f->flags &= (u8)~FD_CLOEXEC;
        return 0;
    }
    if (req == FIONBIO) {
        return 0;
    }
    if (f->kind != FD_TTY_IN && f->kind != FD_TTY_OUT) {
        return -ENOTTY;
    }
    if ((u32)arg < 0x1000) {
        return -EFAULT;
    }

    t = current->tty;
    switch (req) {
    case TCGETS:
        memcpy((u8 *)arg, (const u8 *)&t->termios, sizeof(t->termios));
        return 0;
    case TCSETS:
    case TCSETSW:
    case TCSETSF:
        memcpy((u8 *)&t->termios, (const u8 *)arg, sizeof(t->termios));
        return 0;
    case TIOCGWINSZ: {
        struct winsize *ws = (struct winsize *)arg;
        ws->ws_row = TTY_ROWS;
        ws->ws_col = TTY_COLS;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }
    case TIOCSWINSZ:
        return 0;
    case TIOCGPGRP:
        *(int *)arg = (int)t->pgrp;
        return 0;
    case TIOCSPGRP:
        t->pgrp = (u32)(*(int *)arg);
        return 0;
    default:
        return -ENOTTY;
    }
}

static int sys_kill(struct trap_frame *frame)
{
    return signal_kill((int)frame->ebx, (int)frame->ecx);
}

static int sys_signal_raise(struct trap_frame *frame)
{
    return signal_kill((int)current->pid, (int)frame->ebx);
}

static int sys_sigaction(struct trap_frame *frame)
{
    int sig = (int)frame->ebx;
    struct sigaction *act = (struct sigaction *)(frame->ecx);
    struct sigaction *old = (struct sigaction *)(frame->edx);

    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
        return -EINVAL;
    }
    if (old != 0) {
        if ((u32)old < 0x1000) {
            return -EFAULT;
        }
        old->sa_handler = (void (*)(int))current->sig_handler[sig];
        old->sa_mask = current->sig_mask[sig];
        old->sa_flags = 0;
        old->sa_restorer = (void (*)(void))current->sig_restorer[sig];
    }
    if (act != 0) {
        u32 h, r;
        if ((u32)act < 0x1000) {
            return -EFAULT;
        }
        h = (u32)act->sa_handler;
        r = (u32)act->sa_restorer;
        if (h != (u32)SIG_DFL && h != (u32)SIG_IGN && h < 0x1000) {
            return -EFAULT;
        }
        if (h != (u32)SIG_DFL && h != (u32)SIG_IGN && r < 0x1000) {
            return -EINVAL;
        }
        current->sig_handler[sig] = h;
        current->sig_mask[sig] = act->sa_mask &
            ~(1u << SIGKILL) & ~(1u << SIGSTOP);
        current->sig_restorer[sig] = r;
    }
    return 0;
}

static int sys_sigprocmask(struct trap_frame *frame)
{
    int how = (int)frame->ebx;
    u32 *set = (u32 *)(frame->ecx);
    u32 *old = (u32 *)(frame->edx);
    u32 mask;

    if (old != 0) {
        if ((u32)old < 0x1000) {
            return -EFAULT;
        }
        *old = current->blocked_sig;
    }
    if (set == 0) {
        return 0;
    }
    if ((u32)set < 0x1000) {
        return -EFAULT;
    }
    mask = *set;
    switch (how) {
    case SIG_BLOCK:
        current->blocked_sig |= mask;
        break;
    case SIG_UNBLOCK:
        current->blocked_sig &= ~mask;
        break;
    case SIG_SETMASK:
        current->blocked_sig = mask;
        break;
    default:
        return -EINVAL;
    }
    current->blocked_sig &= ~(1u << SIGKILL) & ~(1u << SIGSTOP);
    return 0;
}

static int sys_sigreturn(struct trap_frame *frame)
{
    return signal_sigreturn(frame);
}

static int sys_sigpending(struct trap_frame *frame)
{
    u32 *set = (u32 *)(frame->ebx);
    if ((u32)set < 0x1000) {
        return -EFAULT;
    }
    *set = current->pending_sig & current->blocked_sig;
    return 0;
}

static int sys_alarm(struct trap_frame *frame)
{
    return signal_alarm((int)frame->ebx);
}

static int sys_pause(__UNUSED__ struct trap_frame *frame)
{
    while (!(current->pending_sig & ~current->blocked_sig)) {
        sti();
        hlt();
        cli();
    }
    return -EINTR;
}

static int sys_setpgid(struct trap_frame *frame)
{
    u32 pid = frame->ebx;
    u32 pgid = frame->ecx;
    struct task_struct *t;

    if (pid == 0) {
        pid = current->pid;
    }
    if (pgid == 0) {
        pgid = pid;
    }
    t = signal_find_task(pid);
    if (!t) {
        return -ESRCH;
    }
    if (t != current && t->parent != current) {
        return -EPERM;
    }
    t->pgid = pgid;
    return 0;
}

static int sys_getpgid(struct trap_frame *frame)
{
    u32 pid = frame->ebx;
    struct task_struct *t;

    if (pid == 0) {
        return (int)current->pgid;
    }
    t = signal_find_task(pid);
    return t ? (int)t->pgid : -ESRCH;
}

static int sys_getpgrp(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->pgid;
}

static int sys_setsid(__UNUSED__ struct trap_frame *frame)
{
    if (current->pgid == current->pid) {
        return -EPERM;
    }
    current->sid = current->pid;
    current->pgid = current->pid;
    return (int)current->pid;
}

static int sys_tcgetpgrp(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (current->fds[fd].kind != FD_TTY_IN &&
             current->fds[fd].kind != FD_TTY_OUT)) {
        return -ENOTTY;
    }
    return (int)current->tty->pgrp;
}

static int sys_tcsetpgrp(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u32 pgrp = frame->ecx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (current->fds[fd].kind != FD_TTY_IN &&
             current->fds[fd].kind != FD_TTY_OUT)) {
        return -ENOTTY;
    }
    current->tty->pgrp = pgrp;
    return 0;
}

static void close_fd_local(int fd)
{
    if (fd >= 0 && fd < FD_MAX && current->fds[fd].used) {
        ofd_put(current->fds[fd].ofd);
        memset((u8 *)&current->fds[fd], 0, sizeof(current->fds[fd]));
    }
}

static int install_socket_fd(struct socket *s)
{
    int fd;
    struct ofd *o;

    for (fd = 0; fd < FD_MAX; fd++) {
        if (!current->fds[fd].used) {
            break;
        }
    }
    if (fd == FD_MAX) {
        return -EMFILE;
    }
    o = ofd_alloc();
    if (!o) {
        return -ENOMEM;
    }
    o->sock = s;
    current->fds[fd].used = 1;
    current->fds[fd].kind = FD_SOCKET;
    current->fds[fd].flags = 0;
    current->fds[fd].node = 0;
    current->fds[fd].ofd = o;
    return fd;
}

static struct socket *socket_of_fd(int fd)
{
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            current->fds[fd].kind != FD_SOCKET) {
        return 0;
    }
    return current->fds[fd].ofd->sock;
}

static u16 k_htons(u16 v)
{
    return (u16)((v << 8) | (v >> 8));
}

static u32 k_htonl(u32 v)
{
    return ((v & 0xffu) << 24) | ((v & 0xff00u) << 8) |
           ((v >> 8) & 0xff00u) | ((v >> 24) & 0xffu);
}

static int get_inet_addr(void *addr, u32 len, u32 *a, u16 *p)
{
    struct sockaddr_in *sin = (struct sockaddr_in *)addr;

    if ((u32)sin < 0x1000 || len < sizeof(struct sockaddr_in)) {
        return -EFAULT;
    }
    if (sin->sin_family != AF_INET) {
        return -EAFNOSUPPORT;
    }
    *p = k_htons(sin->sin_port);
    *a = k_htonl(sin->sin_addr.s_addr);
    return 0;
}

static int put_inet_addr(void *addr, int *ulen, u32 a, u16 p)
{
    struct sockaddr_in tmp;
    u32 cap;

    if (addr == 0) {
        return 0;
    }
    if ((u32)addr < 0x1000 || (ulen && (u32)ulen < 0x1000)) {
        return -EFAULT;
    }
    memset((u8 *)&tmp, 0, sizeof(tmp));
    tmp.sin_family = AF_INET;
    tmp.sin_port = k_htons(p);
    tmp.sin_addr.s_addr = k_htonl(a);
    cap = ulen ? (u32)*ulen : sizeof(tmp);
    if (cap > sizeof(tmp)) {
        cap = sizeof(tmp);
    }
    memcpy((u8 *)addr, (const u8 *)&tmp, cap);
    if (ulen) {
        *ulen = sizeof(tmp);
    }
    return 0;
}

static int sys_socket(struct trap_frame *frame)
{
    int domain = (int)frame->ebx;
    int type = (int)frame->ecx;
    int proto = (int)frame->edx;
    struct socket *s;

    if (domain != AF_UNIX && domain != AF_INET) {
        return -EAFNOSUPPORT;
    }
    if (proto != 0) {
        return -EPROTONOSUPPORT;
    }
    if (domain == AF_UNIX && type != SOCK_STREAM) {
        return -EPROTONOSUPPORT;
    }
    if (domain == AF_INET && type != SOCK_STREAM && type != SOCK_DGRAM) {
        return -EPROTONOSUPPORT;
    }
    s = socket_new();
    if (!s) {
        return -ENOMEM;
    }
    socket_set_type(s, domain, type);
    {
        int fd = install_socket_fd(s);
        if (fd < 0) {
            socket_put(s);
            return fd;
        }
        return fd;
    }
}

static int get_unix_path(void *addr, u32 len, char *path, u32 max)
{
    struct sockaddr_un *usa = (struct sockaddr_un *)addr;
    u32 n;

    if ((u32)usa < 0x1000 || len < 2) {
        return -EFAULT;
    }
    if (usa->sun_family != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    n = len - 2;
    if (n >= max) {
        n = max - 1;
    }
    memcpy((u8 *)path, (const u8 *)usa->sun_path, n);
    path[n] = 0;
    return 0;
}

static int put_unix_addr(void *addr, int *ulen, const char *path)
{
    struct sockaddr_un tmp;
    u32 cap;

    if (addr == 0) {
        return 0;
    }
    if ((u32)addr < 0x1000 || (ulen && (u32)ulen < 0x1000)) {
        return -EFAULT;
    }
    memset((u8 *)&tmp, 0, sizeof(tmp));
    tmp.sun_family = AF_UNIX;
    copy_string(tmp.sun_path, path, sizeof(tmp.sun_path));
    cap = ulen ? (u32)*ulen : sizeof(tmp);
    if (cap > sizeof(tmp)) {
        cap = sizeof(tmp);
    }
    memcpy((u8 *)addr, (const u8 *)&tmp, cap);
    if (ulen) {
        *ulen = sizeof(tmp);
    }
    return 0;
}

static int sys_bind(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct socket *s = socket_of_fd(fd);
    char path[108];
    int r;

    if (!s) {
        return -ENOTSOCK;
    }
    if (frame->edx >= 2 && (u32)frame->ecx >= 0x1000 &&
            *(u16 *)(frame->ecx) == AF_INET) {
        u32 a;
        u16 p;
        r = get_inet_addr((void *)frame->ecx, frame->edx, &a, &p);
        if (r < 0) {
            return r;
        }
        if (a != INADDR_ANY && a != INADDR_LOOPBACK && a != net_ip()) {
            return -EADDRNOTAVAIL;
        }
        return socket_bind_inet(s, a, p);
    }
    r = get_unix_path((void *)frame->ecx, frame->edx, path, sizeof(path));
    if (r < 0) {
        return r;
    }
    return socket_bind(s, path);
}

static int sys_listen(struct trap_frame *frame)
{
    struct socket *s = socket_of_fd((int)frame->ebx);
    if (!s) {
        return -ENOTSOCK;
    }
    if (socket_type(s) == SOCK_DGRAM) {
        return -EOPNOTSUPP;
    }
    return socket_listen(s, (int)frame->ecx);
}

static int sys_connect(struct trap_frame *frame)
{
    struct socket *s = socket_of_fd((int)frame->ebx);
    char path[108];
    int r;

    if (!s) {
        return -ENOTSOCK;
    }
    if (frame->edx >= 2 && (u32)frame->ecx >= 0x1000 &&
            *(u16 *)(frame->ecx) == AF_INET) {
        u32 a;
        u16 p;
        r = get_inet_addr((void *)frame->ecx, frame->edx, &a, &p);
        if (r < 0) {
            return r;
        }
        if (a != INADDR_LOOPBACK) {
            if (!net_ready()) {
                return -EHOSTUNREACH;
            }
            return socket_connect_real(s, a, p);
        }
        return socket_connect_inet(s, a, p);
    }
    r = get_unix_path((void *)frame->ecx, frame->edx, path, sizeof(path));
    if (r < 0) {
        return r;
    }
    return socket_connect(s, path);
}

static int sys_accept(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct socket *s = socket_of_fd(fd);
    struct socket *ns = 0;
    int nonblock, r, nfd;

    if (!s) {
        return -ENOTSOCK;
    }
    nonblock = current->fds[fd].ofd->nonblock;
    r = socket_accept(s, nonblock, &ns);
    if (r < 0) {
        return r;
    }
    if (!ns) {
        return -ECONNREFUSED;
    }
    nfd = install_socket_fd(ns);
    if (nfd < 0) {
        socket_put(ns);
        return nfd;
    }
    {
        char path[108];
        if (socket_family(ns) == AF_INET) {
            u32 a = socket_peer_addr(ns, 0);
            u16 p;
            socket_peer_addr(ns, &p);
            r = put_inet_addr((void *)frame->ecx, (int *)frame->edx, a, p);
        } else {
            socket_name(ns, path, sizeof(path));
            r = put_unix_addr((void *)frame->ecx, (int *)frame->edx, path);
        }
        if (r < 0) {
            close_fd_local(nfd);
            return r;
        }
    }
    return nfd;
}

static int sys_socketpair(struct trap_frame *frame)
{
    int domain = (int)frame->ebx;
    int type = (int)frame->ecx;
    int proto = (int)frame->edx;
    int *sv = (int *)frame->esi;
    struct socket *a, *b;
    int fa, fb;

    if (domain != AF_UNIX) {
        return -EAFNOSUPPORT;
    }
    if (type != SOCK_STREAM || proto != 0) {
        return -EPROTONOSUPPORT;
    }
    if ((u32)sv < 0x1000) {
        return -EFAULT;
    }
    a = socket_new();
    b = socket_new();
    if (!a || !b) {
        if (a) socket_put(a);
        if (b) socket_put(b);
        return -ENOMEM;
    }
    socket_pair(a, b);
    fa = install_socket_fd(a);
    fb = install_socket_fd(b);
    if (fa < 0 || fb < 0) {
        if (fa >= 0) close_fd_local(fa);
        if (fb >= 0) close_fd_local(fb);
        socket_put(a);
        socket_put(b);
        return fa < 0 ? fa : fb;
    }
    sv[0] = fa;
    sv[1] = fb;
    return 0;
}

static int sys_shutdown(struct trap_frame *frame)
{
    struct socket *s = socket_of_fd((int)frame->ebx);
    if (!s) {
        return -ENOTSOCK;
    }
    return socket_shutdown(s, (int)frame->ecx);
}

static int sys_getsockname(struct trap_frame *frame)
{
    struct socket *s = socket_of_fd((int)frame->ebx);
    char path[108];

    if (!s) {
        return -ENOTSOCK;
    }
    if (socket_family(s) == AF_INET) {
        u16 p;
        u32 a = socket_local_addr(s, &p);
        return put_inet_addr((void *)frame->ecx, (int *)frame->edx, a, p);
    }
    socket_name(s, path, sizeof(path));
    return put_unix_addr((void *)frame->ecx, (int *)frame->edx, path);
}

static int sys_getpeername(struct trap_frame *frame)
{
    struct socket *s = socket_of_fd((int)frame->ebx);
    char path[108];

    if (!s) {
        return -ENOTSOCK;
    }
    if (socket_family(s) == AF_INET) {
        u16 p;
        u32 a = socket_peer_addr(s, &p);
        return put_inet_addr((void *)frame->ecx, (int *)frame->edx, a, p);
    }
    if (socket_peer_name(s, path, sizeof(path)) < 0) {
        return -ENOTCONN;
    }
    return put_unix_addr((void *)frame->ecx, (int *)frame->edx, path);
}

static short poll_revents(struct file *f, short events)
{
    short r = 0;

    switch (f->kind) {
    case FD_TTY_IN:
        if ((events & POLLIN) && kbd_haschar()) {
            r |= POLLIN;
        }
        if (events & POLLOUT) {
            r |= POLLOUT;
        }
        break;
    case FD_TTY_OUT:
        if (events & POLLOUT) {
            r |= POLLOUT;
        }
        break;
    case FD_FILE:
    case FD_DIR:
        if (events & POLLIN) {
            r |= POLLIN;
        }
        if (events & POLLOUT) {
            r |= POLLOUT;
        }
        break;
    case FD_PIPE:
        if (!f->ofd || !f->ofd->pipe) {
            r |= POLLNVAL;
            break;
        }
        if (f->ofd->pipe_write) {
            if ((events & POLLOUT) && pipe_writable(f->ofd->pipe)) {
                r |= POLLOUT;
            }
            if (pipe_hup_write(f->ofd->pipe)) {
                r |= POLLERR | POLLHUP;
            }
        } else {
            if ((events & POLLIN) && pipe_readable(f->ofd->pipe)) {
                r |= POLLIN;
            }
            if (pipe_hup_read(f->ofd->pipe)) {
                r |= POLLHUP;
            }
        }
        break;
    case FD_SOCKET:
        if (!f->ofd || !f->ofd->sock) {
            r |= POLLNVAL;
            break;
        }
        if ((events & POLLIN) && socket_readable(f->ofd->sock)) {
            r |= POLLIN;
        }
        if ((events & POLLOUT) && socket_writable(f->ofd->sock)) {
            r |= POLLOUT;
        }
        if (socket_hup(f->ofd->sock)) {
            r |= POLLHUP;
        }
        break;
    default:
        r |= POLLNVAL;
        break;
    }
    return r;
}

static int sys_poll(struct trap_frame *frame)
{
    struct pollfd *pfds = (struct pollfd *)frame->ebx;
    u32 nfds = frame->ecx;
    int timeout = (int)frame->edx;
    u32 start = tick;
    u32 timeout_ticks = 0;

    if ((u32)pfds < 0x1000) {
        return -EFAULT;
    }
    if (nfds > 64) {
        return -EINVAL;
    }
    if (timeout >= 0) {
        if (timeout > 0 && (u32)timeout < 0x7FFFFFFF / TIMER_HZ) {
            timeout_ticks = (u32)timeout * TIMER_HZ / 1000;
            if (timeout_ticks == 0) {
                timeout_ticks = 1;
            }
        } else if (timeout > 0) {
            timeout_ticks = 0xFFFFFFFF;
        }
    }

    for (;;) {
        u32 i;
        int ready = 0;

        net_poll();
        for (i = 0; i < nfds; i++) {
            struct pollfd *p = &pfds[i];
            int fd = p->fd;
            if (fd < 0) {
                p->revents = 0;
                continue;
            }
            if (fd >= FD_MAX || !current->fds[fd].used) {
                p->revents = POLLNVAL;
                ready++;
                continue;
            }
            p->revents = poll_revents(&current->fds[fd], p->events);
            if (p->revents) {
                ready++;
            }
        }
        if (ready) {
            return ready;
        }
        if (timeout == 0) {
            return 0;
        }
        if (timeout > 0 && tick - start >= timeout_ticks) {
            return 0;
        }
        if (current->pending_sig & ~current->blocked_sig) {
            return -EINTR;
        }
        sti();
        hlt();
        cli();
    }
}

static int sys_sendto(struct trap_frame *frame)
{
    struct sendto_args *a = (struct sendto_args *)frame->ebx;
    struct socket *s;
    u32 addr;
    u16 port;
    int r;

    if ((u32)a < 0x1000 || (u32)a->buf < 0x1000) {
        return -EFAULT;
    }
    s = socket_of_fd(a->fd);
    if (!s) {
        return -ENOTSOCK;
    }
    if (socket_type(s) != SOCK_DGRAM) {
        return -EINVAL;
    }
    r = get_inet_addr((void *)a->addr, a->alen, &addr, &port);
    if (r < 0) {
        return r;
    }
    return socket_sendto(s, (const u8 *)a->buf, a->len, addr, port);
}

static int sys_recvfrom(struct trap_frame *frame)
{
    struct recvfrom_args *a = (struct recvfrom_args *)frame->ebx;
    struct socket *s;
    u32 addr = 0;
    u16 port = 0;
    int r;

    if ((u32)a < 0x1000 || (u32)a->buf < 0x1000) {
        return -EFAULT;
    }
    s = socket_of_fd(a->fd);
    if (!s) {
        return -ENOTSOCK;
    }
    if (socket_type(s) != SOCK_DGRAM) {
        return -EINVAL;
    }
    r = socket_recvfrom(s, (u8 *)a->buf, a->len, &addr, &port,
                        current->fds[a->fd].ofd->nonblock);
    if (r >= 0 && a->addr) {
        int rc = put_inet_addr(a->addr, (int *)a->alen, addr, port);
        if (rc < 0) {
            return rc;
        }
    }
    return r;
}

static int sys_setsockopt(struct trap_frame *frame)
{
    if (!socket_of_fd((int)frame->ebx)) {
        return -ENOTSOCK;
    }
    return 0;
}

static int sys_getsockopt(struct trap_frame *frame)
{
    if (!socket_of_fd((int)frame->ebx)) {
        return -ENOTSOCK;
    }
    return 0;
}

static int sys_thread_create(struct trap_frame *frame)
{
    u32 entry = frame->ebx;
    u32 arg = frame->ecx;
    u32 stack = frame->edx;
    struct task_struct *t;
    u32 sp;

    if (!current->mm || entry < 0x1000 || stack < 0x1000 ||
            stack >= PAGE_OFFSET) {
        return -EINVAL;
    }
    t = alloc_task();
    if (!t) {
        return -ENOMEM;
    }
    t->mm = current->mm;
    mm_get(t->mm);
    t->tgid = current->tgid;
    t->is_thread = 1;
    t->pgid = current->pgid;
    t->sid = current->sid;
    t->brk = current->brk;
    t->mmap_base = current->mmap_base;
    t->tty = current->tty;

    t->context->esp -= FRAME_SIZE;
    t->frame = (struct trap_frame *)(t->context->esp);
    memset((u8 *)t->frame, 0, FRAME_SIZE);
    t->frame->gs = t->frame->fs = t->frame->es = t->frame->ds = __USER_DS;
    t->frame->ss = __USER_DS;
    t->frame->cs = __USER_CS;
    t->frame->eflags = 0x202;
    t->frame->eip = entry;
    sp = (stack - 8) & ~15u;
    ((u32 *)sp)[0] = 0;     /* fake return address */
    ((u32 *)sp)[1] = arg;   /* first argument */
    t->frame->esp = sp;
    t->frame->useresp = sp;
    t->context->eip = (u32)int_ret_stub;
    t->state = RUNNABLE;
    return (int)t->pid;
}

static int sys_thread_exit(struct trap_frame *frame)
{
    int i;

    current->exit_code = frame->ebx;
    for (i = 0; i < FD_MAX; i++) {
        if (current->fds[i].used) {
            ofd_put(current->fds[i].ofd);
            memset((u8 *)&current->fds[i], 0, sizeof(current->fds[i]));
        }
    }
    switch_pdt(kernel_pdt_phys());
    if (current->mm) {
        mm_put(current->mm);
        current->mm = 0;
    }
    current->state = ZOMBIE;
    current->time_slice = 0;
    schedule();
    for (;;) {
        hlt();
    }
    return 0;
}

static int sys_thread_wait(struct trap_frame *frame)
{
    u32 tid = frame->ebx;
    int *ustatus = (int *)(frame->ecx);
    u32 options = frame->edx;
    struct task_struct *t = 0;

    if (ustatus != 0 && (u32)ustatus < 0x1000) {
        return -EFAULT;
    }
    for (;;) {
        t = signal_find_task(tid);
        if (!t || t->tgid != current->tgid || !t->is_thread) {
            return -ESRCH;
        }
        if (t->state == ZOMBIE) {
            break;
        }
        if (options & WNOHANG) {
            return 0;
        }
        sti();
        hlt();
        cli();
    }
    if (ustatus) {
        *ustatus = (int)t->exit_code;
    }
    {
        struct task_list *prev = running_task_head;
        while (prev->next->task != t) {
            prev = prev->next;
        }
        {
            struct task_list *node = prev->next;
            u32 reaped = t->pid;
            prev->next = node->next;
            kfree(t->context);
            kfree(t->kernel_stack);
            kfree(t->user_stack);
            kfree(node);
            kfree(t);
            return (int)reaped;
        }
    }
}

static int sys_futex(struct trap_frame *frame)
{
    u32 op = frame->ebx;
    volatile u32 *addr = (volatile u32 *)(frame->ecx);
    u32 val = frame->edx;

    if ((u32)addr < 0x1000 || (u32)addr >= PAGE_OFFSET) {
        return -EFAULT;
    }
    if (op == 0) {          /* FUTEX_WAIT */
        while (*addr == val) {
            if (current->pending_sig & ~current->blocked_sig) {
                return -EINTR;
            }
            sti();
            hlt();
            cli();
        }
        return 0;
    }
    if (op == 1) {          /* FUTEX_WAKE */
        return 0;
    }
    return -EINVAL;
}

static int sys_ping(struct trap_frame *frame)
{
    u32 ip = frame->ebx;
    u32 timeout = frame->ecx;

    if (!net_ready()) {
        return -ENETDOWN;
    }
    return net_ping(ip, timeout);
}

static int sys_chmod(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    u32 mode = frame->ecx & 07777;
    struct fs_node *node;
    char abs[160];
    int r = resolve_path(path, &node);
    if (r < 0) {
        return r;
    }
    node->mask = mode;
    if (path_normalize(path, abs, sizeof(abs)) == 0) {
        jfs_log(JFS_CHMOD, abs, 0, mode, 0, 0);
    }
    return 0;
}

static int sys_fchmod(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u32 mode = frame->ecx & 07777;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    current->fds[fd].node->mask = mode;
    if (current->fds[fd].ofd && current->fds[fd].ofd->path[0]) {
        jfs_log(JFS_CHMOD, current->fds[fd].ofd->path, 0, mode, 0, 0);
    }
    return 0;
}

static int sys_mkdir(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    struct fs_node *parent;
    char base[128];
    int r;

    r = split_path(path, &parent, base, sizeof(base));
    if (r < 0) {
        return r;
    }
    if (!base[0]) {
        return -EINVAL;
    }
    if (!parent->create) {
        return -EACCES;
    }
    if (finddir_fs(parent, base)) {
        return -EEXIST;
    }
    {
        struct fs_node *node = create_fs(parent, base, 1);
        u32 mode = frame->ecx & 07777;
        if (!node) {
            return -ENOSPC;
        }
        node->mask = mode;
        {
            char abs[160];
            if (path_normalize(path, abs, sizeof(abs)) == 0) {
                jfs_log(JFS_CREATE_DIR, abs, 0,
                        0x80000000u | (node->mask & 07777), 0, 0);
            }
        }
    }
    return 0;
}

static int sys_rmdir(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    struct fs_node *parent, *node;
    char base[128];
    int r;

    r = split_path(path, &parent, base, sizeof(base));
    if (r < 0) {
        return r;
    }
    if (!base[0]) {
        return -EINVAL;
    }
    node = finddir_fs(parent, base);
    if (!node) {
        return -ENOENT;
    }
    if ((node->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }
    {
        char abs[160];
        if (path_normalize(path, abs, sizeof(abs)) == 0) {
            jfs_log(JFS_RMDIR, abs, 0, 0, 0, 0);
        }
    }
    return unlink_fs(parent, base);
}

static int sys_unlink(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    struct fs_node *parent, *node;
    char base[128];
    int r;

    r = split_path(path, &parent, base, sizeof(base));
    if (r < 0) {
        return r;
    }
    if (!base[0]) {
        return -EINVAL;
    }
    node = finddir_fs(parent, base);
    if (!node) {
        return -ENOENT;
    }
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        return -EISDIR;
    }
    {
        char abs[160];
        if (path_normalize(path, abs, sizeof(abs)) == 0) {
            jfs_log(JFS_UNLINK, abs, 0, 0, 0, 0);
        }
    }
    return unlink_fs(parent, base);
}

static int sys_rename(struct trap_frame *frame)
{
    const char *oldpath = (const char *)(frame->ebx);
    const char *newpath = (const char *)(frame->ecx);
    struct fs_node *olddir, *newdir;
    char oldbase[128], newbase[128];
    int r;

    r = split_path(oldpath, &olddir, oldbase, sizeof(oldbase));
    if (r < 0) {
        return r;
    }
    r = split_path(newpath, &newdir, newbase, sizeof(newbase));
    if (r < 0) {
        return r;
    }
    if (!oldbase[0] || !newbase[0]) {
        return -EINVAL;
    }
    if (!finddir_fs(olddir, oldbase)) {
        return -ENOENT;
    }
    {
        char a[160], b[160];
        if (path_normalize(oldpath, a, sizeof(a)) == 0 &&
                path_normalize(newpath, b, sizeof(b)) == 0) {
            jfs_log(JFS_RENAME, a, b, 0, 0, 0);
        }
    }
    return rename_fs(olddir, oldbase, newdir, newbase);
}

static int sys_truncate(struct trap_frame *frame)
{
    const char *path = (const char *)(frame->ebx);
    u32 length = frame->ecx;
    struct fs_node *node;
    int r;

    r = resolve_path(path, &node);
    if (r < 0) {
        return r;
    }
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        return -EISDIR;
    }
    {
        char abs[160];
        if (path_normalize(path, abs, sizeof(abs)) == 0) {
            jfs_log(JFS_TRUNCATE, abs, 0, length, 0, 0);
        }
    }
    return truncate_fs(node, length);
}

static int sys_ftruncate(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u32 length = frame->ecx;

    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    if (current->fds[fd].kind != FD_FILE) {
        return -EINVAL;
    }
    if (current->fds[fd].ofd && current->fds[fd].ofd->path[0]) {
        jfs_log(JFS_TRUNCATE, current->fds[fd].ofd->path, 0, length, 0, 0);
    }
    return truncate_fs(current->fds[fd].node, length);
}

// Read directory entries behind a directory fd: fills ubuf with the
// newline-separated names, starting at the fd's iteration index.
static int sys_getdents(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    char *ubuf = (char *)(frame->ecx);
    u32 max = frame->edx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            current->fds[fd].kind != FD_DIR || (u32)ubuf < 0x1000 || max == 0) {
        return -1;
    }

    struct file *f = &current->fds[fd];
    u32 pos = 0;
    struct dirent *de;
    while ((de = readdir_fs(f->node, f->ofd->offset)) != 0) {
        const char *n = de->name;
        while (*n && pos + 1 < max) {
            ubuf[pos++] = *n++;
        }
        if (pos + 1 < max) {
            ubuf[pos++] = '\n';
        }
        f->ofd->offset++;
    }
    if (pos < max) {
        ubuf[pos] = '\0';
    }
    return pos;
}

// Move the calling task to tty n and show it.
static int sys_chtty(struct trap_frame *frame)
{
    u32 n = frame->ebx;
    if (n >= TTY_NUMBER) {
        return -1;
    }
    current->tty = &tty[n];
    tty_print = &tty[n];
    switch_tty(&tty[n]);
    return 0;
}

static u32 ps_utoa(char *dst, u32 max, u32 v)
{
    char tmp[10];
    u32 i = 0, n = 0;
    do {
        tmp[i++] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (i > 0 && n < max) {
        dst[n++] = tmp[--i];
    }
    return n;
}

// One line per task: "pid state tty prio\n". State letters match the
// task_state enum order: Unused New Embryo Sleeping Runnable Zombie.
static int sys_ps(struct trap_frame *frame)
{
    static const char state_char[] = "UNESRZ";
    char *ubuf = (char *)(frame->ebx);
    u32 max = frame->ecx;
    if ((u32)ubuf < 0x1000 || max == 0) {
        return -1;
    }

    u32 pos = 0;
    struct task_list *node = running_task_head;
    do {
        struct task_struct *t = node->task;
        if (pos + 32 >= max) {
            break;
        }
        pos += ps_utoa(ubuf + pos, max - pos, t->pid);
        ubuf[pos++] = ' ';
        ubuf[pos++] = state_char[t->state > ZOMBIE ? 0 : t->state];
        ubuf[pos++] = ' ';
        pos += ps_utoa(ubuf + pos, max - pos, (u32)(t->tty - tty));
        ubuf[pos++] = ' ';
        pos += ps_utoa(ubuf + pos, max - pos, t->priority);
        ubuf[pos++] = ' ';
        {
            const char *n = t->name;
            while (*n && pos + 1 < max) {
                ubuf[pos++] = *n++;
            }
        }
        ubuf[pos++] = '\n';
        node = node->next;
    } while (node != running_task_head);

    ubuf[pos] = '\0';
    return pos;
}

// Graphics syscall: one entry point for the small VGA mode 13h API.
static int sys_gfx(struct trap_frame *frame)
{
    struct gfx_req *req = (struct gfx_req *)frame->ebx;

    if ((u32)req < 0x1000 || (u32)req >= PAGE_OFFSET) {
        return -1;
    }

    switch (req->op) {
    case GFX_ENTER:
        return gfx_enter();
    case GFX_LEAVE:
        gfx_leave();
        return 0;
    case GFX_CLEAR:
        gfx_clear((u8)req->color);
        return 0;
    case GFX_PIXEL:
        gfx_set_pixel((int)req->x, (int)req->y, (u8)req->color);
        return 0;
    case GFX_LINE:
        gfx_draw_line((int)req->x, (int)req->y,
                      (int)req->x2, (int)req->y2, (u8)req->color);
        return 0;
    case GFX_RECT:
        gfx_draw_rect((int)req->x, (int)req->y,
                      (int)req->w, (int)req->h, (u8)req->color);
        return 0;
    case GFX_FILL_RECT:
        gfx_fill_rect((int)req->x, (int)req->y,
                      (int)req->w, (int)req->h, (u8)req->color);
        return 0;
    case GFX_CIRCLE:
        gfx_draw_circle((int)req->x, (int)req->y,
                        (int)req->w, (u8)req->color);
        return 0;
    case GFX_FILL_CIRCLE:
        gfx_fill_circle((int)req->x, (int)req->y,
                        (int)req->w, (u8)req->color);
        return 0;
    case GFX_CHAR:
        gfx_draw_char((int)req->x, (int)req->y,
                      (char)req->ch, (u8)req->color);
        return 0;
    case GFX_TEXT:
        if ((u32)req->text < 0x1000 || (u32)req->text >= PAGE_OFFSET) {
            return -1;
        }
        gfx_draw_text((int)req->x, (int)req->y, req->text, (u8)req->color);
        return 0;
    case GFX_VSYNC:
        gfx_wait_vsync();
        return 0;
    default:
        return -1;
    }
}

int nosys(struct trap_frame *frame) {
    printk("SYSCALL NO.%d DOSE NOT EXIST.\n", frame->eax);
    return -1;
}

void init_syscall()
{
    // Register our syscall handler.
    register_interrupt_handler (0x80, &syscall_handler);
    syscalls[NR_print]  = &sys_print;   /* legacy, used by task_init */
    syscalls[NR_fork]   = &sys_fork;
    syscalls[NR_exec]   = &sys_exec;
    syscalls[NR_exit]   = &sys_exit;
    syscalls[NR_read]   = &sys_read;
    syscalls[NR_chtty]  = &sys_chtty;
    syscalls[NR_ps]     = &sys_ps;
    syscalls[NR_open]   = &sys_open;
    syscalls[NR_close]  = &sys_close;
    syscalls[NR_write]  = &sys_write;
    syscalls[NR_lseek]  = &sys_lseek;
    syscalls[NR_fstat]  = &sys_fstat;
    syscalls[NR_getpid] = &sys_getpid;
    syscalls[NR_getdents] = &sys_getdents;
    syscalls[NR_waitpid] = &sys_waitpid;
    syscalls[NR_brk] = &sys_brk;
    syscalls[NR_nanosleep] = &sys_nanosleep;
    syscalls[NR_time] = &sys_time;
    syscalls[NR_gfx] = &sys_gfx;
    syscalls[NR_dup] = &sys_dup;
    syscalls[NR_dup2] = &sys_dup2;
    syscalls[NR_fcntl] = &sys_fcntl;
    syscalls[NR_pipe] = &sys_pipe;
    syscalls[NR_getppid] = &sys_getppid;
    syscalls[NR_isatty] = &sys_isatty;
    syscalls[NR_uname] = &sys_uname;
    syscalls[NR_clock_gettime] = &sys_clock_gettime;
    syscalls[NR_gettimeofday] = &sys_gettimeofday;
    syscalls[NR_chdir] = &sys_chdir;
    syscalls[NR_getcwd] = &sys_getcwd;
    syscalls[NR_stat] = &sys_stat;
    syscalls[NR_access] = &sys_access;
    syscalls[NR_mkdir] = &sys_mkdir;
    syscalls[NR_rmdir] = &sys_rmdir;
    syscalls[NR_unlink] = &sys_unlink;
    syscalls[NR_rename] = &sys_rename;
    syscalls[NR_truncate] = &sys_truncate;
    syscalls[NR_ftruncate] = &sys_ftruncate;
    syscalls[NR_chmod] = &sys_chmod;
    syscalls[NR_fchmod] = &sys_fchmod;
    syscalls[NR_mmap] = &sys_mmap;
    syscalls[NR_munmap] = &sys_munmap;
    syscalls[NR_mprotect] = &sys_mprotect;
    syscalls[NR_msync] = &sys_msync;
    syscalls[NR_madvise] = &sys_madvise;
    syscalls[NR_ioctl] = &sys_ioctl;
    syscalls[NR_kill] = &sys_kill;
    syscalls[NR_raise] = &sys_signal_raise;
    syscalls[NR_sigaction] = &sys_sigaction;
    syscalls[NR_sigprocmask] = &sys_sigprocmask;
    syscalls[NR_sigreturn] = &sys_sigreturn;
    syscalls[NR_sigpending] = &sys_sigpending;
    syscalls[NR_alarm] = &sys_alarm;
    syscalls[NR_pause] = &sys_pause;
    syscalls[NR_setpgid] = &sys_setpgid;
    syscalls[NR_getpgid] = &sys_getpgid;
    syscalls[NR_getpgrp] = &sys_getpgrp;
    syscalls[NR_setsid] = &sys_setsid;
    syscalls[NR_tcgetpgrp] = &sys_tcgetpgrp;
    syscalls[NR_tcsetpgrp] = &sys_tcsetpgrp;
    syscalls[NR_socket] = &sys_socket;
    syscalls[NR_bind] = &sys_bind;
    syscalls[NR_listen] = &sys_listen;
    syscalls[NR_accept] = &sys_accept;
    syscalls[NR_connect] = &sys_connect;
    syscalls[NR_socketpair] = &sys_socketpair;
    syscalls[NR_shutdown] = &sys_shutdown;
    syscalls[NR_getsockname] = &sys_getsockname;
    syscalls[NR_getpeername] = &sys_getpeername;
    syscalls[NR_poll] = &sys_poll;
    syscalls[NR_setsockopt] = &sys_setsockopt;
    syscalls[NR_getsockopt] = &sys_getsockopt;
    syscalls[NR_sendto] = &sys_sendto;
    syscalls[NR_recvfrom] = &sys_recvfrom;
    syscalls[NR_thread_create] = &sys_thread_create;
    syscalls[NR_thread_exit] = &sys_thread_exit;
    syscalls[NR_thread_wait] = &sys_thread_wait;
    syscalls[NR_futex] = &sys_futex;
    syscalls[NR_ping] = &sys_ping;
}

/* ------------------------------------------------------------------ */
/* Linux i386 syscall ABI shim (used by musl binaries, marked OSABI 0xff) */
/* ------------------------------------------------------------------ */

struct linux_iovec {
    u32 base;
    u32 len;
};

struct linux_user_desc {
    u32 entry_number;
    u32 base_addr;
    u32 limit;
    u32 flags;
};

struct linux_k_sigaction {
    u32 handler;
    u32 flags;
    u32 restorer;
    u32 mask[2];
};

static u8 tls_used[GDT_ENTRY_NUM];

static int linux_set_thread_area(struct trap_frame *frame)
{
    struct linux_user_desc *u = (struct linux_user_desc *)frame->ebx;
    int entry = -1, i;
    u32 base, limit, flags;
    struct gdt_desc_struct *g;

    if ((u32)u < 0x1000) {
        return -EFAULT;
    }
    if (u->entry_number != 0xffffffffu) {
        entry = (int)u->entry_number;
        if (entry < GDT_TLS_FIRST || entry >= GDT_ENTRY_NUM) {
            return -EINVAL;
        }
    } else {
        for (i = GDT_TLS_FIRST; i < GDT_ENTRY_NUM; i++) {
            if (!tls_used[i]) {
                entry = i;
                break;
            }
        }
        if (entry < 0) {
            return -ENOMEM;
        }
    }
    tls_used[entry] = 1;
    base = u->base_addr;
    limit = u->limit;
    flags = u->flags;
    g = &this_cpu.gdt[entry];
    g->limit0 = (u16)(limit & 0xffff);
    g->limit1 = (u16)((limit >> 16) & 0x0f);
    g->base0 = (u16)(base & 0xffff);
    g->base1 = (u16)((base >> 16) & 0xff);
    g->base2 = (u16)((base >> 24) & 0xff);
    g->type = 0x2;
    g->s = 1;
    g->dpl = 3;
    g->p = 1;
    g->avl = 0;
    g->zero = 0;
    g->db = (flags & 1) ? 1 : 0;
    g->g = (flags & (1u << 4)) ? 1 : 0;
    u->entry_number = (u32)entry;
    return 0;
}

static int linux_mmap2(struct trap_frame *frame)
{
    struct mmap_args ma;
    u32 oldebx = frame->ebx;
    int r;

    ma.addr = frame->ebx;
    ma.len = frame->ecx;
    ma.prot = frame->edx;
    ma.flags = frame->esi;
    ma.fd = (int)frame->edi;
    ma.offset = frame->ebp * PAGE_SIZE;
    frame->ebx = (u32)&ma;
    r = sys_mmap(frame);
    frame->ebx = oldebx;
    return r;
}

static int linux_writev(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct linux_iovec *iov = (struct linux_iovec *)frame->ecx;
    int cnt = (int)frame->edx;
    int total = 0, i;
    u32 oe = frame->ebx, oc = frame->ecx, od = frame->edx;

    if (cnt < 0 || (cnt && (u32)iov < 0x1000)) {
        return -EFAULT;
    }
    for (i = 0; i < cnt; i++) {
        int r;
        if (!iov[i].len) {
            continue;
        }
        frame->ebx = (u32)fd;
        frame->ecx = iov[i].base;
        frame->edx = iov[i].len;
        r = sys_write(frame);
        if (r < 0) {
            frame->ebx = oe; frame->ecx = oc; frame->edx = od;
            return total ? total : r;
        }
        total += r;
        if ((u32)r < iov[i].len) {
            break;
        }
    }
    frame->ebx = oe; frame->ecx = oc; frame->edx = od;
    return total;
}

static int linux_readv(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct linux_iovec *iov = (struct linux_iovec *)frame->ecx;
    int cnt = (int)frame->edx;
    int total = 0, i;
    u32 oe = frame->ebx, oc = frame->ecx, od = frame->edx;

    if (cnt < 0 || (cnt && (u32)iov < 0x1000)) {
        return -EFAULT;
    }
    for (i = 0; i < cnt; i++) {
        int r;
        if (!iov[i].len) {
            continue;
        }
        frame->ebx = (u32)fd;
        frame->ecx = iov[i].base;
        frame->edx = iov[i].len;
        r = sys_read(frame);
        if (r < 0) {
            frame->ebx = oe; frame->ecx = oc; frame->edx = od;
            return total ? total : r;
        }
        total += r;
        if ((u32)r < iov[i].len) {
            break;
        }
    }
    frame->ebx = oe; frame->ecx = oc; frame->edx = od;
    return total;
}

static int linux_sigaction(struct trap_frame *frame)
{
    int sig = (int)frame->ebx;
    struct linux_k_sigaction *act = (struct linux_k_sigaction *)frame->ecx;
    struct linux_k_sigaction *old = (struct linux_k_sigaction *)frame->edx;

    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP) {
        return -EINVAL;
    }
    if (old) {
        if ((u32)old < 0x1000) {
            return -EFAULT;
        }
        old->handler = current->sig_handler[sig];
        old->flags = 0;
        old->restorer = current->sig_restorer[sig];
        old->mask[0] = current->sig_mask[sig];
        old->mask[1] = 0;
    }
    if (act) {
        if ((u32)act < 0x1000) {
            return -EFAULT;
        }
        current->sig_handler[sig] = act->handler;
        current->sig_restorer[sig] = act->restorer;
        current->sig_mask[sig] = act->mask[0] &
            ~(1u << SIGKILL) & ~(1u << SIGSTOP);
    }
    return 0;
}

static int linux_getrandom(struct trap_frame *frame)
{
    u8 *buf = (u8 *)frame->ebx;
    u32 len = frame->ecx;

    if ((u32)buf < 0x1000 && len) {
        return -EFAULT;
    }
    memset(buf, 0, len);
    return (int)len;
}

static int linux_clock_gettime(struct trap_frame *frame, int is64)
{
    u32 clk = frame->ebx;
    u32 *ts = (u32 *)(frame->ecx);

    if ((u32)ts < 0x1000) {
        return -EFAULT;
    }
    if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC) {
        return -EINVAL;
    }
    if (is64) {
        ts[0] = tick / TIMER_HZ;
        ts[1] = 0;
        ts[2] = (tick % TIMER_HZ) * (1000000000 / TIMER_HZ);
        ts[3] = 0;
    } else {
        ts[0] = tick / TIMER_HZ;
        ts[1] = (tick % TIMER_HZ) * (1000000000 / TIMER_HZ);
    }
    return 0;
}

static int linux_prlimit64(struct trap_frame *frame)
{
    u32 res = frame->ecx;
    u32 *old = (u32 *)frame->esi;
    u32 val = (res == 7) ? 16 : (64u * 1024u * 1024u);

    if (old) {
        if ((u32)old < 0x1000) {
            return -EFAULT;
        }
        old[0] = val; old[1] = 0;
        old[2] = val; old[3] = 0;
    }
    return 0;
}

#define LINUX_AT_FDCWD ((u32)-100)
#define LINUX_AT_EMPTY_PATH 0x1000

static int linux_getdents64(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    u8 *buf = (u8 *)frame->ecx;
    u32 count = frame->edx;
    struct file *f;
    u32 pos = 0;
    struct dirent *de;

    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (u32)buf < 0x1000) {
        return -EFAULT;
    }
    f = &current->fds[fd];
    if (f->kind != FD_DIR) {
        return -ENOTDIR;
    }
    while ((de = readdir_fs(f->node, f->ofd->offset)) != 0) {
        u32 nlen = (u32)strlen(de->name);
        u32 reclen = (19 + nlen + 1 + 7) & ~7u;
        struct fs_node *child;
        u8 *p;

        if (pos + reclen > count) {
            break;
        }
        p = buf + pos;
        memset(p, 0, reclen);
        *(u32 *)(p + 0) = de->ino;
        *(u32 *)(p + 4) = 0;
        *(u16 *)(p + 16) = (u16)reclen;
        child = f->node->finddir ? f->node->finddir(f->node, de->name) : 0;
        p[18] = (child && (child->flags & 0x7) == FS_DIRECTORY) ? 4 : 8;
        memcpy(p + 19, (const u8 *)de->name, nlen + 1);
        f->ofd->offset++;
        pos += reclen;
    }
    return (int)pos;
}

static int linux_statx(struct trap_frame *frame)
{
    int dirfd = (int)frame->ebx;
    const char *path = (const char *)frame->ecx;
    u32 flags = frame->edx;
    u32 *stx = (u32 *)frame->edi;
    struct fs_node *node = 0;
    u32 mode, size, ino, now;

    if ((u32)stx < 0x1000) {
        return -EFAULT;
    }
    if ((u32)path < 0x1000) {
        return -EFAULT;
    }
    if (!*path) {
        if (dirfd < 0 || dirfd >= FD_MAX || !current->fds[dirfd].used) {
            return -ENOENT;
        }
        node = current->fds[dirfd].node;
    } else if (dirfd == (int)LINUX_AT_FDCWD || path[0] == '/') {
        if (resolve_path(path, &node) < 0) {
            return -ENOENT;
        }
    } else {
        if (dirfd < 0 || dirfd >= FD_MAX || !current->fds[dirfd].used ||
                !current->fds[dirfd].node->finddir ||
                strchr(path, '/')) {
            return -ENOENT;
        }
        node = current->fds[dirfd].node->finddir(
            current->fds[dirfd].node, (char *)path);
        if (!node) {
            return -ENOENT;
        }
    }
    if (!node) {
        return -ENOENT;
    }
    memset((u8 *)stx, 0, 256);
    mode = ((node->flags & 0x7) == FS_DIRECTORY) ?
        (S_IFDIR | (node->mask & 07777)) :
        (S_IFREG | (node->mask & 07777));
    size = node->length;
    ino = node->inode;
    now = tick / TIMER_HZ;
    stx[0] = 0x7ff;              /* stx_mask */
    stx[1] = 512;                /* stx_blksize */
    stx[4] = 1;                  /* stx_nlink */
    stx[5] = node->uid;
    stx[6] = node->gid;
    *(u16 *)(stx + 7) = (u16)mode;
    stx[8] = ino; stx[9] = 0;    /* stx_ino */
    stx[10] = size; stx[11] = 0; /* stx_size */
    stx[12] = (size + 511) / 512;
    stx[13] = 0;
    stx[14] = 0xffffffffu; stx[15] = 0;
    stx[16] = now; stx[17] = 0; stx[18] = 0;  /* atime */
    stx[24] = now; stx[25] = 0; stx[26] = 0;  /* ctime */
    stx[28] = now; stx[29] = 0; stx[30] = 0;  /* mtime */
    (void)flags;
    return 0;
}

static int linux_open_node(struct fs_node *node, u32 flags)
{
    struct ofd *o;
    int fd;

    if (!node) {
        return -ENOENT;
    }
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if ((flags & 3) != O_RDONLY) {
            return -EISDIR;
        }
    } else if ((flags & 3) != O_RDONLY && !node->write) {
        return -EROFS;
    }
    for (fd = 0; fd < FD_MAX; fd++) {
        if (!current->fds[fd].used) {
            break;
        }
    }
    if (fd == FD_MAX) {
        return -EMFILE;
    }
    o = ofd_alloc();
    if (!o) {
        return -ENOMEM;
    }
    o->append = (flags & O_APPEND) ? 1 : 0;
    o->nonblock = (flags & O_NONBLOCK) ? 1 : 0;
    current->fds[fd].used = 1;
    current->fds[fd].flags = 0;
    current->fds[fd].kind =
        ((node->flags & 0x7) == FS_DIRECTORY) ? FD_DIR : FD_FILE;
    current->fds[fd].node = node;
    current->fds[fd].ofd = o;
    return fd;
}

static int linux_openat(struct trap_frame *frame)
{
    int dirfd = (int)frame->ebx;
    const char *path = (const char *)frame->ecx;
    u32 flags = frame->edx;
    u32 mode = frame->esi;

    if ((u32)path < 0x1000) {
        return -EFAULT;
    }
    if (dirfd == (int)LINUX_AT_FDCWD || path[0] == '/') {
        frame->ebx = (u32)path;
        frame->ecx = flags;
        frame->edx = mode;
        return sys_open(frame);
    }
    {
        struct fs_node *node;
        const char *p = path;

        if (dirfd < 0 || dirfd >= FD_MAX || !current->fds[dirfd].used) {
            return -EBADF;
        }
        node = current->fds[dirfd].node;
        while (*p) {
            char name[128];
            u32 l = 0;

            while (*p == '/') {
                p++;
            }
            if (!*p) {
                break;
            }
            while (*p && *p != '/' && l < sizeof(name) - 1) {
                name[l++] = *p++;
            }
            name[l] = 0;
            if (!strcmp(name, ".")) {
                continue;
            }
            if (!strcmp(name, "..")) {
                node = fs_root;
                continue;
            }
            if (!node || !node->finddir) {
                return -ENOTDIR;
            }
            node = node->finddir(node, name);
            if (!node) {
                return -ENOENT;
            }
        }
        return linux_open_node(node, flags);
    }
}

static int tty_canon_read(struct tty *t, u8 *buf, u32 count);
static int linux_abs_from_dir(int dirfd, const char *path, char *abs,
                              u32 max);
static int linux_mkdirat(struct trap_frame *frame);
static int linux_unlinkat(struct trap_frame *frame);
static int linux_renameat(struct trap_frame *frame);
static int linux_fchdir(struct trap_frame *frame);

static int linux_dispatch(struct trap_frame *frame)
{
    u32 nr = frame->eax;

    switch (nr) {
    case 1: case 252: return sys_exit(frame);
    case 2: return sys_fork(frame);
    case 3: {
        int fd = (int)frame->ebx;
        if (fd >= 0 && fd < FD_MAX && current->fds[fd].used &&
                current->fds[fd].kind == FD_TTY_IN &&
                (u32)frame->ecx >= 0x1000) {
            return tty_canon_read(current->tty, (u8 *)frame->ecx,
                                  frame->edx);
        }
        return sys_read(frame);
    }
    case 4: return sys_write(frame);
    case 5: return sys_open(frame);
    case 6: return sys_close(frame);
    case 10: return sys_unlink(frame);
    case 11: return sys_exec(frame);
    case 12: return sys_chdir(frame);
    case 13: return sys_time(frame);
    case 19: return sys_lseek(frame);
    case 20: return sys_getpid(frame);
    case 24: case 47: case 49: case 50: return 0;
    case 15: return sys_chmod(frame);
    case 94: return sys_fchmod(frame);
    case 306: {
        char abs[160];
        int r = linux_abs_from_dir((int)frame->ebx,
                                   (const char *)frame->ecx,
                                   abs, sizeof(abs));
        if (r < 0) {
            return r;
        }
        frame->ebx = (u32)abs;
        frame->ecx = frame->edx;
        return sys_chmod(frame);
    }
    case 27: return sys_alarm(frame);
    case 33: return sys_access(frame);
    case 37: return sys_kill(frame);
    case 38: return sys_rename(frame);
    case 39: return sys_mkdir(frame);
    case 40: return sys_rmdir(frame);
    case 41: return sys_dup(frame);
    case 42: return sys_pipe(frame);
    case 45: return sys_brk(frame);
    case 54: return sys_ioctl(frame);
    case 57: return sys_setpgid(frame);
    case 65: return sys_getpgrp(frame);
    case 66: return sys_setsid(frame);
    case 55: return sys_fcntl(frame);
    case 60: return 0;          /* umask */
    case 63: return sys_dup2(frame);
    case 64: return sys_getppid(frame);
    case 78: return sys_gettimeofday(frame);
    case 91: return sys_munmap(frame);
    case 92: return sys_truncate(frame);
    case 93: return sys_ftruncate(frame);
    case 114: return sys_waitpid(frame);
    case 120: return frame->ecx == 0 ? sys_fork(frame) : -ENOSYS;
    case 119: return signal_sigreturn_linux(frame, 0);
    case 122: return sys_uname(frame);
    case 125: return sys_mprotect(frame);
    case 132: return sys_getpgid(frame);
    case 133: return linux_fchdir(frame);
    case 145: return linux_readv(frame);
    case 146: return linux_writev(frame);
    case 162: return sys_nanosleep(frame);
    case 168: return sys_poll(frame);
    case 173: return signal_sigreturn_linux(frame, 1);
    case 174: return linux_sigaction(frame);
    case 175: return sys_sigprocmask(frame);
    case 186: {
        u32 *old = (u32 *)frame->ecx;
        if (old) {
            if ((u32)old < 0x1000) {
                return -EFAULT;
            }
            old[0] = 0;
            old[1] = 2;     /* SS_DISABLE */
            old[2] = 0;
        }
        return 0;
    }
    case 190: return sys_fork(frame);
    case 183: {
        int r = sys_getcwd(frame);
        return r < 0 ? r : (int)strlen(current->cwd) + 1;
    }
    case 192: return linux_mmap2(frame);
    case 199: case 200: case 201: case 202: return 0;
    case 220: return linux_getdents64(frame);
    case 221: return sys_fcntl(frame);
    case 296: return linux_mkdirat(frame);
    case 301: return linux_unlinkat(frame);
    case 302: return linux_renameat(frame);
    case 224: return (int)current->pid;
    case 240: {
        u32 op = frame->ebx & 0x7f;
        u32 oe = frame->ebx;
        int r;
        frame->ebx = op;
        r = sys_futex(frame);
        frame->ebx = oe;
        return r;
    }
    case 243: return linux_set_thread_area(frame);
    case 258: return (int)current->pid;
    case 265: return linux_clock_gettime(frame, 0);
    case 270: return signal_kill((int)frame->ebx, (int)frame->edx);
    case 295: return linux_openat(frame);
    case 305: return -ENOENT;   /* no procfs/symlinks */
    case 311: return 0;
    case 340: return linux_prlimit64(frame);
    case 383: return linux_statx(frame);
    case 355: return linux_getrandom(frame);
    case 403: return linux_clock_gettime(frame, 1);
    default:
        return -ENOSYS;
    }
}


#define LINUX_AT_REMOVEDIR 0x200

static int linux_abs_from_dir(int dirfd, const char *path, char *abs, u32 max)
{
    if (!path || (u32)path < 0x1000) {
        return -EFAULT;
    }
    if (path[0] == '/' || dirfd == (int)LINUX_AT_FDCWD) {
        return path_normalize(path, abs, max);
    }
    if (dirfd >= 0 && dirfd < FD_MAX && current->fds[dirfd].used &&
            current->fds[dirfd].ofd &&
            current->fds[dirfd].ofd->path[0]) {
        const char *base = current->fds[dirfd].ofd->path;
        u32 bl = (u32)strlen(base);
        u32 pl = (u32)strlen(path);
        if (bl + pl + 2 > max) {
            return -ERANGE;
        }
        memcpy((u8 *)abs, (const u8 *)base, bl);
        abs[bl] = '/';
        memcpy((u8 *)(abs + bl + 1), (const u8 *)path, pl + 1);
        return 0;
    }
    return -ENOSYS;
}

static int linux_mkdirat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx, (const char *)frame->ecx,
                               abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    frame->ebx = (u32)abs;
    frame->ecx = frame->edx;
    return sys_mkdir(frame);
}

static int linux_unlinkat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx, (const char *)frame->ecx,
                               abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    frame->ebx = (u32)abs;
    if (frame->edx & LINUX_AT_REMOVEDIR) {
        return sys_rmdir(frame);
    }
    return sys_unlink(frame);
}

static int linux_renameat(struct trap_frame *frame)
{
    char a[160], b[160];
    int r = linux_abs_from_dir((int)frame->ebx, (const char *)frame->ecx,
                               a, sizeof(a));
    if (r < 0) {
        return r;
    }
    r = linux_abs_from_dir((int)frame->edx, (const char *)frame->esi,
                           b, sizeof(b));
    if (r < 0) {
        return r;
    }
    frame->ebx = (u32)a;
    frame->ecx = (u32)b;
    return sys_rename(frame);
}

static int linux_fchdir(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            current->fds[fd].kind != FD_DIR ||
            !current->fds[fd].ofd || !current->fds[fd].ofd->path[0]) {
        return -EBADF;
    }
    copy_string(current->cwd, current->fds[fd].ofd->path,
                sizeof(current->cwd));
    return 0;
}

static int tty_canon_read(struct tty *t, u8 *buf, u32 count)
{
    if (!t || !buf || !count) {
        return -EINVAL;
    }
    if (t->in_pos >= t->in_len) {
        t->in_len = 0;
        t->in_pos = 0;
        for (;;) {
            int c = kbd_getchar();
            if (c < 0) {
                sti();
                while ((c = kbd_getchar()) < 0) {
                    hlt();
                }
                cli();
            }
            if (c == '\r') {
                c = '\n';
            }
            if (c == '\b' || c == 0x7f) {
                if (t->in_len > 0) {
                    t->in_len--;
                    display_write(t, "\b \b", 3);
                }
                continue;
            }
            if (c == '\n') {
                display_write(t, "\n", 1);
                if (t->in_len < sizeof(t->inbuf) - 1) {
                    t->inbuf[t->in_len++] = '\n';
                }
                break;
            }
            if (c >= ' ') {
                if (t->in_len < sizeof(t->inbuf) - 1) {
                    char ch = (char)c;
                    t->inbuf[t->in_len++] = ch;
                    display_write(t, &ch, 1);
                }
            }
        }
    }
    {
        u32 n = t->in_len - t->in_pos;
        if (n > count) {
            n = count;
        }
        memcpy(buf, (u8 *)(t->inbuf + t->in_pos), n);
        t->in_pos += n;
        if (t->in_pos >= t->in_len) {
            t->in_pos = 0;
            t->in_len = 0;
        }
        return (int)n;
    }
}

static int linux_syscall_handler(struct trap_frame *frame)
{
    int r = linux_dispatch(frame);

    return r;
}

void syscall_handler(struct trap_frame *frame)
{
    u32 ret;
    sysc_func func = 0;

    if (current->linux_abi) {
        frame->eax = (u32)linux_syscall_handler(frame);
        return;
    }
    // Check if the requested syscall number is valid.
    // The syscall number is found in EAX.
    if (frame->eax >= SYSCALL_NUM) {
        PANIC("bad syscall");
    }
    // Get the required syscall location.
    func = syscalls[frame->eax];
    if (func == NULL){
        func = &nosys;
    }
    // Enter syscall
    ret = (*func)(frame);
    frame->eax = ret;
}
