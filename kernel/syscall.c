#include "syscall.h"
#include "debug.h"
#include "drivers/display.h"
#include "elf.h"
#include "fs.h"
#include "mm.h"
#include "page.h"
#include "string.h"
#include "tools.h"
#include "task.h"
#include "init.h"
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

int print_hex(u32 hex)
{
    int a;
    asm volatile("int $0x80" : "=a" (a) : "0" (2), "b"(hex));
    return a;
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

    printk("exec %s: entry 0x%x\n", name, entry);
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

static int sys_print_hex(struct trap_frame *frame)
{
    u32 hex = (u32)(frame->ebx);
    display_print_hex(hex);
    return 0;
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

int nosys(struct trap_frame *frame) {
    printk("SYSCALL NO.%d DOSE NOT EXIST.\n", frame->eax);
    return -1;
}

void init_syscall()
{
    // Register our syscall handler.
    register_interrupt_handler (0x80, &syscall_handler);
    syscalls[0] = &sys_print;
    syscalls[1] = &sys_fork;
    syscalls[2] = &sys_print_hex;
    syscalls[3] = &sys_exec;
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
