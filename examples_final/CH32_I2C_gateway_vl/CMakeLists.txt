add_executable(${APP}.elf
    main.c
)

if(NOT DEFINED CH32_NODE_ID)
    set(CH32_NODE_ID 1)
endif()

target_compile_definitions(${APP}.elf PRIVATE
    NODE_ID=${CH32_NODE_ID}
)

target_include_directories(${APP}.elf PRIVATE
    ${CMAKE_SOURCE_DIR}/examples_final/common
)

target_link_libraries(${APP}.elf PRIVATE
    -Wl,--whole-archive
    board_${BOARD}
    -Wl,--no-whole-archive
    user_api
)

target_link_options(${APP}.elf PRIVATE
    ${COMMON_LINK_FLAGS}
)

add_custom_command(TARGET ${APP}.elf POST_BUILD
    COMMAND ${CMAKE_SIZE} ${APP}.elf
    COMMAND ${CMAKE_OBJCOPY} -O ihex ${APP}.elf ${APP}.hex
    COMMAND ${CMAKE_OBJCOPY} -O binary ${APP}.elf ${APP}.bin
)
