#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "drivers/keyboard.h"
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

int exec(const char *path)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (3), "b"(path));
    return a;
}

void exit()
{
    asm volatile("int $0x80" : : "a" (4));
}

int print_str(const char *str)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (0), "b"(str));
    return a;
}

// Load the ELF program stored in `name` into a fresh address space and
// turn the current task into it. Never returns on success: the syscall
// return path irets straight into the program's entry point.
static int sys_exec(struct trap_frame *frame)
{
    const char *name = (const char *)frame->ebx;
    if ((u32)name < 0x1000) {
        return -1;
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

    // map + fill every PT_LOAD segment
    u32 i;
    elf_section_header_t *ph = (elf_section_header_t *)(eh + elf->phoff);
    for (i = 0; i < elf->phnum; i++, ph++) {
        if (ph->type != ELF_PROG_LOAD) {
            continue;
        }
        u32 seg_start = ph->vaddr & PAGE_MASK;
        u32 seg_end = ph->vaddr + ph->memsz;
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

    u32 entry = elf->entry;
    // report while the old address space (holding `name`) is still alive
    printk("exec %s: entry 0x%x\n", name, entry);
    kfree(eh);

    // commit: replace the old address space, then rewrite the trap
    // frame so int_ret_stub drops the task into the new program
    switch_pdt(mm->pdt_phys);
    if (current->mm) {
        mm_destroy(current->mm);
    }
    current->mm = mm;

    frame->gs = frame->fs = frame->es = frame->ds = __USER_DS;
    frame->ss = __USER_DS;
    frame->cs = __USER_CS;
    frame->eflags = 0x202;          // IF set, bit 1 always one
    frame->eip = entry;
    frame->esp = stack_top;
    frame->useresp = stack_top;

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

// Terminate the calling task: free its address space, become a zombie
// and switch away for good. Never returns.
static int sys_exit(__UNUSED__ struct trap_frame *frame)
{
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
        if (pos + 16 >= max) {
            break;
        }
        pos += ps_utoa(ubuf + pos, max - pos, t->pid);
        ubuf[pos++] = ' ';
        ubuf[pos++] = state_char[t->state > ZOMBIE ? 0 : t->state];
        ubuf[pos++] = ' ';
        pos += ps_utoa(ubuf + pos, max - pos, (u32)(t->tty - tty));
        ubuf[pos++] = ' ';
        pos += ps_utoa(ubuf + pos, max - pos, t->priority);
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
