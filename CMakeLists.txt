cmake_minimum_required(VERSION 3.20)

project(moce_sdk_ch32 C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT DEFINED BOARD)
    set(BOARD ch32v203g6u6)
endif()

if(NOT DEFINED APP)
    set(APP led_blink)
endif()

message(STATUS "BOARD = ${BOARD}")
message(STATUS "APP   = ${APP}")

include(cmake/common.cmake)

add_subdirectory(boards/${BOARD})
add_subdirectory(components/mcu_port)
add_subdirectory(components/bsp)
add_subdirectory(user_api)

if(EXISTS ${CMAKE_SOURCE_DIR}/project/${APP}/CMakeLists.txt)
    add_subdirectory(project/${APP})
elseif(EXISTS ${CMAKE_SOURCE_DIR}/examples_final/${APP}/CMakeLists.txt)
    add_subdirectory(examples_final/${APP})
elseif(EXISTS ${CMAKE_SOURCE_DIR}/examples/${APP}/CMakeLists.txt)
    add_subdirectory(examples/${APP})
else()
    message(FATAL_ERROR "APP not found in project/${APP}, examples/${APP}, or examples_final/${APP}")
endif()

