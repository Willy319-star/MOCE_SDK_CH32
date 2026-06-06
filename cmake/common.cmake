set(CH32_SERIES V20x)
set(CH32_PART CH32V203G6U6)

if(EXISTS ${CMAKE_SOURCE_DIR}/third_party/ch32v20x_repo/EVT/EXAM/SRC)
    set(CH32_THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party/ch32v20x_repo)
else()
    set(CH32_THIRD_PARTY_DIR ${CMAKE_SOURCE_DIR}/third_party/ch32v20x)
endif()

set(CH32_EVT_SRC_DIR ${CH32_THIRD_PARTY_DIR}/EVT/EXAM/SRC)
set(CH32_PERIPHERAL_INC_DIR ${CH32_EVT_SRC_DIR}/Peripheral/inc)
set(CH32_PERIPHERAL_SRC_DIR ${CH32_EVT_SRC_DIR}/Peripheral/src)
set(CH32_CORE_DIR ${CH32_EVT_SRC_DIR}/Core)
set(CH32_DEBUG_DIR ${CH32_EVT_SRC_DIR}/Debug)
set(CH32_STARTUP_DIR ${CH32_EVT_SRC_DIR}/Startup)
set(CH32_SYSTEM_DIR ${CH32_THIRD_PARTY_DIR}/EVT/EXAM/GPIO/GPIO_Toggle/User)
set(CH32_SYSTEM_SOURCE ${CH32_SYSTEM_DIR}/system_ch32v20x.c)

message(STATUS "CH32 series = ${CH32_SERIES}")
message(STATUS "CH32 part   = ${CH32_PART}")
message(STATUS "CH32 SDK    = ${CH32_THIRD_PARTY_DIR}")

set(MCU_FLAGS
    -march=rv32imac_zicsr_zifencei
    -mabi=ilp32
    -msmall-data-limit=8
    -msave-restore
)

add_compile_options(
    ${MCU_FLAGS}
    -Wall
    -Wextra
    -ffunction-sections
    -fdata-sections
    -fmessage-length=0
    -g3
    -O0
)

add_compile_definitions(
    CH32V20x
    CH32V203
)

set(COMMON_LINK_FLAGS
    ${MCU_FLAGS}
    -nostartfiles
    -Wl,--gc-sections
    -Wl,-Map=${APP}.map
    -specs=nano.specs
    -specs=nosys.specs
)


