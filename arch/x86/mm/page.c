#include "init.h"
#include "page.h"
#include "mm.h"
#include "string.h"
#include "debug.h"
#include "bootinfo.h"
#include "drivers/display.h"
#include "tools.h"

page_entry_t __attribute__((aligned(PAGE_SIZE)))
    pdt_kernel[PAGE_TABLE_SIZE];

static page_entry_t __attribute__((aligned(PAGE_SIZE)))
    pet_kernel[KPDT_COUNT][PAGE_TABLE_SIZE];

static page_entry_t __attribute__((aligned(PAGE_SIZE)))
    pet_fb[2][PAGE_TABLE_SIZE];

// Map the VBE linear framebuffer at FB_VIRT_BASE. Two page tables cover
// the 1280x1024x32 mode selected by the loader (5 MiB).
static void map_framebuffer(void)
{
    struct bootinfo *bi =
        (struct bootinfo *)(BOOTINFO_ADDR + PAGE_OFFSET);
    u32 size, pages, i, t;

    if (bi->magic != BOOTINFO_MAGIC || bi->bpp != 32 || bi->fb_addr == 0) {
        return;
    }

    size = (u32)bi->pitch * bi->height;
    pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    if (pages > 2 * PAGE_TABLE_SIZE) {
        pages = 2 * PAGE_TABLE_SIZE;
    }
    for (i = 0; i < pages; i++) {
        t = i / PAGE_TABLE_SIZE;
        pet_fb[t][i % PAGE_TABLE_SIZE].base = (bi->fb_addr >> 12) + i;
        pet_fb[t][i % PAGE_TABLE_SIZE].flags = PG_WRITE | PG_PRESENT;
    }

    for (t = 0; t < 2; t++) {
        i = PDT_INDEX(FB_VIRT_BASE) + t;
        pdt_kernel[i].base = ((u32)pet_fb[t] - PAGE_OFFSET) >> 12;
        pdt_kernel[i].flags = PG_WRITE | PG_PRESENT;
    }
}

    // Initilizing kernel page directory table
    // Mapping kernel space start at 0xC0000000
void init_page()
{
    u32 i,j;
    u32 kpdt_start = PDT_INDEX(PAGE_OFFSET);
    // FIXME: kernel mappings carry PG_USER because user mode currently runs
    // kernel text at CPL3 (see switch_to_user_mode). Drop PG_USER here once
    // real user programs are loaded below PAGE_OFFSET.
    for (i=kpdt_start, j=0; i < (kpdt_start + KPDT_COUNT); i++, j++) {
        pdt_kernel[i].base = ((u32)&pet_kernel[j] - PAGE_OFFSET) >> 12;
        pdt_kernel[i].flags = PG_WRITE | PG_PRESENT | PG_USER;
    }

    page_entry_t *pet = *pet_kernel;
    for (i=0; i < KPDT_SIZE; i++) {
        pet[i].base = i;
        pet[i].flags = PG_WRITE | PG_PRESENT | PG_USER;
    }

    map_framebuffer();

    // Registered page fault handler
    register_interrupt_handler(14, (interrupt_handler_t)page_fault);

    // Switch kernel page directory table
    u32 pdt_kernel_phy_addr = (u32)pdt_kernel - PAGE_OFFSET;
    switch_pdt(pdt_kernel_phy_addr);
}

void switch_pdt(uaddr pdt_addr)
{
    asm volatile ("movl %0, %%cr3" :: "r"(pdt_addr));   // put page table addr
}

// Mapping physical address to vritual address
void map(page_entry_t *pdt_now, uaddr va, uaddr pa, u32 flags)
{
    u32 pdt_index = PDT_INDEX(va);
    u32 pet_index = PET_INDEX(va);
    page_entry_t *pet_now;

    pet_now = (page_entry_t *)((u32)pdt_now[pdt_index].base << 12);
    // if the PET not present
    if (!pet_now) {
        pet_now = (page_entry_t *)page_alloc();
        pdt_now[pdt_index].base = (u32)(pet_now) >> 12;
        pdt_now[pdt_index].flags = PG_WRITE | PG_PRESENT | PG_USER;
        // Switch to kernel space
        pet_now = (page_entry_t *)((u32)pet_now + PAGE_OFFSET);
        bzero((u8 *)pet_now, PAGE_SIZE);
    } else {
        // Switch to kernel space
        pet_now = (page_entry_t *)((u32)pet_now + PAGE_OFFSET);
    }
    pet_now[pet_index].base = (pa >> 12);
    // flags come from the caller: kernel memory stays supervisor-only,
    // user pages pass PG_USER explicitly
    pet_now[pet_index].flags = flags;
    // flush CPU page cache
    asm volatile ("invlpg (%0)" : : "a" (va));
}

void unmap(page_entry_t *pdt_now, uaddr va)
{
    u32 pdt_index = PDT_INDEX(va);
    u32 pet_index = PET_INDEX(va);

    page_entry_t *pet_now = (page_entry_t *)((u32)pdt_now[pdt_index].base << 12);
    if (!pet_now) {
        return;
    }
    // Switch to kernel space
    pet_now = (page_entry_t *)((u32)pet_now + PAGE_OFFSET);
    pet_now[pet_index].base = 0;
    pet_now[pet_index].flags = 0;

    // flush CPU page cache
    asm volatile ("invlpg (%0)" : : "a" (va));
}

u32 get_mapping(page_entry_t *pdt_now, uaddr va, uaddr *pa)
{
    u32 pdt_index = PDT_INDEX(va);
    u32 pet_index = PET_INDEX(va);

    page_entry_t *pet_now = (page_entry_t *)((u32)pdt_now[pdt_index].base << 12);
    if (!pet_now) {
        return 0;
    }
    // Switch to kernel space
    pet_now = (page_entry_t *)((u32)pet_now + PAGE_OFFSET);

    // if page exists return psychics addrss
    if (pet_now[pet_index].flags != 0 && pa) {
        *pa = pet_now[pet_index].base << 12;
        return 1;
    }

    return 0;
}

// Page Fault handler
void page_fault(struct trap_frame *frame)
{
    // The faulting address is stored in the CR2 register.
    u32 cr2;
    asm volatile ("mov %%cr2, %0" : "=r" (cr2));

    printk("Page fault at 0x%x, virtual faulting address 0x%x\n", frame->eip, cr2);
    printk("Error code: %x\n", frame->err_code);

    // bit0=0: Protection fault
    if (!(frame->err_code & (1 << 0))) {
        ERROR("Page not present.");
    }
    // bit1=0: read fault | bit1=1: write fault
    if (frame->err_code & (1 << 2)) {
        ERROR("Write error.");
    } else {
        ERROR("Read error.");
    }
    // bit2=1: interrupted in user mode | bit2=0 interrupted in kernel mode
    if (frame->err_code & (1 << 2)) {
        ERROR("In user mode.");
    } else {
        ERROR("In kernel mode.");
    }
    // bit3=1: protected bit overwritten
    if (frame->err_code & (1 << 3)) {
        ERROR("Use of reserved bit detected.");
    }
    // bit4=1: occured at instruction fetching
    if (frame->err_code & (1 << 4)) {
        ERROR("Fault was an instruction fetch.");
    }
    // bit5=1: Protection keys block access
    if (frame->err_code & (1 << 5)) {
        ERROR("Protection keys block access");
    }
    hlt();
}
