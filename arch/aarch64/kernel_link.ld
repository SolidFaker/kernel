ENTRY(_start)
SECTIONS
{
    . = 0x40080000;
    PROVIDE(kernel_start_pos = .);
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss (NOLOAD) : {
        PROVIDE(_bss_start = .);
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        . += 32768;
        _stack_top = .;
        PROVIDE(kernel_end_pos = .);
    }
    /DISCARD/ : { *(.comment .note* .eh_frame*) }
}
