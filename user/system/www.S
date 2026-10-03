/* RTR-OS - web interface files embedded in the program. The path comes from CMake. */

    .section ".rodata.www", "a"

    .balign 8
    .globl www_index_start
www_index_start:
    .incbin RTR_WWW_INDEX
    .globl www_index_end
www_index_end:
    .byte 0
