.section .text
.global switch_task
.type switch_task, %function
switch_task:
    mov x2, sp
    str x2, [x0, #8]
    str x29, [x0, #16]
    str x19, [x0, #24]
    str x20, [x0, #32]
    str x21, [x0, #40]
    str x22, [x0, #48]
    str x23, [x0, #56]
    str x24, [x0, #72]
    str x25, [x0, #80]
    str x26, [x0, #88]
    str x27, [x0, #96]
    str x28, [x0, #120]
    mrs x2, daif
    str x2, [x0, #64]
    str lr, [x0, #0]

    ldr x2, [x1, #8]
    mov sp, x2
    ldr x29, [x1, #16]
    ldr x19, [x1, #24]
    ldr x20, [x1, #32]
    ldr x21, [x1, #40]
    ldr x22, [x1, #48]
    ldr x23, [x1, #56]
    ldr x24, [x1, #72]
    ldr x25, [x1, #80]
    ldr x26, [x1, #88]
    ldr x27, [x1, #96]
    ldr x28, [x1, #120]
    ldr x2, [x1, #64]
    and x2, x2, #0x3c0
    add x2, x2, #5
    msr spsr_el1, x2
    ldr x2, [x1, #0]
    msr elr_el1, x2
    mov x0, x23
    eret

.global int_ret_stub
int_ret_stub:
    ldr x2, [sp, #112]
    msr elr_el1, x2
    ldr x2, [sp, #128]
    msr spsr_el1, x2
    ldr x2, [sp, #136]
    msr sp_el0, x2
    ldp q0, q1, [sp, #624]
    ldp q2, q3, [sp, #656]
    ldp q4, q5, [sp, #688]
    ldp q6, q7, [sp, #720]
    ldp q8, q9, [sp, #752]
    ldp q10, q11, [sp, #784]
    ldp q12, q13, [sp, #816]
    ldp q14, q15, [sp, #848]
    ldp q16, q17, [sp, #880]
    ldp q18, q19, [sp, #912]
    ldp q20, q21, [sp, #944]
    ldp q22, q23, [sp, #976]
    ldp q24, q25, [sp, #1008]
    add x30, sp, #1040
    ldp q26, q27, [x30]
    ldp q28, q29, [x30, #32]
    ldp q30, q31, [x30, #64]
    ldp x0, x1, [sp, #368]
    ldp x2, x3, [sp, #384]
    ldp x4, x5, [sp, #400]
    ldp x6, x7, [sp, #416]
    ldp x8, x9, [sp, #432]
    ldp x10, x11, [sp, #448]
    ldp x12, x13, [sp, #464]
    ldp x14, x15, [sp, #480]
    add x16, sp, #512
    ldp x18, x19, [x16]
    ldp x20, x21, [x16, #16]
    ldp x22, x23, [x16, #32]
    ldp x24, x25, [x16, #48]
    ldp x26, x27, [x16, #64]
    ldp x28, x29, [x16, #80]
    ldp x16, x17, [sp, #496]
    ldr x30, [sp, #120]
    ands x30, x30, #3
    bne 1f
    ldr x30, [sp, #136]
    mov sp, x30
    sub x30, x30, #528
    ldr x30, [x30]
    eret
1:
    ldr x30, [sp, #608]
    eret

.global enter_user_mode
enter_user_mode:
    mov sp, x0
    b int_ret_stub
