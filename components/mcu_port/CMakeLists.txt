add_library(mcu_port STATIC
    src/mcu_port_gpio.c
    src/mcu_port_i2c.c
    src/mcu_port_spi.c
    src/mcu_port_time.c
    src/mcu_port_uart.c
)

target_include_directories(mcu_port PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mcu_port PUBLIC
    board_${BOARD}
)

