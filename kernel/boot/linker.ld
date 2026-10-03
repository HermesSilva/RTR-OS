/*
 * RTR-OS - image layout. The Pi 4 firmware loads kernel8.img at 0x80000.
 *
 * Sections are page aligned because each one gets its own permission in the
 * translation table (kernel/core/mmu.c).
 */

ENTRY(_start)

SECTIONS
{
    . = 0x80000;

    image_text_start = .;
    .text : {
        KEEP(*(.text.boot))
        *(.text .text.*)
    }
    . = ALIGN(4096);
    image_text_end = .;

    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(4096);
    image_rodata_end = .;

    .data : {
        *(.data .data.*)
    }

    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(16);
        __bss_end = .;
    }
    . = ALIGN(4096);
    image_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note*)
        *(.eh_frame*)
    }
}
