#include "unistd.h"
#include "arch/user.h"

#if defined(__i386__)
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
#endif

char **environ = 0;

__attribute__((noreturn)) void __libc_sigrestorer(void)
{
    arch_syscall0(NR_sigreturn);
    for (;;) { }
}
