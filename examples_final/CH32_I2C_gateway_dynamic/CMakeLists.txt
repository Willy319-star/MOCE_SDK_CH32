add_executable(${APP}.elf main.c)

target_include_directories(${APP}.elf PRIVATE ${CMAKE_SOURCE_DIR}/examples/common)
target_link_libraries(${APP}.elf PRIVATE -Wl,--whole-archive board_${BOARD} -Wl,--no-whole-archive user_api)
target_link_options(${APP}.elf PRIVATE ${COMMON_LINK_FLAGS})

add_custom_command(TARGET ${APP}.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${APP}.elf ${APP}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${APP}.elf ${APP}.bin
    COMMAND ${CMAKE_SIZE} ${APP}.elf
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)
