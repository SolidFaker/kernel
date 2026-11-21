.code64
.section .text
.global _start
.type _start, @function
_start:
    movq $_bss_start, %rdi
    movq $kernel_end_pos, %rcx
    subq %rdi, %rcx
    shrq $3, %rcx
    xorq %rax, %rax
    rep stosq
    movq $_stack_top, %rsp
    call kernel_start
1:
    hlt
    jmp 1b
