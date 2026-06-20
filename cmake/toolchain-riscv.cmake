set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv)

set(TOOLCHAIN_DIR "${CMAKE_SOURCE_DIR}/tools/xpack-riscv-none-elf-gcc-15.2.0-1/bin")

set(CMAKE_C_COMPILER    "${TOOLCHAIN_DIR}/riscv-none-elf-gcc.exe"   CACHE FILEPATH "")
set(CMAKE_CXX_COMPILER  "${TOOLCHAIN_DIR}/riscv-none-elf-g++.exe"  CACHE FILEPATH "")
set(CMAKE_ASM_COMPILER  "${TOOLCHAIN_DIR}/riscv-none-elf-gcc.exe"  CACHE FILEPATH "")
set(CMAKE_AR             "${TOOLCHAIN_DIR}/riscv-none-elf-ar.exe"   CACHE FILEPATH "")
set(CMAKE_OBJCOPY        "${TOOLCHAIN_DIR}/riscv-none-elf-objcopy.exe" CACHE FILEPATH "")
set(CMAKE_SIZE           "${TOOLCHAIN_DIR}/riscv-none-elf-size.exe" CACHE FILEPATH "")
set(CMAKE_OBJDUMP        "${TOOLCHAIN_DIR}/riscv-none-elf-objdump.exe" CACHE FILEPATH "")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
