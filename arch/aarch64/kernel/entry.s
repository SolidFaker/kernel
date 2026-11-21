.section .text
.global _start
.type _start, %function
_start:
    mrs x0, cpacr_el1
    mov x1, #0x300000
    orr x0, x0, x1
    msr cpacr_el1, x0
    isb
    ldr x0, =_bss_start
    ldr x1, =kernel_end_pos
    sub x1, x1, x0
    cbz x1, 1f
    mov x2, x0
2:
    str xzr, [x2], #8
    subs x1, x1, #8
    bhi 2b
1:
    ldr x0, =_stack_top
    mov sp, x0
    bl kernel_start
3:
    b 3b
