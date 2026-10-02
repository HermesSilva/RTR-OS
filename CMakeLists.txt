cmake_minimum_required(VERSION 3.20)

project(RTR-OS LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/src/boot/linker.ld)

add_executable(kernel8
    src/boot/start.S
    src/boot/vectors.S
    src/drivers/gic.c
    src/drivers/timer.c
    src/drivers/uart.c
    src/kernel/kprintf.c
    src/kernel/main.c
    src/kernel/sched.c
    src/kernel/trap.c
)

target_include_directories(kernel8 PRIVATE src/include)

# -mgeneral-regs-only: o kernel não usa FP/SIMD, então o contexto salvo fica pequeno.
# -mstrict-align: com a MMU desligada, acesso desalinhado à memória gera falha.
target_compile_options(kernel8 PRIVATE
    -mcpu=cortex-a72
    -mgeneral-regs-only
    -mstrict-align
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -O2
    -g
    -Wall
    -Wextra
)

target_link_options(kernel8 PRIVATE
    -nostdlib
    -static
    -fuse-ld=lld
    -Wl,-T,${LINKER_SCRIPT}
    -Wl,--build-id=none
    -Wl,-Map,${CMAKE_BINARY_DIR}/kernel8.map
)
set_target_properties(kernel8 PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# O firmware do Pi carrega a imagem binária crua, não o ELF.
add_custom_command(TARGET kernel8 POST_BUILD
    COMMAND ${RTR_OBJCOPY} -O binary $<TARGET_FILE:kernel8> ${CMAKE_BINARY_DIR}/kernel8.img
    COMMENT "Gerando kernel8.img"
)
