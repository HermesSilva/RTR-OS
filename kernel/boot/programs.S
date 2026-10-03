/*
 * RTR-OS - the boot set, embedded in the kernel image (D6a). The quoted path
 * of the binary comes from CMake. Every other program is a file on the SD card.
 */

    .section ".rodata.programs", "a"

    .balign 16
    .globl program_system_start
program_system_start:
    .incbin RTR_PROGRAM_SYSTEM
    .globl program_system_end
program_system_end:
