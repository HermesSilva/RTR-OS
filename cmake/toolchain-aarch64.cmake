# RTR-OS - compilação cruzada para AArch64 bare-metal com clang/lld.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LLVM_HINTS "C:/Program Files/LLVM/bin")
find_program(RTR_CLANG clang HINTS ${LLVM_HINTS} REQUIRED)
find_program(RTR_OBJCOPY llvm-objcopy HINTS ${LLVM_HINTS} REQUIRED)

set(CMAKE_C_COMPILER ${RTR_CLANG})
set(CMAKE_ASM_COMPILER ${RTR_CLANG})
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)

# Não há libc nem runtime para linkar os programas de teste do CMake.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
