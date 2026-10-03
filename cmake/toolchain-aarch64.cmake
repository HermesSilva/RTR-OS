# RTR-OS - cross compilation for bare-metal AArch64 with clang/lld.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

set(LLVM_HINTS "C:/Program Files/LLVM/bin")
find_program(RTR_CLANG clang HINTS ${LLVM_HINTS} REQUIRED)
find_program(RTR_CLANG_TIDY clang-tidy HINTS ${LLVM_HINTS} REQUIRED)
find_program(RTR_OBJCOPY llvm-objcopy HINTS ${LLVM_HINTS} REQUIRED)

set(CMAKE_C_COMPILER ${RTR_CLANG})
set(CMAKE_ASM_COMPILER ${RTR_CLANG})
set(CMAKE_C_COMPILER_TARGET aarch64-none-elf)
set(CMAKE_ASM_COMPILER_TARGET aarch64-none-elf)

# There is no libc or runtime to link CMake's test programs against.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
