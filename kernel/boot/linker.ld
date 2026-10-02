/*
 * RTR-OS - layout da imagem. O firmware do Pi 4 carrega kernel8.img em 0x80000.
 *
 * As seções são alinhadas a página porque cada uma recebe uma permissão
 * diferente na tabela de tradução (kernel/core/mmu.c).
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
