add_library(bsp STATIC
    src/bsp_bme280.c
    src/bsp_can.c
    src/bsp_led.c
    src/bsp_max31865.c
    src/bsp_mpu6050.c
    src/bsp_serial.c
    src/bsp_time.c
    src/bsp_vl53l0x.c
)

target_include_directories(bsp PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(bsp PUBLIC
    mcu_port
)

