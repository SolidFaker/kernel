.code64
.global _start
.type _start,@function
_start:
    movq (%rsp), %rdi
    leaq 8(%rsp), %rsi
    leaq 8(%rsi,%rdi,8), %rdx
    movq %rdx, environ(%rip)
    call main
    movl %eax, %edi
    call exit
    hlt
