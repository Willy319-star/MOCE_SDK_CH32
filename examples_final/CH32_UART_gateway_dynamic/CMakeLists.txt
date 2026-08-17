add_executable(${APP}.elf main.c)

target_include_directories(${APP}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/examples/common
)
target_link_libraries(${APP}.elf PRIVATE
    -Wl,--whole-archive
    board_${BOARD}
    -Wl,--no-whole-archive
    user_api
)

target_link_options(${APP}.elf PRIVATE
    ${COMMON_LINK_FLAGS}
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${APP}.map
)

add_custom_command(TARGET ${APP}.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${APP}.elf>
    COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${APP}.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/${APP}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${APP}.elf>
            ${CMAKE_CURRENT_BINARY_DIR}/${APP}.bin
    VERBATIM
)
