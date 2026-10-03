/*
 * RTR-OS - the header at the start of every program image (struct
 * rtr_program_header). Assembled once per program, with the flags and the
 * description that CMake defines for it: RTR_PROGRAM_FLAGS and
 * RTR_PROGRAM_DESCRIPTION.
 */
#include <rtr/abi.h>

#ifndef RTR_PROGRAM_FLAGS
#define RTR_PROGRAM_FLAGS 0
#endif
#ifndef RTR_PROGRAM_DESCRIPTION
#define RTR_PROGRAM_DESCRIPTION ""
#endif

    .section ".header", "a"
    .quad   RTR_PROGRAM_MAGIC
    .quad   _start
    .quad   __text_end
    .quad   __rodata_end
    .quad   __data_end
    .quad   __bss_end
    .quad   RTR_PROGRAM_FLAGS
description:
    .asciz  RTR_PROGRAM_DESCRIPTION
    .org    description + RTR_PROGRAM_DESCRIPTION_MAX
