.code64
.section .text

.global isr_bad
isr_bad:
    pushq $0
    pushq $255
    jmp interrupt_common

.global isr_page_fault
isr_page_fault:
    pushq $14
    jmp interrupt_common

.global isr_ud
isr_ud:
    pushq $0
    pushq $6
    jmp interrupt_common

.global isr_gp
isr_gp:
    pushq $13
    jmp interrupt_common

.global irq_timer
irq_timer:
    pushq $0
    pushq $32
    jmp interrupt_common

.global irq_keyboard
irq_keyboard:
    pushq $0
    pushq $33
    jmp interrupt_common

interrupt_common:
    subq $368, %rsp
    movq %rax, 240(%rsp)
    movq %rbx, 248(%rsp)
    movq %rcx, 256(%rsp)
    movq %rdx, 264(%rsp)
    movq %rsi, 272(%rsp)
    movq %rdi, 280(%rsp)
    movq %rbp, 288(%rsp)
    movq %r8, 304(%rsp)
    movq %r9, 312(%rsp)
    movq %r10, 320(%rsp)
    movq %r11, 328(%rsp)
    movq %r12, 336(%rsp)
    movq %r13, 344(%rsp)
    movq %r14, 352(%rsp)
    movq %r15, 360(%rsp)
    movq %rsp, %r11
    addq $368, %r11
    movq %r11, 296(%rsp)
    addq $40, 296(%rsp)
    movq (%r11), %rax
    movq %rax, 96(%rsp)
    movq 8(%r11), %rax
    movq %rax, 104(%rsp)
    movq 16(%r11), %rax
    movq %rax, 112(%rsp)
    movq 24(%r11), %rax
    movq %rax, 120(%rsp)
    movq 32(%r11), %rax
    movq %rax, 128(%rsp)
    testb $3, 120(%rsp)
    jz 1f
    movq 40(%r11), %rax
    movq %rax, 136(%rsp)
    movq 48(%r11), %rax
    movq %rax, 144(%rsp)
    jmp 2f
1:
    leaq 40(%r11), %rax
    movq %rax, 136(%rsp)
    movq $0x10, 144(%rsp)
2:
    movq 280(%rsp), %rax
    movq %rax, 32(%rsp)
    movq 272(%rsp), %rax
    movq %rax, 40(%rsp)
    movq 288(%rsp), %rax
    movq %rax, 48(%rsp)
    movq 296(%rsp), %rax
    movq %rax, 56(%rsp)
    movq 248(%rsp), %rax
    movq %rax, 64(%rsp)
    movq 264(%rsp), %rax
    movq %rax, 72(%rsp)
    movq 256(%rsp), %rax
    movq %rax, 80(%rsp)
    movq 240(%rsp), %rax
    movq %rax, 88(%rsp)
    movq 304(%rsp), %rax
    movq %rax, 168(%rsp)
    movq 312(%rsp), %rax
    movq %rax, 176(%rsp)
    movq 320(%rsp), %rax
    movq %rax, 184(%rsp)
    movq 328(%rsp), %rax
    movq %rax, 192(%rsp)
    movq 336(%rsp), %rax
    movq %rax, 200(%rsp)
    movq 344(%rsp), %rax
    movq %rax, 216(%rsp)
    movq 352(%rsp), %rax
    movq %rax, 224(%rsp)
    movq 360(%rsp), %rax
    movq %rax, 232(%rsp)
    movq %rsp, %rbx
    andq $-16, %rsp
    movq %rbx, %rdi
    call int_handler
    movq %rbx, %rsp
    jmp int_ret_stub

.global syscall_entry
syscall_entry:
    movq %rsp, user_rsp_scratch(%rip)
    movq kernel_stack_top(%rip), %rsp
    subq $368, %rsp
    movq %rax, 240(%rsp)
    movq %rax, 88(%rsp)
    movq %rbx, 248(%rsp)
    movq %rcx, 256(%rsp)
    movq %rdx, 264(%rsp)
    movq %rsi, 272(%rsp)
    movq %rdi, 280(%rsp)
    movq %rbp, 288(%rsp)
    movq %r8, 304(%rsp)
    movq %r9, 312(%rsp)
    movq %r10, 320(%rsp)
    movq %r11, 328(%rsp)
    movq %r12, 336(%rsp)
    movq %r13, 344(%rsp)
    movq %r14, 352(%rsp)
    movq %r15, 360(%rsp)
    movq user_rsp_scratch(%rip), %rax
    movq %rax, 296(%rsp)
    movq %rax, 136(%rsp)
    movq %rdi, %rax
    movq %rax, 64(%rsp)
    movq %rsi, %rax
    movq %rax, 80(%rsp)
    movq %rdx, 72(%rsp)
    movq %r10, 40(%rsp)
    movq %r8, 32(%rsp)
    movq %r9, 48(%rsp)
    movq %rax, 0(%rsp)
    movq %rax, 8(%rsp)
    movq %rax, 16(%rsp)
    movq %rax, 24(%rsp)
    movq %rcx, 112(%rsp)
    movq $0x23, 120(%rsp)
    movq %r11, 128(%rsp)
    movq $0x1b, 144(%rsp)
    movq %rax, 208(%rsp)
    movq %rax, 216(%rsp)
    movq %rax, 224(%rsp)
    movq %rax, 232(%rsp)
    movq %r8, 168(%rsp)
    movq %r9, 176(%rsp)
    movq %r10, 184(%rsp)
    movq %r11, 192(%rsp)
    movq %r12, 200(%rsp)
    movq $0x80, 96(%rsp)
    movq $0, 104(%rsp)
    movq %rsp, %rbx
    andq $-16, %rsp
    movq %rbx, %rdi
    call x86_64_syscall_dispatch
    movq %rbx, %rsp
    jmp int_ret_stub

.global int_ret_stub
int_ret_stub:
    testb $3, 120(%rsp)
    jnz 1f
    movq 296(%rsp), %rdx
    subq $24, %rdx
    movq 112(%rsp), %rax
    movq %rax, (%rdx)
    movq 120(%rsp), %rax
    movq %rax, 8(%rdx)
    movq 128(%rsp), %rax
    movq %rax, 16(%rdx)
    movq %rdx, user_rsp_scratch(%rip)
1:
    movq 248(%rsp), %rbx
    movq 256(%rsp), %rcx
    movq 264(%rsp), %rdx
    movq 272(%rsp), %rsi
    movq 280(%rsp), %rdi
    movq 288(%rsp), %rbp
    movq 304(%rsp), %r8
    movq 312(%rsp), %r9
    movq 320(%rsp), %r10
    movq 328(%rsp), %r11
    movq 336(%rsp), %r12
    movq 344(%rsp), %r13
    movq 352(%rsp), %r14
    movq 360(%rsp), %r15
    movq 88(%rsp), %rax
    testb $3, 120(%rsp)
    jnz 2f
    movq user_rsp_scratch(%rip), %rsp
    jmp 3f
2:
    leaq 112(%rsp), %rsp
3:
    iretq

.global enter_user_mode
enter_user_mode:
    movq %rdi, %rsp
    jmp int_ret_stub

.section .bss
.align 8
user_rsp_scratch:
    .quad 0
