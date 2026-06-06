add_library(user_api STATIC
    src/vibe_api.c
    src/vibe_runtime.c
    src/vibe_runtime_main.c
)

target_include_directories(user_api PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(user_api PUBLIC
    bsp
)
