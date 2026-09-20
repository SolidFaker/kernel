#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/tty.h"
#include "elf.h"
#include "fs.h"
#include "mm.h"
#include "page.h"
#include "string.h"
#include "tools.h"
#include "task.h"
#include "init.h"
#include "unistd.h"
#include "vfs.h"

typedef int (*sysc_func) (struct trap_frame *r);

static void syscall_handler(struct trap_frame *frame);

static sysc_func syscalls[SYSCALL_NUM];

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
// execve semantics: argv==0 falls back to { path }; envp is ignored
// (the environment is always empty for now).
static int sys_exec(struct trap_frame *frame)
{
    const char *name = (const char *)frame->ebx;
    char **argv_user = (char **)(frame->ecx);
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

    struct fs_node *node = finddir_fs(fs_root, (char *)name);
    if (node == 0 || (node->flags & 0x7) != FS_FILE) {
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

    // build the initial stack words: [argc][argv...][NULL][envp NULL]
    // with the strings just below the stack top (x86 process start)
    u32 sp = stack_top - str_bytes;
    copy_to_userva(mm, sp, (const u8 *)argv_buf, str_bytes);
    u32 str_base = sp;
    sp &= ~3u;
    sp -= 4 * (argc + 3);
    u32 words[8 + 3];
    words[0] = argc;
    for (i = 0; i < argc; i++) {
        words[1 + i] = str_base + argv_off[i];
    }
    words[1 + argc] = 0;  // argv terminator
    words[2 + argc] = 0;  // envp terminator
    copy_to_userva(mm, sp, (const u8 *)words, 4 * (argc + 3));

    u32 entry = elf->entry;
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
    // the user heap starts right after the last segment
    current->brk = (brk_max + PAGE_SIZE - 1) & PAGE_MASK;

    frame->gs = frame->fs = frame->es = frame->ds = __USER_DS;
    frame->ss = __USER_DS;
    frame->cs = __USER_CS;
    frame->eflags = 0x202;          // IF set, bit 1 always one
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
    {   /* mirror user output to the bochs debug port for headless runs */
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
    current->exit_code = frame->ebx;
    switch_pdt(kernel_pdt_phys());
    if (current->mm) {
        mm_destroy(current->mm);
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
        if (t->parent == parent &&
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
    if (pid == 0 || pid < -1) {
        return -1;  // process groups are not supported
    }

    struct task_struct *z;
    for (;;) {
        z = task_find_child(current, pid, 1);
        if (z != 0) {
            break;
        }
        if (task_find_child(current, pid, 0) == 0) {
            return -1;  // no such child
        }
        // no zombie yet: idle until the child runs and exits
        sti();
        hlt();
        cli();
    }

    if (ustatus != 0 && (u32)ustatus >= 0x1000) {
        *ustatus = z->exit_code;
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
    if (f->kind == FD_FILE) {
        u32 n = read_fs(f->node, f->offset, count, ubuf);
        f->offset += n;
        return (int)n;
    }
    return -1;  // directories do not read
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
        {   /* mirror output to the bochs debug port for headless runs */
            const char *c;
            for (c = ubuf; c < ubuf + count; c++) {
                outb(0xE9, (u8)*c);
            }
        }
#endif
        display_write(current->tty, ubuf, count);
        return (int)count;
    }
    return -1;
}

static int sys_open(struct trap_frame *frame)
{
    const char *name = (const char *)(frame->ebx);
    u32 flags = frame->ecx;
    if ((u32)name < 0x1000) {
        return -1;
    }

    struct fs_node *node;
    if ((name[0] == '.' && name[1] == 0) ||
            (name[0] == '/' && name[1] == 0)) {
        node = fs_root;
    } else {
        node = finddir_fs(fs_root, (char *)name);
    }
    if (node == 0) {
        return -1;
    }
    if ((flags & O_RDWR) != 0 && (node->flags & 0x7) == FS_FILE) {
        return -1;  // read-only filesystem
    }

    int fd;
    for (fd = 0; fd < FD_MAX; fd++) {
        if (!current->fds[fd].used) {
            break;
        }
    }
    if (fd == FD_MAX) {
        return -1;
    }

    current->fds[fd].used = 1;
    current->fds[fd].kind =
        ((node->flags & 0x7) == FS_DIRECTORY) ? FD_DIR : FD_FILE;
    current->fds[fd].node = node;
    current->fds[fd].offset = 0;
    return fd;
}

static int sys_close(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used) {
        return -1;
    }
    memset((u8 *)&current->fds[fd], 0, sizeof(current->fds[fd]));
    return 0;
}

static int sys_lseek(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    int offset = (int)frame->ecx;
    u32 whence = frame->edx;
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            current->fds[fd].kind != FD_FILE) {
        return -1;
    }

    struct file *f = &current->fds[fd];
    u32 base;
    switch (whence) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = f->offset; break;
        case SEEK_END: base = f->node->length; break;
        default: return -1;
    }
    if (offset < 0 && (u32)(-offset) > base) {
        return -1;
    }
    f->offset = base + offset;
    return (int)f->offset;
}

static int sys_fstat(struct trap_frame *frame)
{
    int fd = (int)frame->ebx;
    struct stat *ust = (struct stat *)(frame->ecx);
    if (fd < 0 || fd >= FD_MAX || !current->fds[fd].used ||
            (u32)ust < 0x1000) {
        return -1;
    }

    struct fs_node *node = current->fds[fd].node;
    ust->st_mode =
        ((node->flags & 0x7) == FS_DIRECTORY) ? S_IFDIR : S_IFREG;
    ust->st_size = node->length;
    return 0;
}

static int sys_getpid(__UNUSED__ struct trap_frame *frame)
{
    return (int)current->pid;
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
    while ((de = readdir_fs(f->node, f->offset)) != 0) {
        const char *n = de->name;
        while (*n && pos + 1 < max) {
            ubuf[pos++] = *n++;
        }
        if (pos + 1 < max) {
            ubuf[pos++] = '\n';
        }
        f->offset++;
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
}

void syscall_handler(struct trap_frame *frame)
{
    u32 ret;
    sysc_func func = 0;

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
