.code32
.global enter_long_mode
.type enter_long_mode, @function
enter_long_mode:
    cli
    movl 4(%esp), %ebx

    movl $0x9000, %edi
    xorl %eax, %eax
    movl $8192, %ecx
    rep stosl

    movl $0x0000a003, 0x9000
    movl $0x0000f003, 0x9800
    movl $0x0000d003, 0x9ff8
    movl $0x0000b003, 0xa000
    movl $0x00010003, 0xf000
    movl $0x0000e003, 0xdff0

    movl $0xb000, %edi
    movl $0x00000083, %eax
    xorl %ecx, %ecx
1:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    incl %ecx
    cmpl $512, %ecx
    jne 1b

    movl $0x10000, %edi
    movl $0x00000083, %eax
    xorl %ecx, %ecx
2:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    incl %ecx
    cmpl $512, %ecx
    jne 2b

    movl $0xe000, %edi
    movl $0x00000083, %eax
    xorl %ecx, %ecx
3:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    addl $0x200000, %eax
    addl $8, %edi
    incl %ecx
    cmpl $16, %ecx
    jne 3b

    movl $0x9000, %eax
    movl %eax, %cr3
    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    movl $0xc0000080, %ecx
    rdmsr
    orl $0x100, %eax
    wrmsr

    movl %cr0, %eax
    orl $0x80000000, %eax
    movl %eax, %cr0

    lgdt gdt64_ptr
    ljmp $0x08, $long_mode_entry

.code64
long_mode_entry:
    movw $0x10, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movl $0x8000, %esp
    movl $0xffffffff, %ecx
    shlq $32, %rcx
    movl $0x80000000, %edx
    orq %rdx, %rcx
    movq %rbx, %rax
    addq %rcx, %rax
    call *%rax
4:
    hlt
    jmp 4b

.align 8
gdt64:
    .quad 0
    .quad 0x00209a0000000000
    .quad 0x0000920000000000
gdt64_ptr:
    .word (gdt64_ptr - gdt64 - 1)
    .long gdt64
