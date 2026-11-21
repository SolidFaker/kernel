.code16                   # Assemble for 16-bit code

start:
    movw %cs, %ax           # make segment register point to 0x07c0
    movw %ax, %ds
    movw %ax, %ss
    movl $0x5000, %esp       # stack pointer prepare for copy disk

# Store memory map info 
# Save memory map at 0x1000, Then kernel would read this address
get_memory_map:
    movw $0, %ax 
    movw %ax, %es
    movw $0x1000, %di 
_loop:
    movw $0xe820, %ax 
    movw $20, %cx
    movl $0x534d4150, %edx
    int  $0x15 
    jc   _failed 
    add  $20, %di 
    incl (0x0500)
    cmp  $0, %ebx
    jne  _loop
    jmp  load_disk

_failed:
    movl $0, %es:(%di)

load_disk:
    movw $0x0080, %dx       # bios int 0x13 read data from disk
    movw $0x0002, %cx       # second sector
    movw $0x0800, %ax       # move to 0x8000 in menory
    movw %ax, %es           # ES:BX point to buffer
    xor  %bx, %bx
    movw $0x0200+9, %ax     # AH function index AL number of sector
    int  $0x13
    jnc  ok_loaded

die:  jmp  die

ok_loaded:
    jmp $0x0000, $0x8000

.org 510
    .word 0xaa55
