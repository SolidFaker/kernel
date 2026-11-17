// C runtime entry: the kernel enters here with [esp] = argc, followed by
// argv[] and a NULL envp[]. Call main() and then exit().

__asm__(
".text\n"
".globl _start\n"
".type _start,@function\n"
"_start:\n"
"  movl (%esp), %eax\n"
"  leal 4(%esp), %ebx\n"
"  leal 4(%ebx,%eax,4), %ecx\n"
"  movl %ecx, environ\n"
"  pushl %ecx\n"
"  pushl %ebx\n"
"  pushl %eax\n"
"  call main\n"
"  pushl %eax\n"
"  call exit\n"
"  hlt\n"
);

char **environ = 0;

__asm__(
".globl __libc_sigrestorer\n"
"__libc_sigrestorer:\n"
"  movl $51, %eax\n"
"  int $0x80\n"
);
