set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

get_filename_component(SDK_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(LOCAL_XPACK_BIN "${SDK_ROOT}/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin")

if(EXISTS "${LOCAL_XPACK_BIN}/riscv-none-elf-gcc.exe")
    set(CMAKE_C_COMPILER "${LOCAL_XPACK_BIN}/riscv-none-elf-gcc.exe")
    set(CMAKE_ASM_COMPILER "${LOCAL_XPACK_BIN}/riscv-none-elf-gcc.exe")
    set(CMAKE_OBJCOPY "${LOCAL_XPACK_BIN}/riscv-none-elf-objcopy.exe")
    set(CMAKE_SIZE "${LOCAL_XPACK_BIN}/riscv-none-elf-size.exe")
    set(CMAKE_GDB "${LOCAL_XPACK_BIN}/riscv-none-elf-gdb.exe")
else()
    if(DEFINED ENV{RISCV_TOOLCHAIN_PREFIX})
        set(TOOLCHAIN_PREFIX $ENV{RISCV_TOOLCHAIN_PREFIX})
    else()
        set(TOOLCHAIN_PREFIX riscv-none-elf)
    endif()

    find_program(RISCV_GCC_FROM_PATH
        NAMES ${TOOLCHAIN_PREFIX}-gcc
        NO_CACHE
    )
    if(RISCV_GCC_FROM_PATH)
        get_filename_component(RISCV_TOOLCHAIN_BIN
            "${RISCV_GCC_FROM_PATH}" DIRECTORY)
        set(TOOLCHAIN_PREFIX "${RISCV_TOOLCHAIN_BIN}/riscv-none-elf")
    endif()

    if(CMAKE_HOST_WIN32)
        set(TOOLCHAIN_EXECUTABLE_SUFFIX ".exe")
    else()
        set(TOOLCHAIN_EXECUTABLE_SUFFIX "")
    endif()

    set(CMAKE_C_COMPILER
        "${TOOLCHAIN_PREFIX}-gcc${TOOLCHAIN_EXECUTABLE_SUFFIX}")
    set(CMAKE_ASM_COMPILER
        "${TOOLCHAIN_PREFIX}-gcc${TOOLCHAIN_EXECUTABLE_SUFFIX}")
    set(CMAKE_OBJCOPY
        "${TOOLCHAIN_PREFIX}-objcopy${TOOLCHAIN_EXECUTABLE_SUFFIX}")
    set(CMAKE_SIZE
        "${TOOLCHAIN_PREFIX}-size${TOOLCHAIN_EXECUTABLE_SUFFIX}")
    set(CMAKE_GDB
        "${TOOLCHAIN_PREFIX}-gdb${TOOLCHAIN_EXECUTABLE_SUFFIX}")
endif()

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
