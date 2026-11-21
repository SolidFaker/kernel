#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/tty.h"
#include "drivers/hd.h"
#include "arch/user.h"
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
#if __SIZEOF_POINTER__ == 8
#include "arch/stat.h"
#endif

#ifndef ARCH_NAME
#define ARCH_NAME "i686"
#endif

typedef int (*sysc_func) (struct trap_frame *r);

void syscall_handler(struct trap_frame *frame);
static void copy_string(char *dst, const char *src, u32 max);


static sysc_func syscalls[SYSCALL_NUM];
static int sys_getuid(struct trap_frame *frame);
static int sys_geteuid(struct trap_frame *frame);
static int sys_getgid(struct trap_frame *frame);
static int sys_getegid(struct trap_frame *frame);
static int sys_setuid(struct trap_frame *frame);
static int sys_setgid(struct trap_frame *frame);
static int sys_seteuid(struct trap_frame *frame);
static int sys_setegid(struct trap_frame *frame);
static int sys_getgroups(struct trap_frame *frame);
static int sys_setgroups(struct trap_frame *frame);
static int sys_chown(struct trap_frame *frame);
static int sys_fchown(struct trap_frame *frame);
static int sys_setresuid(struct trap_frame *frame);
static int sys_setresgid(struct trap_frame *frame);

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
    u32 len = (uaddr)strlen(path);

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
        u32 l = (uaddr)strlen(comp[j]);
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

static int path_absolute(const char *path, char *out, u32 out_size)
{
    u32 len = (uaddr)strlen(path);
    if (path[0] == '/') {
        if (len + 1 > out_size) return -ERANGE;
        memcpy((u8 *)out, (const u8 *)path, len + 1);
        return 0;
    }
    {
        u32 cwd_len = (uaddr)strlen(current->cwd);
        if (cwd_len + len + 2 > out_size) return -ERANGE;
        memcpy((u8 *)out, (const u8 *)current->cwd, cwd_len);
        out[cwd_len] = '/';
        memcpy((u8 *)out + cwd_len + 1, (const u8 *)path, len + 1);
    }
    return 0;
}

static int resolve_path(const char *path, struct fs_node **out)
{
    char abs[256];
    int r;

    if ((uaddr)path < 0x1000) {
        return -EFAULT;
    }
    r = path_absolute(path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    return vfs_resolve(abs, out, 1);
}

static int resolve_path_nofollow(const char *path, struct fs_node **out)
{
    char abs[256];
    int r;

    if ((uaddr)path < 0x1000) {
        return -EFAULT;
    }
    r = path_absolute(path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    return vfs_resolve(abs, out, 0);
}

/* Split a path into parent directory + final component. */
static int split_path(const char *path, struct fs_node **dir,
                      char *name, u32 name_size)
{
    char abs[256];
    char *p;
    char *last = 0;
    int r;

    r = path_absolute(path, abs, sizeof(abs));
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
    if ((uaddr)strlen(last + 1) + 1 > name_size) {
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
    return (int)arch_syscall0(NR_fork);
}

int execve(const char *path, char *const argv[], char *const envp[])
{
    return (int)arch_syscall3(NR_exec, (uaddr)path, (uaddr)argv,
                              (uaddr)envp);
}

void exit(int status)
{
    arch_syscall1(NR_exit, (uaddr)status);
}

int print_str(const char *str)
{
    return (int)arch_syscall1(NR_print, (uaddr)str);
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
    if ((uaddr)name < 0x1000) {
        return -1;
    }

    // gather the argv strings while the old address space is still alive
    char argv_buf[2048];
    u32 argv_off[34];
    u32 argc = 0;
    u32 str_bytes = 0;
    if ((uaddr)argv_user >= 0x1000) {
        u32 i;
        for (i = 0; i < 32 && argv_user[i] != 0; i++) {
            const char *s = argv_user[i];
            if ((uaddr)s < 0x1000) {
                continue;
            }
            u32 len = (uaddr)strlen(s);
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
        u32 len = (uaddr)strlen(name);
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
    char envp_buf[2048];
    u32 envp_off[32];
    u32 envc = 0;
    u32 env_bytes = 0;
    if ((uaddr)envp_user >= 0x1000) {
        u32 i;
        for (i = 0; i < 32 && envp_user[i] != 0; i++) {
            const char *s = envp_user[i];
            u32 len;
            if ((uaddr)s < 0x1000) {
                continue;
            }
            len = (uaddr)strlen(s);
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
    {
        char abs[256];
        if (path_normalize(name, abs, sizeof(abs)) == 0 &&
                vfs_noexec_path(abs)) {
            return -EACCES;
        }
    }
    if (fs_permission(node, X_OK) < 0) {
        return -EACCES;
    }

    // header + program headers of a small statically linked program
    u8 *eh = (u8 *)kmalloc(2048);
    bzero(eh, 2048);
    if (read_fs(node, 0, 2048, eh) == 0) {
        kfree(eh);
        return -1;
    }
    if (eh[0] == '#' && eh[1] == '!') {
        char interp[96], iarg[96];
        char *p = (char *)eh + 2;
        char *end = (char *)eh + 512;
        char *nl = p;
        u32 ilen = 0, alen = 0;
        u32 old_off[32], old_argc = argc;
        u32 tail_start, tail_len, prefix, idx = 0, pos = 0, i;

        while (nl < end && *nl != '\n') {
            nl++;
        }
        if (nl == end) {
            kfree(eh);
            return -ENOEXEC;
        }
        *nl = 0;
        if (nl > p && nl[-1] == '\r') {
            nl[-1] = 0;
        }
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (p[ilen] && p[ilen] != ' ' && p[ilen] != '\t') {
            ilen++;
        }
        if (ilen == 0 || ilen >= sizeof(interp)) {
            kfree(eh);
            return -ENOEXEC;
        }
        memcpy((u8 *)interp, (const u8 *)p, ilen);
        interp[ilen] = 0;
        p += ilen;
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        while (p[alen] && alen + 1 < sizeof(iarg)) {
            alen++;
        }
        memcpy((u8 *)iarg, (const u8 *)p, alen);
        iarg[alen] = 0;
        while (alen && (iarg[alen - 1] == ' ' || iarg[alen - 1] == '\t')) {
            iarg[--alen] = 0;
        }

        for (i = 0; i < old_argc; i++) {
            old_off[i] = argv_off[i];
        }
        tail_start = old_argc > 1 ? old_off[1] : str_bytes;
        tail_len = str_bytes - tail_start;
        prefix = ilen + 1 + (alen ? alen + 1 : 0) +
                 (uaddr)strlen(name) + 1;
        if (prefix + tail_len > sizeof(argv_buf)) {
            kfree(eh);
            return -E2BIG;
        }
        for (i = tail_len; i > 0; i--) {
            argv_buf[prefix + i - 1] = argv_buf[tail_start + i - 1];
        }

        argv_off[idx++] = pos;
        memcpy((u8 *)argv_buf + pos, (const u8 *)interp, ilen + 1);
        pos += ilen + 1;
        if (alen) {
            argv_off[idx++] = pos;
            memcpy((u8 *)argv_buf + pos, (const u8 *)iarg, alen + 1);
            pos += alen + 1;
        }
        argv_off[idx++] = pos;
        ilen = (uaddr)strlen(name);
        memcpy((u8 *)argv_buf + pos, (const u8 *)name, ilen + 1);
        pos += ilen + 1;
        for (i = 1; i < old_argc; i++) {
            argv_off[idx++] = prefix + old_off[i] - tail_start;
        }
        argc = idx;
        str_bytes = prefix + tail_len;
        name = argv_buf;

        kfree(eh);
        if (resolve_path(name, &node) < 0 || !node ||
                (node->flags & 0x7) != FS_FILE) {
            return -ENOENT;
        }
        eh = (u8 *)kmalloc(2048);
        bzero(eh, 2048);
        if (read_fs(node, 0, 2048, eh) == 0) {
            kfree(eh);
            return -EIO;
        }
    }
    elf_header_t *elf = (elf_header_t *)eh;
    elf64_header_t *elf64 = (elf64_header_t *)eh;
    u8 elf_class = eh[4];
    if (elf->magic != ELF_MAGIC ||
            (elf_class != 1 && elf_class != 2) ||
            (elf_class == 1 && elf->phentsize != sizeof(elf_section_header_t)) ||
            (elf_class == 2 && elf64->phentsize != sizeof(elf64_program_header_t))) {
        printk("exec: %s is not a loadable ELF\n", name);
        kfree(eh);
        return -ENOEXEC;
    }

    struct mm_struct *mm = mm_create();

    // map + fill every PT_LOAD segment; track the heap start
    u32 i;
    uaddr brk_max = 0;
    u32 phnum = elf_class == 1 ? elf->phnum : elf64->phnum;
    uaddr phoff = elf_class == 1 ? elf->phoff : elf64->phoff;
    elf_section_header_t *ph = (elf_section_header_t *)(eh + phoff);
    elf64_program_header_t *ph64 = (elf64_program_header_t *)(eh + phoff);
    if (phoff + phnum * (elf_class == 1 ? sizeof(*ph) : sizeof(*ph64)) > 2048) {
        kfree(eh);
        return -ENOEXEC;
    }
    for (i = 0; i < phnum; i++, ph++, ph64++) {
        uaddr segva, memsz;
        if (elf_class == 1) {
            if (ph->type != ELF_PROG_LOAD) continue;
            segva = ph->vaddr;
            memsz = ph->memsz;
        } else {
            if (ph64->type != ELF_PROG_LOAD) continue;
            segva = ph64->vaddr;
            memsz = ph64->memsz;
        }
        uaddr seg_start = segva & PAGE_MASK;
        uaddr seg_end = segva + memsz;
        if (seg_end > brk_max) {
            brk_max = seg_end;
        }
        uaddr va;

        // freshly allocated pages hold garbage: zero them via the
        // physical memory alias before copying anything in
        for (va = seg_start; va < seg_end; va += PAGE_SIZE) {
            uaddr pa;
            if (!get_mapping(mm->pdt, va, &pa)) {
                pa = page_alloc();
                map(mm->pdt, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
            }
            bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
        }
    }

    // user stack
    uaddr stack_top = USER_STACK_VA_TOP;
    uaddr va;
    for (va = stack_top - USER_STACK_PAGES * PAGE_SIZE; va < stack_top;
            va += PAGE_SIZE) {
        uaddr pa = page_alloc();
        map(mm->pdt, va, pa, PG_PRESENT | PG_WRITE | PG_USER);
        bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
    }

    // build the initial stack: [argc][argv...][NULL][envp...][NULL]
    uaddr sp = stack_top - (str_bytes + env_bytes);
    uaddr str_base = sp;
    uaddr env_base = sp + str_bytes;
    u32 nwords = argc + envc + 3 + 12;  /* + auxv pairs */
    uaddr words[128];
    sp = (sp - sizeof(reg_t) * nwords) &
         ~(uaddr)(2 * sizeof(reg_t) - 1);
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

    uaddr entry = elf_class == 1 ? elf->entry : elf64->entry;
    u8 osabi = elf->elf[3];
    // Adopt the program name while the old address space is still alive.
    {
        u32 k;
        for (k = 0; k < sizeof(current->name) - 1 && name[k]; k++) {
            current->name[k] = name[k];
        }
        current->name[k] = '\0';
    }
    // commit: replace the old address space, then rewrite the trap
    // frame so int_ret_stub drops the task into the new program
    switch_pdt(mm->pdt_phys);
    if (current->mm) {
        mm_destroy(current->mm);
    }
    current->mm = mm;
    current->tls = 0;
    arch_set_tls_entry(current->tls_entry, 0);
    ph = (elf_section_header_t *)(eh + phoff);
    ph64 = (elf64_program_header_t *)(eh + phoff);
    for (i = 0; i < phnum; i++, ph++, ph64++) {
        if (elf_class == 1) {
            if (ph->type == ELF_PROG_LOAD && ph->filesz)
                read_fs(node, ph->off, ph->filesz, (u8 *)(uaddr)ph->vaddr);
        } else if (ph64->type == ELF_PROG_LOAD && ph64->filesz) {
            read_fs(node, ph64->off, ph64->filesz, (u8 *)(uaddr)ph64->vaddr);
        }
    }
    memcpy((u8 *)str_base, (const u8 *)argv_buf, str_bytes);
    if (env_bytes)
        memcpy((u8 *)env_base, (const u8 *)envp_buf, env_bytes);
    memcpy((u8 *)sp, (const u8 *)words, sizeof(reg_t) * nwords);
    kfree(eh);
    current->mmap_base = MMAP_BASE;
    signal_exec(current);
    for (i = 0; i < FD_MAX; i++) {
        if (current->fds[i].used && (current->fds[i].flags & FD_CLOEXEC)) {
            ofd_put(current->fds[i].ofd);
            memset((u8 *)&current->fds[i], 0, sizeof(current->fds[i]));
        }
    }
    // the user heap starts right after the last segment
    current->brk = (brk_max + PAGE_SIZE - 1) & PAGE_MASK;

    current->linux_abi = (osabi == 0xff) ? 1 : 0;
    arch_prepare_user_frame(frame, entry, sp);

    return 0;
}

int kernel_execve(const char *path, char *const argv[], char *const envp[],
                  struct trap_frame *frame)
{
    cli();
    memset((u8 *)frame, 0, sizeof(*frame));
    frame->ebx = (uaddr)path;
    frame->ecx = (uaddr)argv;
    frame->edx = (uaddr)envp;
    return sys_exec(frame);
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
        new_task->context->svc_sp = new_task->context->esp;
        new_task->frame = (struct trap_frame *)(new_task->context->esp);
        *new_task->frame = *frame;
        arch_set_fork_result(new_task->frame, 0);
        new_task->context->eip = (uaddr)int_ret_stub;
        new_task->state = RUNNABLE;
#ifdef DEBUG_E9
        printk("fork parent pc=%x child frame=%x sp=%x\n",
               frame->eip, new_task->frame, new_task->context->esp);
#endif
        return new_task->pid;
    }

    if (current->user_stack == NULL) {
        return -1;  // kernel threads have no user space to duplicate
    }

    struct task_struct *new_task = alloc_task();
    new_task->context->esp -= FRAME_SIZE;
    new_task->context->svc_sp = new_task->context->esp;
    new_task->frame = (struct trap_frame *)(new_task->context->esp);
    *new_task->frame = *frame;
    arch_set_fork_result(new_task->frame, 0);

    // The child gets a private copy of the parent's user stack and keeps
    // running at the same stack offset, so locals and return addresses
    // survive the fork.
    memcpy(new_task->user_stack, current->user_stack, USER_STACK_SIZE);
    uaddr parent_top = (uaddr)current->user_stack + USER_STACK_SIZE;
    if (frame->esp >= (uaddr)current->user_stack && frame->esp < parent_top) {
        new_task->frame->esp =
            (uaddr)new_task->user_stack + (frame->esp - (uaddr)current->user_stack);
    } else {
        new_task->frame->esp = (uaddr)new_task->user_stack + USER_STACK_SIZE;
    }
    if (frame->ebp >= (uaddr)current->user_stack && frame->ebp < parent_top) {
        new_task->frame->ebp =
            (uaddr)new_task->user_stack + (frame->ebp - (uaddr)current->user_stack);
    }

    new_task->context->eip = (uaddr)int_ret_stub;
    new_task->state = RUNNABLE;
    return new_task->pid;
}

static int sys_print(struct trap_frame *frame)
{
    char *str = (char *)(frame->ebx);
    // Reject null/near-null pointers. Full user/kernel validation needs real
    // user space: user code currently lives in kernel .rodata, so its string
    // literals legitimately sit above PAGE_OFFSET.
    if ((uaddr)str < 0x1000) {
        return -1;
    }
#if defined(DEBUG_E9) && (defined(__i386__) || defined(__x86_64__))
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
#ifdef DEBUG_E9
    printk("exit task=%s status=%d\n", current->name, frame->ebx);
#endif
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
#ifdef DEBUG_E9
    u32 traversed = 0;
#endif
    do {
        struct task_struct *t = node->task;
#ifdef DEBUG_E9
        if (++traversed > 64) {
            printk("task ring loop at %d target=%d\n", current->pid, pid);
            return 0;
        }
#endif
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

#ifdef DEBUG_E9
    printk("waitpid task=%d target=%d options=%x\n", current->pid, pid, options);
    if (current->pid != 3) {
        struct task_list *node = running_task_head;
        do {
            printk("wait sees %d %s state=%d parent=%d\n",
                   node->task->pid, node->task->name, node->task->state,
                   node->task->parent ? node->task->parent->pid : -1);
            node = node->next;
        } while (node != running_task_head);
    }
#endif
    if (pid == 0 || pid < -1) {
        return -ENOSYS;  // process groups are not supported yet
    }
    if (options & ~(uaddr)(WNOHANG | WUNTRACED | 8)) {
        return -EINVAL;
    }

    struct task_struct *z;
#ifdef DEBUG_E9
    u32 wait_started = tick;
#endif
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
#ifdef DEBUG_E9
        if (tick - wait_started > 4000) {
            struct task_list *node = running_task_head;
            printk("wait stuck task=%d target=%d\n", current->pid, pid);
            do {
                printk("task %d %s state=%d\n", node->task->pid,
                       node->task->name, node->task->state);
                node = node->next;
            } while (node != running_task_head);
            wait_started = tick;
        }
#endif
        sti();
        hlt();
        cli();
    }

    if (ustatus != 0 && (uaddr)ustatus >= 0x1000) {
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
    kfree(z->interrupt_stack);
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
    uaddr addr = frame->ebx;
#ifdef DEBUG_E9
    printk("brk request=%x old=%x\n", addr, current->brk);
#endif
    if (current->mm == 0) {
        return -1;  // kernel threads have no user heap
    }
    u32 old = current->brk;
    if (addr == 0 || addr == old) {
        return (int)old;
    }
    // keep the heap clear of the user stack
    uaddr heap_limit = USER_STACK_VA_TOP - USER_STACK_PAGES * PAGE_SIZE;
    if (addr > heap_limit) {
        return -1;
    }

    if (addr > old) {
        uaddr va;
        for (va = old & PAGE_MASK; va < addr; va += PAGE_SIZE) {
            uaddr pa;
            if (!get_mapping(current->mm->pdt, va, &pa)) {
                pa = page_alloc();
                map(current->mm->pdt, va, pa,
                    PG_PRESENT | PG_WRITE | PG_USER);
                bzero((u8 *)(pa + PAGE_OFFSET), PAGE_SIZE);
            }
        }
    } else {
        uaddr va;
        for (va = (addr + PAGE_SIZE - 1) & PAGE_MASK; va < old;
                va += PAGE_SIZE) {
            uaddr pa;
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
    if ((uaddr)req < 0x1000) {
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
            (uaddr)ubuf < 0x1000 || count == 0) {
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

// Write to a tty, pipe, socket or regular file.
static int sys_write(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    const char *ubuf = (const char *)(frame->ecx);
    u32 count = frame->edx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (uaddr)ubuf < 0x1000) {
        return -1;
    }

    struct file *f = &current->fds[fd];
    if (f->kind == FD_TTY_OUT) {
#if defined(DEBUG_E9) && (defined(__i386__) || defined(__x86_64__))
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
        if (f->node->flags & FS_READONLY) {
            return -EROFS;
        }
        if (f->ofd->append || (f->node->flags & FS_APPEND_ONLY)) {
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
        f->ofd->offset = off + (uaddr)n;
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

    if ((uaddr)name < 0x1000) {
        return -EFAULT;
    }
#ifdef DEBUG_E9
    printk("open path=%s flags=%x\n", name, flags);
#endif
    r = split_path(name, &parent, base, sizeof(base));
    if (r < 0) {
        return r;
    }
    if (!parent || (parent->flags & 0x7) != FS_DIRECTORY) {
        return -ENOTDIR;
    }

    if (base[0]) {
        if (flags & O_NOFOLLOW) {
            r = resolve_path_nofollow(name, &node);
        } else {
            r = resolve_path(name, &node);
        }
        if (r < 0 && r != -ENOENT) {
            return r;
        }
        if (node && (flags & O_NOFOLLOW) &&
                (node->flags & 0x7) == FS_SYMLINK) {
            return -ELOOP;
        }
        if (!node) {
            if (!(flags & O_CREAT)) {
                return -ENOENT;
            }
            if (!parent->create) {
                return -EACCES;
            }
            if (parent->flags & FS_READONLY) {
                return -EROFS;
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
    {
        u32 need = acc == O_RDONLY ? R_OK :
            (acc == O_WRONLY ? W_OK : R_OK | W_OK);
        int perm = fs_permission(node, need);
        if (perm < 0) return perm;
    }
    if ((node->flags & 0x7) == FS_DIRECTORY) {
        if (acc != O_RDONLY) {
            return -EISDIR;
        }
    } else {
        if (acc != O_RDONLY && !node->write) {
            return -EROFS;
        }
        if ((flags & O_TRUNC) && acc != O_RDONLY) {
            if (!created &&
                    (node->flags & (FS_READONLY | FS_APPEND_ONLY))) {
                return -EPERM;
            }
            if (!created) {
                if (path_normalize(name, abspath, sizeof(abspath)) == 0) {
                    jfs_log(JFS_TRUNCATE, abspath, 0, 0, 0, 0);
                }
                r = truncate_fs(node, 0);
                if (r < 0) {
                    return r;
                }
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
    current->fds[fd].flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
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
    if (current->fds[fd].kind != FD_FILE &&
            current->fds[fd].kind != FD_DIR) {
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
    if (offset < 0 && (uaddr)(-offset) > base) {
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
            (uaddr)ust < 0x1000) {
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
    case F_DUPFD_CLOEXEC:
        for (newfd = (int)arg; newfd < FD_MAX; newfd++) {
            if (!current->fds[newfd].used) {
                current->fds[newfd] = current->fds[fd];
                current->fds[newfd].flags = cmd == F_DUPFD_CLOEXEC ?
                    FD_CLOEXEC : 0;
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
        if (current->fds[fd].kind == FD_TTY_OUT ||
                (current->fds[fd].kind == FD_PIPE && o && o->pipe_write)) {
            fl = O_WRONLY;
        }
        if (o && o->append) {
            fl |= O_APPEND;
        }
        if (o && o->nonblock) {
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

    if ((uaddr)ufds < 0x1000) {
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
            ofd_put(ro);
        }
        if (wo) {
            ofd_put(wo);
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
    if (current->linux_abi) {
        if (frame->ecx & O_NONBLOCK) {
            ro->nonblock = 1;
            wo->nonblock = 1;
        }
        if (frame->ecx & O_CLOEXEC) {
            current->fds[rfd].flags = FD_CLOEXEC;
            current->fds[wfd].flags = FD_CLOEXEC;
        }
    }

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
    if ((uaddr)u < 0x1000) {
        return -EFAULT;
    }
    copy_string(u->sysname, "a-mini-kernel", sizeof(u->sysname));
    copy_string(u->nodename, "mini", sizeof(u->nodename));
    copy_string(u->release, "0.2", sizeof(u->release));
    copy_string(u->version, "posix-phase1", sizeof(u->version));
    copy_string(u->machine, ARCH_NAME, sizeof(u->machine));
    copy_string(u->domainname, "(none)", sizeof(u->domainname));
    return 0;
}

static int sys_clock_gettime(struct trap_frame *frame)
{
    u32 clk = frame->ebx;
    struct timespec *ts = (struct timespec *)(frame->ecx);

    if ((uaddr)ts < 0x1000) {
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
        if ((uaddr)tv < 0x1000) {
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
    u32 len = (uaddr)strlen(current->cwd) + 1;

    if ((uaddr)buf < 0x1000) {
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

    if ((uaddr)ust < 0x1000) {
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
    if (mode & X_OK) {
        char abs[256];
        if (fs_permission(node, X_OK) < 0) {
            return -EACCES;
        }
        if (path_normalize(path, abs, sizeof(abs)) == 0 &&
                vfs_noexec_path(abs)) {
            return -EACCES;
        }
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
    uaddr addr, len, off;
    u32 prot, flags;
    int fd;
    uaddr start, va, mapped_len;
    struct fs_node *node = 0;
    int pflags;

    if ((uaddr)ma < 0x1000) {
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
        if ((addr & ~PAGE_MASK) != 0 || addr < 0x1000)
            return -EINVAL;
        start = addr;
    } else {
        start = current->mmap_base;
    }
    if ((off & ~PAGE_MASK) != 0)
        return -EINVAL;
    if (start + mapped_len < start || start + mapped_len > USER_ADDR_LIMIT ||
            start < 0x1000 || (!(flags & MAP_FIXED) && start < MMAP_BASE))
        return -ENOMEM;

    if (!(flags & MAP_ANONYMOUS)) {
        if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
                current->fds[fd].kind != FD_FILE) {
            return -EBADF;
        }
        node = current->fds[fd].node;
    }

    pflags = mmap_prot_flags(prot);
    for (va = start; va < start + mapped_len; va += PAGE_SIZE) {
        uaddr pa = page_alloc();
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
    uaddr addr = frame->ebx;
    uaddr len = frame->ecx;
    uaddr va;

    if (!current->mm || addr < MMAP_BASE || (addr & ~PAGE_MASK)) {
#ifdef DEBUG_E9
        printk("munmap addr=%x len=%x mm=%x base=%x\n",
               addr, frame->ecx, current->mm, MMAP_BASE);
#endif
        return -EINVAL;
    }
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;
    if (len == 0 || addr + len < addr || addr + len > USER_ADDR_LIMIT) {
#ifdef DEBUG_E9
        printk("munmap range addr=%x len=%x\n", addr, frame->ecx);
#endif
        return -EINVAL;
    }
    for (va = addr; va < addr + len; va += PAGE_SIZE) {
        uaddr pa;
        if (get_mapping(current->mm->pdt, va, &pa)) {
            unmap(current->mm->pdt, va);
            page_free(pa);
        }
    }
    return 0;
}

static int sys_mprotect(struct trap_frame *frame)
{
    uaddr addr = frame->ebx;
    uaddr len = frame->ecx;
    u32 prot = frame->edx;
    uaddr va;
    int pflags;

    if (!current->mm || addr < MMAP_BASE || (addr & ~PAGE_MASK)) {
        return -EINVAL;
    }
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;
    if (len == 0 || addr + len < addr || addr + len > USER_ADDR_LIMIT) {
        return -EINVAL;
    }
    pflags = mmap_prot_flags(prot);
    for (va = addr; va < addr + len; va += PAGE_SIZE) {
        uaddr pa;
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
        if ((uaddr)arg < 0x1000) {
            return -EFAULT;
        }
        if (f->ofd) {
            f->ofd->nonblock = *(int *)arg != 0;
        }
        return 0;
    }
    if (f->kind != FD_TTY_IN && f->kind != FD_TTY_OUT) {
        return -ENOTTY;
    }
    if ((uaddr)arg < 0x1000) {
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
        t->pgrp = (uaddr)(*(int *)arg);
        return 0;
    default:
        return -ENOTTY;
    }
}

static int sys_kill(struct trap_frame *frame)
{
    int pid = (int)frame->ebx;
    if (pid > 0 && current->euid != 0) {
        struct task_struct *target = signal_find_task((u32)pid);
        if (!target) return -ESRCH;
        if (target->uid != current->uid && target->euid != current->uid) {
            return -EPERM;
        }
    }
    return signal_kill(pid, (int)frame->ecx);
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
        if ((uaddr)old < 0x1000) {
            return -EFAULT;
        }
        old->sa_handler = (void (*)(int))current->sig_handler[sig];
        old->sa_mask = current->sig_mask[sig];
        old->sa_flags = 0;
        old->sa_restorer = (void (*)(void))current->sig_restorer[sig];
    }
    if (act != 0) {
        reg_t h, r;
        if ((uaddr)act < 0x1000) {
            return -EFAULT;
        }
        h = (uaddr)act->sa_handler;
        r = (uaddr)act->sa_restorer;
        if (h != (uaddr)SIG_DFL && h != (uaddr)SIG_IGN && h < 0x1000) {
            return -EFAULT;
        }
        if (h != (uaddr)SIG_DFL && h != (uaddr)SIG_IGN && r < 0x1000) {
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
        if ((uaddr)old < 0x1000) {
            return -EFAULT;
        }
        *old = current->blocked_sig;
    }
    if (set == 0) {
        return 0;
    }
    if ((uaddr)set < 0x1000) {
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
    if ((uaddr)set < 0x1000) {
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

    if ((uaddr)sin < 0x1000 || len < sizeof(struct sockaddr_in)) {
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
    if ((uaddr)addr < 0x1000 || (ulen && (uaddr)ulen < 0x1000)) {
        return -EFAULT;
    }
    memset((u8 *)&tmp, 0, sizeof(tmp));
    tmp.sin_family = AF_INET;
    tmp.sin_port = k_htons(p);
    tmp.sin_addr.s_addr = k_htonl(a);
    cap = ulen ? (uaddr)*ulen : sizeof(tmp);
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

    if ((uaddr)usa < 0x1000 || len < 2) {
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
    if ((uaddr)addr < 0x1000 || (ulen && (uaddr)ulen < 0x1000)) {
        return -EFAULT;
    }
    memset((u8 *)&tmp, 0, sizeof(tmp));
    tmp.sun_family = AF_UNIX;
    copy_string(tmp.sun_path, path, sizeof(tmp.sun_path));
    cap = ulen ? (uaddr)*ulen : sizeof(tmp);
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
    if (frame->edx >= 2 && (uaddr)frame->ecx >= 0x1000 &&
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
    if (frame->edx >= 2 && (uaddr)frame->ecx >= 0x1000 &&
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
    if ((uaddr)sv < 0x1000) {
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
        if ((events & POLLIN) && tty_input_ready(current->tty)) {
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

    if ((uaddr)pfds < 0x1000) {
        return -EFAULT;
    }
    if (nfds > 64) {
        return -EINVAL;
    }
    if (timeout >= 0) {
        if (timeout > 0 && (uaddr)timeout < 0x7FFFFFFF / TIMER_HZ) {
            timeout_ticks = (uaddr)timeout * TIMER_HZ / 1000;
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

    if ((uaddr)a < 0x1000 || (uaddr)a->buf < 0x1000) {
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

    if ((uaddr)a < 0x1000 || (uaddr)a->buf < 0x1000) {
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
    uaddr entry = frame->ebx;
    uaddr arg = frame->ecx;
    uaddr stack = frame->edx;
    struct task_struct *t;
    if (!current->mm || entry < 0x1000 || stack < 0x1000 ||
            stack >= USER_ADDR_LIMIT) {
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
    t->context->svc_sp = t->context->esp;
    t->frame = (struct trap_frame *)(t->context->esp);
    memset((u8 *)t->frame, 0, FRAME_SIZE);
    t->frame->gs = t->frame->fs = t->frame->es = t->frame->ds = __USER_DS;
    t->frame->ss = __USER_DS;
    t->frame->cs = __USER_CS;
    t->frame->eflags = 0x202;
    arch_prepare_thread_frame(t->frame, entry, arg, stack);
    t->context->eip = (uaddr)int_ret_stub;
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

    if (ustatus != 0 && (uaddr)ustatus < 0x1000) {
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
            kfree(t->interrupt_stack);
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

    if ((uaddr)addr < 0x1000 || (uaddr)addr >= USER_ADDR_LIMIT) {
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
    if (current->euid != 0 && current->euid != node->uid) {
        return -EPERM;
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
    if (current->euid != 0 && current->euid != current->fds[fd].node->uid) {
        return -EPERM;
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
    if (parent->flags & FS_READONLY) {
        return -EROFS;
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
            current->fds[fd].kind != FD_DIR || (uaddr)ubuf < 0x1000 || max == 0) {
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
    if ((uaddr)ubuf < 0x1000 || max == 0) {
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
        pos += ps_utoa(ubuf + pos, max - pos, (uaddr)(t->tty - tty));
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

    if ((uaddr)req < 0x1000 || (uaddr)req >= USER_ADDR_LIMIT) {
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
        if ((uaddr)req->text < 0x1000 || (uaddr)req->text >= USER_ADDR_LIMIT) {
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
    syscalls[NR_getuid] = &sys_getuid;
    syscalls[NR_geteuid] = &sys_geteuid;
    syscalls[NR_getgid] = &sys_getgid;
    syscalls[NR_getegid] = &sys_getegid;
    syscalls[NR_setuid] = &sys_setuid;
    syscalls[NR_setgid] = &sys_setgid;
    syscalls[NR_seteuid] = &sys_seteuid;
    syscalls[NR_setegid] = &sys_setegid;
    syscalls[NR_getgroups] = &sys_getgroups;
    syscalls[NR_setgroups] = &sys_setgroups;
    syscalls[NR_chown] = &sys_chown;
    syscalls[NR_fchown] = &sys_fchown;
    syscalls[NR_setresuid] = &sys_setresuid;
    syscalls[NR_setresgid] = &sys_setresgid;
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
#if __SIZEOF_POINTER__ == 8
    uaddr base;
    uaddr len;
#else
    u32 base;
    u32 len;
#endif
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

#if !defined(__aarch64__)
static u8 tls_used[GDT_ENTRY_NUM];
#endif

static int linux_set_thread_area(struct trap_frame *frame)
{
#if defined(__aarch64__)
    (void)frame;
    return -ENOSYS;
#else
    struct linux_user_desc *u = (struct linux_user_desc *)frame->ebx;
    int entry = -1, i;
    u32 base, limit, flags;
    struct gdt_desc_struct *g;

    if ((uaddr)u < 0x1000) {
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
    current->tls_entry = entry;
    current->tls = u->base_addr;
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
    u->entry_number = (uaddr)entry;
    return 0;
#endif
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
#if __SIZEOF_POINTER__ == 8
    ma.offset = frame->ebp;
#else
    ma.offset = frame->ebp * PAGE_SIZE;
#endif
    frame->ebx = (uaddr)&ma;
    r = sys_mmap(frame);
    frame->ebx = oldebx;
#ifdef DEBUG_E9
    if (r < 0)
        printk("mmap failed addr=%x len=%x flags=%x off=%x ret=%d\n",
               ma.addr, ma.len, ma.flags, ma.offset, r);
#endif
    return r;
}

static int linux_writev(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct linux_iovec *iov = (struct linux_iovec *)frame->ecx;
    int cnt = (int)frame->edx;
    int total = 0, i;
    u32 oe = frame->ebx, oc = frame->ecx, od = frame->edx;

    if (cnt < 0 || (cnt && (uaddr)iov < 0x1000)) {
        return -EFAULT;
    }
    for (i = 0; i < cnt; i++) {
        int r;
        if (!iov[i].len) {
            continue;
        }
        frame->ebx = (uaddr)fd;
        frame->ecx = iov[i].base;
        frame->edx = iov[i].len;
        r = sys_write(frame);
        if (r < 0) {
            frame->ebx = oe; frame->ecx = oc; frame->edx = od;
            return total ? total : r;
        }
        total += r;
        if ((uaddr)r < iov[i].len) {
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

    if (cnt < 0 || (cnt && (uaddr)iov < 0x1000)) {
        return -EFAULT;
    }
    for (i = 0; i < cnt; i++) {
        int r;
        if (!iov[i].len) {
            continue;
        }
        frame->ebx = (uaddr)fd;
        frame->ecx = iov[i].base;
        frame->edx = iov[i].len;
        r = sys_read(frame);
        if (r < 0) {
            frame->ebx = oe; frame->ecx = oc; frame->edx = od;
            return total ? total : r;
        }
        total += r;
        if ((uaddr)r < iov[i].len) {
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
        if ((uaddr)old < 0x1000) {
            return -EFAULT;
        }
        old->handler = current->sig_handler[sig];
        old->flags = 0;
        old->restorer = current->sig_restorer[sig];
        old->mask[0] = current->sig_mask[sig];
        old->mask[1] = 0;
    }
    if (act) {
        if ((uaddr)act < 0x1000) {
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

    if ((uaddr)buf < 0x1000 && len) {
        return -EFAULT;
    }
    memset(buf, 0, len);
    return (int)len;
}

static int linux_clock_gettime(struct trap_frame *frame, int is64)
{
    u32 clk = frame->ebx;
    u32 *ts = (u32 *)(frame->ecx);

    if ((uaddr)ts < 0x1000) {
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
        if ((uaddr)old < 0x1000) {
            return -EFAULT;
        }
        old[0] = val; old[1] = 0;
        old[2] = val; old[3] = 0;
    }
    return 0;
}

#define LINUX_AT_FDCWD ((uaddr)-100)
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
            (uaddr)buf < 0x1000) {
        return -EFAULT;
    }
    f = &current->fds[fd];
    if (f->kind != FD_DIR) {
        return -ENOTDIR;
    }
    while ((de = readdir_fs(f->node, f->ofd->offset)) != 0) {
        u32 nlen = (uaddr)strlen(de->name);
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
    struct file *f = 0;
    u32 mode, size, ino, now;

    if ((uaddr)stx < 0x1000) {
        return -EFAULT;
    }
    if ((uaddr)path < 0x1000) {
        return -EFAULT;
    }
    if (!*path) {
        if (dirfd < 0 || dirfd >= FD_MAX || !current->fds[dirfd].used) {
            return -EBADF;
        }
        f = &current->fds[dirfd];
        node = f->node;
    } else if (dirfd == (int)LINUX_AT_FDCWD || path[0] == '/') {
        if (resolve_path(path, &node) < 0) {
            return -ENOENT;
        }
    } else {
        if (dirfd < 0 || dirfd >= FD_MAX || !current->fds[dirfd].used ||
                !current->fds[dirfd].node ||
                !current->fds[dirfd].node->finddir || strchr(path, '/')) {
            return -ENOENT;
        }
        node = current->fds[dirfd].node->finddir(
            current->fds[dirfd].node, (char *)path);
        if (!node) {
            return -ENOENT;
        }
    }
    if (!node && !f) {
        return -ENOENT;
    }
    memset((u8 *)stx, 0, 256);
    if (node) {
        mode = ((node->flags & 0x7) == FS_DIRECTORY) ?
            (S_IFDIR | (node->mask & 07777)) :
            (S_IFREG | (node->mask & 07777));
        size = node->length;
        ino = node->inode;
    } else {
        ino = 0;
        size = 0;
        switch (f->kind) {
        case FD_TTY_IN:
        case FD_TTY_OUT:
            mode = S_IFCHR | 0600;
            break;
        case FD_PIPE:
            mode = S_IFIFO | 0600;
            size = pipe_count(f->ofd->pipe);
            break;
        case FD_SOCKET:
            mode = S_IFSOCK | 0600;
            break;
        default:
            return -EBADF;
        }
    }
    now = tick / TIMER_HZ;
    stx[0] = 0x7ff;              /* stx_mask */
    stx[1] = 512;                /* stx_blksize */
    stx[4] = 1;                  /* stx_nlink */
    stx[5] = node ? node->uid : 0;
    stx[6] = node ? node->gid : 0;
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

static int linux_abs_from_dir(int dirfd, const char *path, char *abs,
                              u32 max);

static u32 linux_open_flags(u32 flags)
{
#if defined(__aarch64__)
    u32 out = flags & ~(040000u | 0100000u | 0400000u);
    if (flags & 040000u) out |= O_DIRECTORY;
    if (flags & 0100000u) out |= O_NOFOLLOW;
    if (flags & 0400000u) out |= O_LARGEFILE;
    return out;
#else
    return flags;
#endif
}

static int linux_openat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx,
                               (const char *)frame->ecx,
                               abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    frame->ebx = (uaddr)abs;
    frame->ecx = linux_open_flags(frame->edx);
    frame->edx = frame->esi;
    return sys_open(frame);
}

static int linux_pselect6(struct trap_frame *frame)
{
    int nfds = (int)frame->ebx;
    u32 *rset = (u32 *)frame->ecx;
    u32 *wset = (u32 *)frame->edx;
    u32 *eset = (u32 *)frame->esi;
    u32 *ts = (u32 *)frame->edi;
    uaddr *sigdata = (uaddr *)frame->ebp;
    u32 orig_r[2] = { 0, 0 }, orig_w[2] = { 0, 0 }, orig_e[2] = { 0, 0 };
    u32 words, i, start = tick, timeout_ticks = 0;
    u32 oldmask = current->blocked_sig;
    int result;

    if (nfds < 0 || nfds > FD_MAX) {
        return -EINVAL;
    }
    words = ((uaddr)nfds + 31) / 32;
    if ((words && rset && (uaddr)rset < 0x1000) ||
            (words && wset && (uaddr)wset < 0x1000) ||
            (words && eset && (uaddr)eset < 0x1000) ||
            (ts && (uaddr)ts < 0x1000) ||
            (sigdata && (uaddr)sigdata < 0x1000)) {
        return -EFAULT;
    }
    if (sigdata) {
        u32 *mask;
        if (sigdata[1] != 8 ||
                (sigdata[0] && (uaddr)sigdata[0] < 0x1000)) {
            return -EINVAL;
        }
        if (sigdata[0]) {
            mask = (u32 *)(uaddr)sigdata[0];
            current->blocked_sig = *mask &
                ~(1u << SIGKILL) & ~(1u << SIGSTOP);
        }
    }
    for (i = 0; i < words; i++) {
        if (rset) {
            orig_r[i] = rset[i];
            rset[i] = 0;
        }
        if (wset) {
            orig_w[i] = wset[i];
            wset[i] = 0;
        }
        if (eset) {
            orig_e[i] = eset[i];
            eset[i] = 0;
        }
    }
    if (ts) {
        u64 sec, nsec;
#if __SIZEOF_POINTER__ == 8
        sec = ((u64 *)ts)[0];
        nsec = ((u64 *)ts)[1];
#else
        sec = ts[0];
        nsec = ts[1];
#endif
        if (nsec >= 1000000000u) {
            result = -EINVAL;
            goto out;
        }
        if (sec > 0x7fffffffu / TIMER_HZ) {
            timeout_ticks = 0xffffffffu;
        } else {
            timeout_ticks = sec * TIMER_HZ +
                (u32)nsec / (1000000000u / TIMER_HZ);
            if (nsec && timeout_ticks == 0) {
                timeout_ticks = 1;
            }
        }
    }

    for (;;) {
        int fd, ready = 0;
        net_poll();
        for (fd = 0; fd < nfds; fd++) {
            u32 word = (uaddr)fd / 32;
            u32 bit = 1u << (fd & 31);
            int wr = rset && (orig_r[word] & bit);
            int ww = wset && (orig_w[word] & bit);
            int we = eset && (orig_e[word] & bit);
            short events = (short)((wr ? POLLIN : 0) |
                                   (ww ? POLLOUT : 0) |
                                   (we ? POLLPRI : 0));
            short rev;
            if (!events) {
                continue;
            }
            if (!current->fds[fd].used) {
                result = -EBADF;
                goto out;
            }
            rev = poll_revents(&current->fds[fd], events);
            if (rev) {
                if (wr) rset[word] |= bit;
                if (ww) wset[word] |= bit;
                if (we) eset[word] |= bit;
                ready++;
            }
        }
        if (ready) {
            result = ready;
            goto out;
        }
        if (ts && (timeout_ticks == 0 || tick - start >= timeout_ticks)) {
            result = 0;
            goto out;
        }
        if (current->pending_sig & ~current->blocked_sig) {
            result = -EINTR;
            goto out;
        }
        sti();
        hlt();
        cli();
    }

out:
    if (sigdata) {
        current->blocked_sig = oldmask;
    }
    return result;
}

static int linux_dup3(struct trap_frame *frame)
{
    u32 flags = frame->edx;
    int r;
    if (flags & ~(uaddr)O_CLOEXEC || frame->ebx == frame->ecx) {
        return -EINVAL;
    }
    r = sys_dup2(frame);
    if (r >= 0 && (flags & O_CLOEXEC)) {
        current->fds[r].flags = FD_CLOEXEC;
    }
    return r;
}

static int linux_llseek(struct trap_frame *frame)
{
    u32 high = frame->ecx;
    u32 low = frame->edx;
    u32 *result = (u32 *)frame->esi;
    int r;

    if ((uaddr)result < 0x1000) {
        return -EFAULT;
    }
    if ((high != 0 && high != 0xffffffffu) ||
            (high == 0 && low > 0x7fffffffu) ||
            (high == 0xffffffffu && low < 0x80000000u)) {
        return -EOVERFLOW;
    }
    frame->ecx = low;
    frame->edx = frame->edi;
    r = sys_lseek(frame);
    if (r < 0) {
        return r;
    }
    result[0] = (uaddr)r;
    result[1] = 0;
    return 0;
}

static int linux_faccessat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx,
                               (const char *)frame->ecx,
                               abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    frame->ebx = (uaddr)abs;
    frame->ecx = frame->edx;
    return sys_access(frame);
}

static int linux_fchownat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx,
                               (const char *)frame->ecx, abs, sizeof(abs));
    if (r < 0) return r;
    frame->ebx = (uaddr)abs;
    frame->ecx = frame->edx;
    frame->edx = frame->esi;
    return sys_chown(frame);
}

static int linux_readlink_path(const char *path, u8 *buf, u32 count)
{
    struct fs_node *node;
    int r;

    if ((uaddr)buf < 0x1000) {
        return -EFAULT;
    }
    r = resolve_path_nofollow(path, &node);
    if (r < 0) {
        return r;
    }
    if ((node->flags & 0x7) != FS_SYMLINK) {
        return -EINVAL;
    }
    return (int)read_fs(node, 0, count, buf);
}

static int linux_readlinkat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ebx,
                               (const char *)frame->ecx,
                               abs, sizeof(abs));
    if (r < 0) return r;
    return linux_readlink_path(abs, (u8 *)frame->edx, frame->esi);
}

static int linux_symlink_path(const char *path, const char *target)
{
    char abs[256];
    int r;

    if ((uaddr)target < 0x1000) {
        return -EFAULT;
    }
    r = path_absolute(path, abs, sizeof(abs));
    if (r < 0) return r;
    r = vfs_symlink(abs, target);
    if (r < 0) return r;
    jfs_log(JFS_SYMLINK, abs, 0, 0777, (const u8 *)target,
            (uaddr)strlen(target));
    return 0;
}

static int linux_symlinkat(struct trap_frame *frame)
{
    char abs[160];
    int r = linux_abs_from_dir((int)frame->ecx,
                               (const char *)frame->edx,
                               abs, sizeof(abs));
    if (r < 0) return r;
    return linux_symlink_path(abs, (const char *)frame->ebx);
}

static int linux_prctl(struct trap_frame *frame)
{
    u32 option = frame->ebx;
    char *name = (char *)frame->ecx;

    if (option == 15) {
        if ((uaddr)name < 0x1000) {
            return -EFAULT;
        }
        copy_string(current->name, name, sizeof(current->name));
        return 0;
    }
    if (option == 16) {
        if ((uaddr)name < 0x1000) {
            return -EFAULT;
        }
        copy_string(name, current->name, sizeof(current->name));
        return 0;
    }
    if (option == 35) {
        return 0;
    }
    return -EINVAL;
}

static int linux_utimes(struct trap_frame *frame)
{
    frame->ecx = F_OK;
    return sys_access(frame);
}

static int linux_futimens(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    return 0;
}

static int linux_utimensat(struct trap_frame *frame)
{
    const char *path = (const char *)frame->ecx;
    char abs[160];
    int r;

    if (!path || !*path) {
        int fd = (int)frame->ebx;
        if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
            return -EBADF;
        }
        return 0;
    }
    if ((uaddr)path < 0x1000) {
        return -EFAULT;
    }
    r = linux_abs_from_dir((int)frame->ebx, path, abs, sizeof(abs));
    if (r < 0) {
        return r;
    }
    frame->ebx = (uaddr)abs;
    frame->ecx = F_OK;
    return sys_access(frame);
}

static int linux_sysinfo(struct trap_frame *frame)
{
    u32 *info = (u32 *)frame->ebx;
    if ((uaddr)info < 0x1000) {
        return -EFAULT;
    }
    memset((u8 *)info, 0, 64);
    info[0] = tick / TIMER_HZ;
    info[4] = MEMORY_SIZE;
    info[5] = MEMORY_SIZE;
    info[13] = 1;
    return 0;
}

static int linux_sched_getaffinity(struct trap_frame *frame)
{
    u32 size = frame->ecx;
    u8 *mask = (u8 *)frame->edx;
    if (size < sizeof(uaddr) || (uaddr)mask < 0x1000) {
        return size < sizeof(uaddr) ? -EINVAL : -EFAULT;
    }
    memset(mask, 0, size);
    *(u32 *)mask = 1;
    return 0;
}

static int linux_times(struct trap_frame *frame)
{
    u32 *tms = (u32 *)frame->ebx;
    if (tms) {
        if ((uaddr)tms < 0x1000) {
            return -EFAULT;
        }
        memset((u8 *)tms, 0, 16);
    }
    return (int)tick;
}

static int linux_getrusage(struct trap_frame *frame)
{
    u32 *usage = (u32 *)frame->ecx;
    if ((uaddr)usage < 0x1000) {
        return -EFAULT;
    }
    memset((u8 *)usage, 0, 72);
    return 0;
}

static int linux_getrlimit(struct trap_frame *frame)
{
    u32 *old = (u32 *)frame->ecx;
    u32 val = frame->ebx == 7 ? 16 : 64u * 1024u * 1024u;
    if ((uaddr)old < 0x1000) {
        return -EFAULT;
    }
    old[0] = val;
    old[1] = val;
    return 0;
}

static int sys_getgroups(struct trap_frame *frame);
static int linux_getgroups(struct trap_frame *frame)
{
    return sys_getgroups(frame);
}

static int sys_getuid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->uid;
}
static int sys_geteuid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->euid;
}
static int sys_getgid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->gid;
}
static int sys_getegid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->egid;
}

static int cred_can_set(u32 id)
{
    return current->euid == 0 || id == current->uid || id == current->euid ||
           id == current->suid;
}
static int cred_can_set_gid(u32 id)
{
    return current->euid == 0 || id == current->gid || id == current->egid ||
           id == current->sgid;
}

static int sys_setuid(struct trap_frame *frame)
{
    u32 id = frame->ebx;
    if (!cred_can_set(id)) return -EPERM;
    if (current->euid == 0) {
        current->uid = current->euid = current->suid = id;
    } else {
        current->euid = id;
    }
    return 0;
}
static int sys_seteuid(struct trap_frame *frame)
{
    u32 id = frame->ebx;
    if (!cred_can_set(id)) return -EPERM;
    current->euid = id;
    return 0;
}
static int sys_setgid(struct trap_frame *frame)
{
    u32 id = frame->ebx;
    if (!cred_can_set_gid(id)) return -EPERM;
    if (current->euid == 0) {
        current->gid = current->egid = current->sgid = id;
    } else {
        current->egid = id;
    }
    return 0;
}
static int sys_setegid(struct trap_frame *frame)
{
    u32 id = frame->ebx;
    if (!cred_can_set_gid(id)) return -EPERM;
    current->egid = id;
    return 0;
}

static int sys_setresuid(struct trap_frame *frame)
{
    u32 uid = frame->ebx, euid = frame->ecx, suid = frame->edx;
    if (current->euid != 0 &&
            ((uid != (u32)-1 && !cred_can_set(uid)) ||
             (euid != (u32)-1 && !cred_can_set(euid)) ||
             (suid != (u32)-1 && !cred_can_set(suid)))) {
        return -EPERM;
    }
    if (uid != (u32)-1) current->uid = uid;
    if (euid != (u32)-1) current->euid = euid;
    if (suid != (u32)-1) current->suid = suid;
    return 0;
}
static int sys_setresgid(struct trap_frame *frame)
{
    u32 gid = frame->ebx, egid = frame->ecx, sgid = frame->edx;
    if (current->euid != 0 &&
            ((gid != (u32)-1 && !cred_can_set_gid(gid)) ||
             (egid != (u32)-1 && !cred_can_set_gid(egid)) ||
             (sgid != (u32)-1 && !cred_can_set_gid(sgid)))) {
        return -EPERM;
    }
    if (gid != (u32)-1) current->gid = gid;
    if (egid != (u32)-1) current->egid = egid;
    if (sgid != (u32)-1) current->sgid = sgid;
    return 0;
}

static int sys_getgroups(struct trap_frame *frame)
{
    u32 size = frame->ebx;
    u32 *groups = (u32 *)frame->ecx;
    u32 i;
    if (size == 0) return (int)current->ngroups;
    if (size < current->ngroups) return -EINVAL;
    if (current->ngroups && (uaddr)groups < 0x1000) return -EFAULT;
    for (i = 0; i < current->ngroups; i++) groups[i] = current->groups[i];
    return (int)current->ngroups;
}
static int sys_setgroups(struct trap_frame *frame)
{
    u32 size = frame->ebx;
    u32 *groups = (u32 *)frame->ecx;
    u32 i;
    if (current->euid != 0) return -EPERM;
    if (size > 16 || (size && (uaddr)groups < 0x1000)) return -EINVAL;
    for (i = 0; i < size; i++) current->groups[i] = groups[i];
    current->ngroups = size;
    return 0;
}

static int cred_update_owner(struct fs_node *node, u32 uid, u32 gid)
{
    if (current->euid != 0) return -EPERM;
    if (!node) return -ENOENT;
    if (uid > 65535 || gid > 65535) return -EINVAL;
    if (uid != (u32)-1) node->uid = uid;
    if (gid != (u32)-1) node->gid = gid;
    return 0;
}

static int sys_chown(struct trap_frame *frame)
{
    const char *path = (const char *)frame->ebx;
    u32 uid = frame->ecx, gid = frame->edx;
    struct fs_node *node;
    char abs[160];
    int r = resolve_path(path, &node);
    if (r < 0) return r;
    r = cred_update_owner(node, uid, gid);
    if (r < 0) return r;
    if (path_normalize(path, abs, sizeof(abs)) == 0) {
        jfs_log(JFS_CHOWN, abs, 0,
                (node->uid & 0xffff) | ((node->gid & 0xffff) << 16), 0, 0);
    }
    return 0;
}
static int sys_fchown(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct file *f;
    int r;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) return -EBADF;
    f = &current->fds[fd];
    r = cred_update_owner(f->node, frame->ecx, frame->edx);
    if (r < 0) return r;
    if (f->ofd && f->ofd->path[0]) {
        jfs_log(JFS_CHOWN, f->ofd->path, 0,
                (f->node->uid & 0xffff) | ((f->node->gid & 0xffff) << 16),
                0, 0);
    }
    return 0;
}

static int linux_fsync(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -EBADF;
    }
    hd_flush();
    return 0;
}

static int tty_read(struct tty *t, u8 *buf, u32 count);
static int linux_abs_from_dir(int dirfd, const char *path, char *abs,
                              u32 max);
static int linux_mkdirat(struct trap_frame *frame);
static int linux_unlinkat(struct trap_frame *frame);
static int linux_renameat(struct trap_frame *frame);
static int linux_fchdir(struct trap_frame *frame);

#if __SIZEOF_POINTER__ == 8
static void linux64_fill_stat(void *output, const struct stat *st)
{
    struct arch_linux_stat *out = (struct arch_linux_stat *)output;
    memset((u8 *)out, 0, sizeof(*out));
    out->st_ino = st->st_ino;
    out->st_mode = st->st_mode;
    out->st_nlink = st->st_nlink;
    out->st_uid = st->st_uid;
    out->st_gid = st->st_gid;
    out->st_rdev = st->st_rdev;
    out->st_size = st->st_size;
    out->st_blksize = st->st_blksize;
    out->st_blocks = st->st_blocks;
    out->st_atime_sec = st->st_atime;
    out->st_mtime_sec = st->st_mtime;
    out->st_ctime_sec = st->st_ctime;
}

static int linux64_fstat(struct trap_frame *frame)
{
    struct stat st;
    uaddr out = frame->ecx;
    int r;
    if (out < 0x1000) return -EFAULT;
    frame->ecx = (uaddr)&st;
    r = sys_fstat(frame);
    if (r >= 0) linux64_fill_stat((void *)out, &st);
    return r;
}

static int linux64_stat(struct trap_frame *frame)
{
    struct stat st;
    uaddr out = frame->ecx;
    int r;
    if (out < 0x1000) return -EFAULT;
    frame->ecx = (uaddr)&st;
    r = sys_stat(frame);
    if (r >= 0) linux64_fill_stat((void *)out, &st);
    return r;
}

static int linux64_newfstatat(struct trap_frame *frame)
{
    char abs[256];
    const char *path = (const char *)frame->ecx;
    uaddr out = frame->edx;
    int r;
    if (path && !*path && frame->ebx < FD_MAX) {
        frame->ecx = out;
        return linux64_fstat(frame);
    }
    r = linux_abs_from_dir((int)frame->ebx, path, abs, sizeof(abs));
    if (r < 0) return r;
    frame->ebx = (uaddr)abs;
    frame->ecx = out;
    return linux64_stat(frame);
}

static int linux64_clock_gettime(struct trap_frame *frame)
{
    u64 *ts = (u64 *)frame->ecx;
    u32 clk = frame->ebx;
    if ((uaddr)ts < 0x1000) return -EFAULT;
    if (clk != CLOCK_REALTIME && clk != CLOCK_MONOTONIC) return -EINVAL;
    ts[0] = tick / TIMER_HZ;
    ts[1] = (tick % TIMER_HZ) * (1000000000u / TIMER_HZ);
    return 0;
}

static int linux64_gettimeofday(struct trap_frame *frame)
{
    u64 *tv = (u64 *)frame->ebx;
    if (!tv) return 0;
    if ((uaddr)tv < 0x1000) return -EFAULT;
    tv[0] = tick / TIMER_HZ;
    tv[1] = (tick % TIMER_HZ) * (1000000u / TIMER_HZ);
    return 0;
}

static int linux64_nanosleep(struct trap_frame *frame)
{
    u64 *input = (u64 *)frame->ebx;
    struct timespec req;
    if ((uaddr)input < 0x1000) return -EFAULT;
    req.tv_sec = input[0];
    req.tv_nsec = input[1];
    frame->ebx = (uaddr)&req;
    return sys_nanosleep(frame);
}

static int linux64_getrlimit(struct trap_frame *frame)
{
    u64 *old = (u64 *)frame->ecx;
    u64 val = frame->ebx == 7 ? 16 : 64ULL * 1024 * 1024;
    if ((uaddr)old < 0x1000) return -EFAULT;
    old[0] = val;
    old[1] = val;
    return 0;
}

static int linux64_getrusage(struct trap_frame *frame)
{
    void *usage = (void *)frame->ecx;
    if ((uaddr)usage < 0x1000) return -EFAULT;
    memset((u8 *)usage, 0, 144);
    return 0;
}

static int linux64_times(struct trap_frame *frame)
{
    u64 *tms = (u64 *)frame->ebx;
    if (tms) {
        if ((uaddr)tms < 0x1000) return -EFAULT;
        memset((u8 *)tms, 0, 32);
    }
    return (int)tick;
}

static int linux64_sysinfo(struct trap_frame *frame)
{
    u8 *info = (u8 *)frame->ebx;
    if ((uaddr)info < 0x1000) return -EFAULT;
    memset(info, 0, 128);
    *(u64 *)(info + 0) = tick / TIMER_HZ;
    *(u64 *)(info + 32) = MEMORY_SIZE;
    *(u64 *)(info + 40) = MEMORY_SIZE;
    *(u16 *)(info + 80) = 1;
    *(u32 *)(info + 104) = 1;
    return 0;
}

static int linux64_getresid(struct trap_frame *frame)
{
    u32 *values[3];
    u32 i;
    values[0] = (u32 *)frame->ebx;
    values[1] = (u32 *)frame->ecx;
    values[2] = (u32 *)frame->edx;
    for (i = 0; i < 3; i++) {
        if (values[i]) {
            if ((uaddr)values[i] < 0x1000) return -EFAULT;
            *values[i] = 0;
        }
    }
    return 0;
}

static int linux64_sigaltstack(struct trap_frame *frame)
{
    u8 *old = (u8 *)frame->ecx;
    if (!old) return 0;
    if ((uaddr)old < 0x1000) return -EFAULT;
    memset(old, 0, 24);
    *(u32 *)(old + 8) = 2;
    return 0;
}

static int linux64_arch_prctl(struct trap_frame *frame)
{
    uaddr option = frame->ebx;
    uaddr value = frame->ecx;
    if (option == 0x1002) {
        current->tls = value;
        arch_set_tls(value);
        return 0;
    }
    if (option == 0x1003) {
        if (value < 0x1000) return -EFAULT;
        *(uaddr *)value = arch_get_tls();
        return 0;
    }
    return -EINVAL;
}

struct linux64_k_sigaction {
    uaddr handler;
    uaddr flags;
    uaddr restorer;
    u64 mask;
};

static int linux64_sigaction(struct trap_frame *frame)
{
    int sig = (int)frame->ebx;
    struct linux64_k_sigaction *act = (void *)frame->ecx;
    struct linux64_k_sigaction *old = (void *)frame->edx;
    if (sig <= 0 || sig >= NSIG || sig == SIGKILL || sig == SIGSTOP)
        return -EINVAL;
    if (old) {
        if ((uaddr)old < 0x1000) return -EFAULT;
        old->handler = current->sig_handler[sig];
        old->flags = 0;
        old->restorer = current->sig_restorer[sig];
        old->mask = current->sig_mask[sig];
    }
    if (act) {
        if ((uaddr)act < 0x1000) return -EFAULT;
        current->sig_handler[sig] = act->handler;
        current->sig_mask[sig] = act->mask & ~(1u << SIGKILL) &
            ~(1u << SIGSTOP);
        current->sig_restorer[sig] = act->restorer;
    }
    return 0;
}
#endif

static int linux_dispatch(struct trap_frame *frame)
{
    u32 nr = frame->eax;

    switch (nr) {
#if __SIZEOF_POINTER__ == 8
    case 504: return linux64_stat(frame);
    case 505: return linux64_fstat(frame);
    case 513: return linux64_sigaction(frame);
    case 535: return linux64_nanosleep(frame);
    case 543: return linux64_times(frame);
    case 578: return linux64_gettimeofday(frame);
    case 597: return linux64_getrlimit(frame);
    case 598: return linux64_getrusage(frame);
    case 599: return linux64_sysinfo(frame);
    case 618: case 620: return linux64_getresid(frame);
    case 658: return linux64_arch_prctl(frame);
    case 686: return linux64_sigaltstack(frame);
    case 728: return linux64_clock_gettime(frame);
    case 762: return linux64_newfstatat(frame);
#endif
    case 29: return sys_pause(frame);
    case 1: case 252: return sys_exit(frame);
    case 2: return sys_fork(frame);
    case 3: {
        int fd = (int)frame->ebx;
        if (fd >= 0 && fd < FD_MAX && current->fds[fd].used &&
                current->fds[fd].kind == FD_TTY_IN &&
                (uaddr)frame->ecx >= 0x1000) {
            return tty_read(current->tty, (u8 *)frame->ecx,
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
    case 24: return sys_getuid(frame);
    case 47: return sys_getgid(frame);
    case 49: return sys_geteuid(frame);
    case 50: return sys_getegid(frame);
    case 199: return sys_getuid(frame);
    case 200: return sys_getgid(frame);
    case 201: return sys_geteuid(frame);
    case 202: return sys_getegid(frame);
    case 15: return sys_chmod(frame);
    case 94: return sys_fchmod(frame);
    case 308: return linux_pselect6(frame);
    case 330: return linux_dup3(frame);
    case 331: return sys_pipe(frame);
    case 306: {
        char abs[160];
        int r = linux_abs_from_dir((int)frame->ebx,
                                   (const char *)frame->ecx,
                                   abs, sizeof(abs));
        if (r < 0) {
            return r;
        }
        frame->ebx = (uaddr)abs;
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
    case 30: case 271: return linux_utimes(frame);
    case 36: hd_flush(); return 0;
    case 43: return linux_times(frame);
    case 54: return sys_ioctl(frame);
    case 57: return sys_setpgid(frame);
    case 65: return sys_getpgrp(frame);
    case 66: return sys_setsid(frame);
    case 55: return sys_fcntl(frame);
    case 60: return 0;          /* umask */
    case 23: return sys_setuid(frame);
    case 46: return sys_setgid(frame);
    case 70: return sys_seteuid(frame);
    case 71: return sys_setegid(frame);
    case 164: return sys_setresuid(frame);
    case 170: return sys_setresgid(frame);
    case 208: return sys_setresuid(frame);
    case 210: return sys_setresgid(frame);
    case 213: return sys_setuid(frame);
    case 214: return sys_setgid(frame);
    case 75: return 0;          /* setrlimit */
    case 76: return linux_getrlimit(frame);
    case 77: return linux_getrusage(frame);
    case 80: case 205: return linux_getgroups(frame);
    case 81: case 206: return sys_setgroups(frame);
    case 95: case 181: case 207: return sys_fchown(frame);
    case 182: case 212: return sys_chown(frame);
    case 63: return sys_dup2(frame);
    case 64: return sys_getppid(frame);
    case 78: return sys_gettimeofday(frame);
    case 91: return sys_munmap(frame);
    case 83: return linux_symlink_path((const char *)frame->ecx,
                                       (const char *)frame->ebx);
    case 85: return linux_readlink_path((const char *)frame->ebx,
                                        (u8 *)frame->ecx, frame->edx);
    case 92: return sys_truncate(frame);
    case 93: return sys_ftruncate(frame);
    case 114: return sys_waitpid(frame);
    case 116: return linux_sysinfo(frame);
    case 118: case 148: case 344: return linux_fsync(frame);
    case 120: return frame->ecx == 0 ? sys_fork(frame) : -ENOSYS;
    case 119: return signal_sigreturn_linux(frame, 0);
    case 122: return sys_uname(frame);
    case 125: return sys_mprotect(frame);
    case 132: return sys_getpgid(frame);
    case 133: return linux_fchdir(frame);
    case 140: return linux_llseek(frame);
    case 145: return linux_readv(frame);
    case 146: return linux_writev(frame);
    case 158: current->time_slice = 0; return 0;
    case 162: return sys_nanosleep(frame);
    case 168: return sys_poll(frame);
    case 172: return linux_prctl(frame);
    case 173: return signal_sigreturn_linux(frame, 1);
    case 174: return linux_sigaction(frame);
    case 175: return sys_sigprocmask(frame);
    case 186: {
        u32 *old = (u32 *)frame->ecx;
        if (old) {
            if ((uaddr)old < 0x1000) {
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
    case 220: return linux_getdents64(frame);
    case 221: return sys_fcntl(frame);
    case 296: return linux_mkdirat(frame);
    case 299: case 320: return linux_utimensat(frame);
    case 300: return linux_futimens(frame);
    case 301: return linux_unlinkat(frame);
    case 302: return linux_renameat(frame);
    case 307: case 439: return linux_faccessat(frame);
    case 309: return linux_fchownat(frame);
    case 224: return (int)current->pid;
    case 242: return linux_sched_getaffinity(frame);
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
    case 272: return 0;          /* fadvise64_64: advice is optional */
    case 295: return linux_openat(frame);
    case 304: return linux_symlinkat(frame);
    case 305: return linux_readlinkat(frame);
    case 311: return 0;
    case 340: return linux_prlimit64(frame);
    case 353: return frame->edi == 0 ? linux_renameat(frame) : -EINVAL;
    case 383: return linux_statx(frame);
    case 355: return linux_getrandom(frame);
    case 403: return linux_clock_gettime(frame, 1);
    default:
#ifdef DEBUG_E9
        printk("linux syscall %d: ENOSYS\n", nr);
#endif
        return -ENOSYS;
    }
}


#define LINUX_AT_REMOVEDIR 0x200

static int linux_abs_from_dir(int dirfd, const char *path, char *abs, u32 max)
{
    if (!path || (uaddr)path < 0x1000) {
        return -EFAULT;
    }
    if (path[0] == '/' || dirfd == (int)LINUX_AT_FDCWD) {
        return path_normalize(path, abs, max);
    }
    if (dirfd >= 0 && dirfd < FD_MAX && current->fds[dirfd].used &&
            current->fds[dirfd].ofd &&
            current->fds[dirfd].ofd->path[0]) {
        const char *base = current->fds[dirfd].ofd->path;
        u32 bl = (uaddr)strlen(base);
        u32 pl = (uaddr)strlen(path);
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
    frame->ebx = (uaddr)abs;
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
    frame->ebx = (uaddr)abs;
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
    frame->ebx = (uaddr)a;
    frame->ecx = (uaddr)b;
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

static int tty_read(struct tty *t, u8 *buf, u32 count)
{
    if (!t || !buf || !count) {
        return -EINVAL;
    }
    if (!(t->termios.c_lflag & ICANON) && t->in_pos >= t->in_len) {
        u32 n = 0;
        u32 vmin = t->termios.c_cc[VMIN];
        while (n < count) {
            int c = kbd_getchar();
            if (c < 0) {
                if (n > 0 || vmin == 0) {
                    break;
                }
                sti();
                while ((c = kbd_getchar()) < 0) {
                    hlt();
                }
                cli();
            }
            if (c == '\r' && (t->termios.c_iflag & ICRNL)) {
                c = '\n';
            }
            if ((t->termios.c_lflag & ECHO) && (c == '\n' || c >= ' ')) {
                char ch = (char)c;
                display_write(t, &ch, 1);
            }
            buf[n++] = (u8)c;
            if (n >= vmin && !kbd_haschar()) {
                break;
            }
        }
        return (int)n;
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
            if (c == t->termios.c_cc[VEOF]) {
                break;
            }
            if (c == t->termios.c_cc[VKILL]) {
                while (t->in_len > 0) {
                    t->in_len--;
                    display_write(t, "\b \b", 3);
                }
                continue;
            }
            if (c == t->termios.c_cc[VERASE] || c == '\b' || c == 0x7f) {
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
    int ret;
    sysc_func func = 0;

    if (current->linux_abi) {
        int ret;
        extern reg_t arch_linux_syscall_number(reg_t nr);
        frame->eax = arch_linux_syscall_number(frame->eax);
        ret = linux_syscall_handler(frame);
        frame->eax = (reg_t)(s64)ret;
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
