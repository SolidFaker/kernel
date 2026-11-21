.code16                 # Assemble for 16−bit mode

start:
    cli                   # Disable BIOS interrupts
# Zero data segment registers DS, ES, and SS.
    xorw %ax, %ax # Set %ax to zero
    movw %ax, %ds # −> Data Segment
    movw %ax, %es # −> Extra Segment
    movw %ax, %ss # −> Stack Segment

# Try to set a VBE linear-framebuffer mode while the BIOS is still
# reachable. Leaves a struct bootinfo at 0x7600 on success.
    sti
    call vbe_setup
    cli
    xorw %ax, %ax
    movw %ax, %ds
    movw %ax, %es

protected_mode:

# enable A20 line 
    in  $0x92, %al
    or  $0x02, %al
    out %al, $0x92

    lgdt gdt_ptr         # load the new gdt pointer

# Switch from real to protected mode. Use a bootstrap GDT that makes
# virtual addresses map directly to physical addresses so that the
# effective memory map doesn’t change during the transition.
# prepare to protect move
    movl %cr0, %eax
    or   $0x01, %al
    movl %eax, %cr0

# Complete transition to 32−bit protected mode by using long jmp
# to reload %cs and %eip. The segment descriptors are set up with no
# translation, so that the mapping is still the identity mapping.
    jmp $0x8, $start_32

.code32
start_32:
    movw $0x10, %ax 
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

# Set up the stack pointer and call into C.
    movl $start, %esp
    call loadermain

# If loadermain returns (it shouldn’t), hang forever.
spin:
    jmp spin

gdt:
    .word 0,0,0,0
    .word 0xffff, 0x0000, 0x9a00, 0x00cf    # 0x08 code segment
    .word 0xffff, 0x0000, 0x9200, 0x00cf    # 0x10 data segment

    .p2align 2           # force 4 byte alignment
gdt_ptr:
    .word (gdt_ptr - gdt - 1)
    .long gdt

.code16
# ---------------------------------------------------------------------------
# Ask VBE for 1024x768x32 with a linear framebuffer. Controller info lands
# at 0x7000, mode info at 0x7200, the mode-list cursor at 0x7400 and the
# final result at 0x7600 (struct bootinfo in include/bootinfo.h).
# ---------------------------------------------------------------------------
vbe_setup:
    movw $0x7000, %di
    movw $0x4F00, %ax
    int  $0x10
    cmpw $0x004F, %ax
    jne  vbe_fail
    cmpl $0x41534556, 0x7000      # "VESA"
    jne  vbe_fail

    movw 0x700E, %ax              # mode list offset
    movw %ax, 0x7400
    movw 0x7010, %ax              # mode list segment
    movw %ax, 0x7402

vbe_next:
    movw 0x7402, %ax
    movw %ax, %es
    movw 0x7400, %si
    movw %es:(%si), %cx
    addw $2, 0x7400
    cmpw $0xFFFF, %cx
    je   vbe_fail

    xorw %ax, %ax
    movw %ax, %es
    movw $0x7200, %di
    movw $0x4F01, %ax
    int  $0x10
    cmpw $0x004F, %ax
    jne  vbe_next

    movw 0x7200, %ax              # attributes
    andw $0x0081, %ax             # supported + linear framebuffer
    cmpw $0x0081, %ax
    jne  vbe_next

    cmpw $1280, 0x7212            # x resolution
    jne  vbe_next
    cmpw $1024, 0x7214             # y resolution
    jne  vbe_next
    cmpb $32, 0x7219              # bits per pixel
    jne  vbe_next

    movw %cx, %bx
    orw  $0x4000, %bx             # request linear framebuffer
    movw $0x4F02, %ax
    int  $0x10
    cmpw $0x004F, %ax
    jne  vbe_fail

    movl 0x7228, %eax             # physBasePtr
    movl %eax, 0x7604
    movw 0x7212, %ax
    movw %ax, 0x7608
    movw 0x7214, %ax
    movw %ax, 0x760A
    movw 0x7210, %ax              # bytes per scan line
    movw %ax, 0x760C
    movb 0x7219, %al
    movb %al, 0x760E
    movb $0, 0x760F
    movl $0x31454256, %eax        # "VBE1"
    movl %eax, 0x7600
    ret

vbe_fail:
    movl $0, 0x7600
    ret
