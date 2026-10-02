cmake_minimum_required(VERSION 3.20)

project(RTR-OS LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/kernel/boot/linker.ld)

set(KERNEL_C_SOURCES
    kernel/board/gic.c
    kernel/board/uart.c
    kernel/core/console.c
    kernel/core/main.c
    kernel/core/memory.c
    kernel/core/mmu.c
    kernel/core/panic.c
    kernel/core/timer.c
    kernel/core/trap.c
)

add_executable(kernel8
    kernel/boot/start.S
    kernel/boot/vectors.S
    ${KERNEL_C_SOURCES}
)

target_include_directories(kernel8 PRIVATE kernel/include)

# Compilação de ensaio: o kernel tenta gravar no próprio código logo após
# ligar a MMU. O resultado esperado é a parada por exceção.
option(RTR_FAULT_TEST "Ensaio de proteção de memória do kernel" OFF)
if(RTR_FAULT_TEST)
    target_compile_definitions(kernel8 PRIVATE RTR_FAULT_TEST)
endif()

# -mgeneral-regs-only: o kernel não usa FP/SIMD.
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
)

# Regras de codificação do plano (D9): todo aviso é erro, e o analisador
# estático roda a cada compilação. A lista de verificações está em .clang-tidy.
set(RTR_C_WARNINGS
    -Wall
    -Wextra
    -Werror
    -Wconversion
    -Wshadow
    -Wundef
    -Wvla
    -Wstrict-prototypes
    -Wmissing-prototypes
    -Wimplicit-fallthrough
)
set_source_files_properties(${KERNEL_C_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${RTR_C_WARNINGS}"
)
set_target_properties(kernel8 PROPERTIES
    C_CLANG_TIDY "${RTR_CLANG_TIDY};--warnings-as-errors=*"
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
