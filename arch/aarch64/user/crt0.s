.section .text
.global _start
.type _start,%function
_start:
    ldr x0, [sp]
    add x1, sp, #8
    add x2, x1, x0, lsl #3
    add x2, x2, #8
    adr x3, environ
    str x2, [x3]
    bl main
    bl exit
1:
    b 1b
