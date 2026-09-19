/* user_link.ld -- link a user-mode program below PAGE_OFFSET; the
   kernel's exec() maps its PT_LOAD segments at these addresses. */
ENTRY(_start)
SECTIONS
{
    . = 0x10000;

    .text : {
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : { *(.comment) *(.eh_frame) *(.note*) }
}
