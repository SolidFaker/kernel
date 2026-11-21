.section .text

.macro AARCH64_ENTRY name, user, handler
\name:
    .if \user
    msr tpidr_el1, x16
    ldr x16, =kernel_stack_top
    ldr x16, [x16]
    mov sp, x16
    .endif
    sub sp, sp, #1136
    .if \user
    mrs x16, tpidr_el1
    .endif
    stp x0, x1, [sp, #368]
    stp x2, x3, [sp, #384]
    stp x4, x5, [sp, #400]
    stp x6, x7, [sp, #416]
    stp x8, x9, [sp, #432]
    stp x10, x11, [sp, #448]
    stp x12, x13, [sp, #464]
    stp x14, x15, [sp, #480]
    stp x16, x17, [sp, #496]
    add x17, sp, #512
    stp x18, x19, [x17]
    stp x20, x21, [x17, #16]
    stp x22, x23, [x17, #32]
    stp x24, x25, [x17, #48]
    stp x26, x27, [x17, #64]
    stp x28, x29, [x17, #80]
    str x30, [sp, #608]
    stp q0, q1, [sp, #624]
    stp q2, q3, [sp, #656]
    stp q4, q5, [sp, #688]
    stp q6, q7, [sp, #720]
    stp q8, q9, [sp, #752]
    stp q10, q11, [sp, #784]
    stp q12, q13, [sp, #816]
    stp q14, q15, [sp, #848]
    stp q16, q17, [sp, #880]
    stp q18, q19, [sp, #912]
    stp q20, q21, [sp, #944]
    stp q22, q23, [sp, #976]
    stp q24, q25, [sp, #1008]
    add x17, sp, #1040
    stp q26, q27, [x17]
    stp q28, q29, [x17, #32]
    stp q30, q31, [x17, #64]
    mrs x0, elr_el1
    str x0, [sp, #112]
    mrs x0, spsr_el1
    str x0, [sp, #128]
    mrs x0, sp_el0
    .if \user
    str x0, [sp, #136]
    .else
    mov x0, sp
    add x0, x0, #1136
    str x0, [sp, #136]
    .endif
    mov x0, #3
    .if !\user
    mov x0, #0
    .endif
    str x0, [sp, #120]
    str x0, [sp, #144]
    mov x0, sp
    bl \handler
    b int_ret_stub
.endm

AARCH64_ENTRY aarch64_el1_sync, 0, aarch64_sync_dispatch
AARCH64_ENTRY aarch64_el1_irq, 0, aarch64_irq_dispatch
AARCH64_ENTRY aarch64_el0_sync, 1, aarch64_sync_dispatch
AARCH64_ENTRY aarch64_el0_irq, 1, aarch64_irq_dispatch
AARCH64_ENTRY aarch64_error, 0, aarch64_error_dispatch

.balign 2048
.global aarch64_vector_table
aarch64_vector_table:
    b aarch64_el1_sync
    .balign 128
    b aarch64_el1_irq
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_el1_sync
    .balign 128
    b aarch64_el1_irq
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_el0_sync
    .balign 128
    b aarch64_el0_irq
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
    .balign 128
    b aarch64_error
