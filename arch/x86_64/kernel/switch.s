.code64
.section .text
.global switch_task
.type switch_task, @function
switch_task:
    movq (%rsp), %rax
    movq %rax, 0(%rdi)
    leaq 8(%rsp), %rax
    movq %rax, 8(%rdi)
    movq %rbp, 16(%rdi)
    movq %rbx, 24(%rdi)
    movq %r12, 32(%rdi)
    movq %r13, 40(%rdi)
    movq %r14, 48(%rdi)
    movq %r15, 56(%rdi)
    pushfq
    popq %r11
    movq %r11, 64(%rdi)

    movq 8(%rsi), %rsp
    movq 16(%rsi), %rbp
    movq 24(%rsi), %rbx
    movq 32(%rsi), %r12
    movq 40(%rsi), %r13
    movq 48(%rsi), %r14
    movq 56(%rsi), %r15
    movq 72(%rsi), %rdi
    movq 0(%rsi), %rax
    cli
    jmp *%rax

.global kthread_entry
kthread_entry:
    sti
    subq $8, %rsp
    call *%r12
    addq $8, %rsp
    ret
