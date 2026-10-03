/*
 * RTR-OS - program layout.
 *
 * Sections are page aligned because the kernel gives each one its own
 * permission: code, constants and data.
 */

ENTRY(_start)

SECTIONS
{
    . = 0x100000000;                    /* RTR_USER_BASE */

    .text : {
        KEEP(*(.header))
        *(.text.start)
        *(.text .text.*)
    }
    . = ALIGN(4096);
    __text_end = .;

    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(4096);
    __rodata_end = .;

    .data : {
        *(.data .data.*)
    }
    __data_end = .;

    .bss (NOLOAD) : ALIGN(16) {
        *(.bss .bss.*)
        *(COMMON)
    }
    . = ALIGN(4096);
    __bss_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
