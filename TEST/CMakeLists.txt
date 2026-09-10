cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME TEST)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        Interface/Int_can.c
        Interface/Int_can.h)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
        ${CMAKE_CURRENT_SOURCE_DIR}/Interface
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)

# ELF 文件由 add_executable 生成。
# 在每次链接 TEST 目标后，重新生成 HEX 和 BIN 文件。
set(HEX_FILE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex)
set(BIN_FILE ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin)

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E remove -f
        ${HEX_FILE}
        ${BIN_FILE}

        COMMAND ${CMAKE_OBJCOPY} -Oihex
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${HEX_FILE}

        COMMAND ${CMAKE_OBJCOPY} -Obinary
        $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        ${BIN_FILE}

        COMMENT "Generating HEX and BIN files"
)

# ============ 程序体积优化（作用于全工程，含 HAL 库，保证 ABI 一致） ============
add_compile_options(
        -fno-common              # 未初始化全局变量各自独立 section，配合 gc-sections 裁剪
        -fno-unwind-tables       # 去掉 C 异常展开表
        -fno-asynchronous-unwind-tables
        -fmerge-constants        # 合并相同常量
)

# 等效 Keil5 勾选 "USE MicroLIB"：启用 newlib-nano 精简 C 库
target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
        --specs=nano.specs
        --specs=nosys.specs
)


