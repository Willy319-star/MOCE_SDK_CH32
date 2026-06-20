if(NOT EXISTS ${CH32_EVT_SRC_DIR})
    message(FATAL_ERROR "WCH CH32V20x EVT SDK not found. Expected ${CH32_EVT_SRC_DIR}")
endif()

add_library(board_ch32v203g6u6 STATIC
    board.c
    board_clock.c
    board_gpio.c
    board_uart.c
    ch32v20x_it.c

    ${CH32_STARTUP_DIR}/startup_ch32v20x_D6.S
    ${CH32_SYSTEM_SOURCE}
    ${CH32_CORE_DIR}/core_riscv.c
    ${CH32_DEBUG_DIR}/debug.c

    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_gpio.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_can.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_i2c.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_rcc.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_spi.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_usart.c
    ${CH32_PERIPHERAL_SRC_DIR}/ch32v20x_misc.c
)

target_include_directories(board_ch32v203g6u6 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CH32_CORE_DIR}
    ${CH32_DEBUG_DIR}
    ${CH32_SYSTEM_DIR}
    ${CH32_PERIPHERAL_INC_DIR}
)

target_compile_definitions(board_ch32v203g6u6 PUBLIC
    CH32V20x
    CH32V203
)

target_link_options(board_ch32v203g6u6 INTERFACE
    -T${CMAKE_SOURCE_DIR}/linker/CH32V203G6U6_FLASH.ld
)


