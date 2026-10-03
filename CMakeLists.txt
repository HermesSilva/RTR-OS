cmake_minimum_required(VERSION 3.20)

project(RTR-OS LANGUAGES C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_EXECUTABLE_SUFFIX ".elf")

# Coding rules of the plan (D9): every warning is an error.
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

# Neither the kernel nor the programs use FP/SIMD: the saved context stays small.
set(RTR_COMMON_OPTIONS
    -mcpu=cortex-a72
    -mgeneral-regs-only
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -O2
    -g
)

# ---------------------------------------------------------------------------
# Programs: run in EL0, each in its own address space
# ---------------------------------------------------------------------------

set(USER_LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/user/lib/link.ld)
find_program(RTR_PYTHON NAMES python3 python REQUIRED)

add_library(rtruser STATIC
    user/lib/crt0.S
    user/lib/print.c
    user/lib/string.c
)
target_include_directories(rtruser PUBLIC abi user/lib/include)
target_compile_options(rtruser PUBLIC ${RTR_COMMON_OPTIONS})
set_source_files_properties(user/lib/print.c user/lib/string.c
    PROPERTIES COMPILE_OPTIONS "${RTR_C_WARNINGS}"
)

# Creates program <name> and its binary image <name>.bin, which the kernel loads.
#   rtr_add_program(<name> DESCRIPTION "<text>" [REALTIME] SOURCES <files...>)
function(rtr_add_program name)
    cmake_parse_arguments(PROGRAM "REALTIME" "DESCRIPTION" "SOURCES" ${ARGN})
    set(flags 0)
    if(PROGRAM_REALTIME)
        set(flags 1)
    endif()
    add_executable(${name} ${PROGRAM_SOURCES} user/lib/header.S)
    target_compile_definitions(${name} PRIVATE
        "RTR_PROGRAM_FLAGS=${flags}"
        "RTR_PROGRAM_DESCRIPTION=\"${PROGRAM_DESCRIPTION}\""
    )
    target_link_libraries(${name} PRIVATE -Wl,--whole-archive rtruser -Wl,--no-whole-archive)
    target_link_options(${name} PRIVATE
        -nostdlib
        -static
        -fuse-ld=lld
        -Wl,-T,${USER_LINKER_SCRIPT}
        -Wl,--build-id=none
        -Wl,-Map,${CMAKE_BINARY_DIR}/${name}.map
    )
    set_target_properties(${name} PROPERTIES LINK_DEPENDS ${USER_LINKER_SCRIPT})
    string(TOUPPER "${name}" upper)
    add_custom_command(TARGET ${name} POST_BUILD
        COMMAND ${RTR_OBJCOPY} -O binary $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/${name}.bin
        COMMAND ${RTR_PYTHON} ${CMAKE_SOURCE_DIR}/tools/rpkg/make-rpkg.py ${CMAKE_BINARY_DIR}/${name}.rpkg
                "${upper}.BIN=${CMAKE_BINARY_DIR}/${name}.bin"
        BYPRODUCTS ${CMAKE_BINARY_DIR}/${name}.bin ${CMAKE_BINARY_DIR}/${name}.rpkg
        COMMENT "Generating ${name}.bin and ${name}.rpkg"
    )
endfunction()

rtr_add_program(demo DESCRIPTION "Console report of the system statistics" SOURCES user/demo/main.c)
rtr_add_program(pulse DESCRIPTION "Real-time periodic job with bounded work" REALTIME SOURCES user/pulse/main.c)
rtr_add_program(pwm DESCRIPTION "Software PWM on a GPIO pin" REALTIME SOURCES user/pwm/main.c)
rtr_add_program(counter DESCRIPTION "Binary counter on consecutive GPIO pins" REALTIME SOURCES user/counter/main.c)
set_source_files_properties(user/demo/main.c user/pulse/main.c user/pwm/main.c
    PROPERTIES COMPILE_OPTIONS "${RTR_C_WARNINGS}")

# lwIP network stack (D16): third-party code, BSD license, fetched at a pinned version.
include(FetchContent)
FetchContent_Declare(lwip
    GIT_REPOSITORY https://github.com/lwip-tcpip/lwip.git
    GIT_TAG STABLE-2_2_1_RELEASE
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR do-not-use-lwip-cmake
)
FetchContent_MakeAvailable(lwip)

file(GLOB LWIP_SOURCES
    ${lwip_SOURCE_DIR}/src/core/*.c
    ${lwip_SOURCE_DIR}/src/core/ipv4/*.c
)
list(APPEND LWIP_SOURCES ${lwip_SOURCE_DIR}/src/netif/ethernet.c)

# System process (the boot set): SD card, manifest, Ethernet driver, lwIP and the web server.
set(SYSTEM_SOURCES
    user/system/fat32.c
    user/system/genet.c
    user/system/http.c
    user/system/main.c
    user/system/manifest.c
    user/system/sdcard.c
)
rtr_add_program(system DESCRIPTION "Boot set: SD card, network and web interface"
    SOURCES ${SYSTEM_SOURCES} user/system/www.S ${LWIP_SOURCES})
target_include_directories(system PRIVATE
    user/system
    user/system/port
    ${lwip_SOURCE_DIR}/src/include
)
set_source_files_properties(${SYSTEM_SOURCES} PROPERTIES COMPILE_OPTIONS "${RTR_C_WARNINGS}")
set_source_files_properties(user/system/www.S PROPERTIES
    COMPILE_DEFINITIONS "RTR_WWW_INDEX=\"${CMAKE_SOURCE_DIR}/user/system/www/index.html\""
    OBJECT_DEPENDS "${CMAKE_SOURCE_DIR}/user/system/www/index.html"
)

# ---------------------------------------------------------------------------
# Kernel
# ---------------------------------------------------------------------------

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/kernel/boot/linker.ld)

set(KERNEL_C_SOURCES
    kernel/board/gic.c
    kernel/board/reboot.c
    kernel/board/uart.c
    kernel/core/console.c
    kernel/core/install.c
    kernel/core/main.c
    kernel/core/memory.c
    kernel/core/mmu.c
    kernel/core/page.c
    kernel/core/panic.c
    kernel/core/process.c
    kernel/core/sched.c
    kernel/core/timer.c
    kernel/core/trap.c
)

add_executable(kernel8
    kernel/boot/start.S
    kernel/boot/vectors.S
    kernel/boot/programs.S
    ${KERNEL_C_SOURCES}
)

target_include_directories(kernel8 PRIVATE kernel/include abi)

# Test build: the kernel tries to write to its own code right after turning
# on the MMU. The expected result is a halt by exception.
option(RTR_FAULT_TEST "Kernel memory protection test" OFF)
if(RTR_FAULT_TEST)
    target_compile_definitions(kernel8 PRIVATE RTR_FAULT_TEST)
endif()

# -mstrict-align: the kernel runs a stretch with the MMU off, when an
# unaligned memory access faults.
target_compile_options(kernel8 PRIVATE ${RTR_COMMON_OPTIONS} -mstrict-align)

# The static analyzer runs on every kernel compilation. The list of checks
# is in .clang-tidy.
set_source_files_properties(${KERNEL_C_SOURCES}
    PROPERTIES COMPILE_OPTIONS "${RTR_C_WARNINGS}"
)
set_target_properties(kernel8 PROPERTIES
    C_CLANG_TIDY "${RTR_CLANG_TIDY};--warnings-as-errors=*"
)

# The boot set, embedded in the kernel image. The other programs live on the SD card.
set_source_files_properties(kernel/boot/programs.S PROPERTIES
    COMPILE_DEFINITIONS "RTR_PROGRAM_SYSTEM=\"${CMAKE_BINARY_DIR}/system.bin\""
    OBJECT_DEPENDS "${CMAKE_BINARY_DIR}/system.bin"
)
add_dependencies(kernel8 system demo pulse pwm)

target_link_options(kernel8 PRIVATE
    -nostdlib
    -static
    -fuse-ld=lld
    -Wl,-T,${LINKER_SCRIPT}
    -Wl,--build-id=none
    -Wl,-Map,${CMAKE_BINARY_DIR}/kernel8.map
)
set_target_properties(kernel8 PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# The Pi firmware loads the raw binary image, not the ELF.
add_custom_command(TARGET kernel8 POST_BUILD
    COMMAND ${RTR_OBJCOPY} -O binary $<TARGET_FILE:kernel8> ${CMAKE_BINARY_DIR}/kernel8.img
    COMMENT "Generating kernel8.img"
)
