cmake_minimum_required(VERSION 3.15)

project(riverFinderLibJ LANGUAGES C CXX)

# ========= 基本设置 =========

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ========= 外部 cubiomes =========
#
# 这里不再使用 riverfinder 自带的旧 cubiomes。默认指向
# <riverfinder>/../cubiomes；独立构建时用 -DRIVER_CUBIOMES_DIR=... 指定。
set(RIVER_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cubiomes"
    CACHE PATH "Path to an external cubiomes checkout")
if (NOT EXISTS "${RIVER_CUBIOMES_DIR}/generator.c")
    message(FATAL_ERROR
        "RIVER_CUBIOMES_DIR does not look like cubiomes: ${RIVER_CUBIOMES_DIR}")
endif()
get_filename_component(CUBIOMES_INCLUDE_ROOT "${RIVER_CUBIOMES_DIR}" DIRECTORY)

# ========= 查找 JNI =========

find_package(JNI REQUIRED)

# ========= 源文件 =========

set(SOURCES
        sunnyslopes_riverfinder_RiverFinderBridge.cpp
        ${RIVER_CUBIOMES_DIR}/generator.c
        ${RIVER_CUBIOMES_DIR}/layers.c
        ${RIVER_CUBIOMES_DIR}/biomenoise.c
        ${RIVER_CUBIOMES_DIR}/biomes.c
        ${RIVER_CUBIOMES_DIR}/noise.c
        ${RIVER_CUBIOMES_DIR}/util.c
        ${RIVER_CUBIOMES_DIR}/quadbase.c
)

# ========= 构建 JNI 动态库 =========

add_library(riverFinderLibJ SHARED ${SOURCES})

target_include_directories(riverFinderLibJ PRIVATE
        ${JNI_INCLUDE_DIRS}
        "${CUBIOMES_INCLUDE_ROOT}"
        "${CMAKE_CURRENT_SOURCE_DIR}/.."
)

# ========= 编译优化 =========

if(MSVC)
    target_compile_options(riverFinderLibJ PRIVATE
            /O2
            /DNDEBUG
    )
else()
    target_compile_options(riverFinderLibJ PRIVATE
            -O3
            -DNDEBUG
            -ffp-contract=off
            -march=native
    )
endif()

# ========= 静态链接 MinGW runtime =========

if(MINGW)
    target_link_options(riverFinderLibJ PRIVATE
            -static
            -static-libgcc
            -static-libstdc++
    )
endif()

# ========= 输出目录 =========

set_target_properties(riverFinderLibJ PROPERTIES
        PREFIX "lib"
        OUTPUT_NAME "riverFinderLibJ"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/native
        LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/native
)

# ========= 自动 strip（减小体积） =========

if(NOT MSVC)
    add_custom_command(TARGET riverFinderLibJ POST_BUILD
            COMMAND ${CMAKE_STRIP} $<TARGET_FILE:riverFinderLibJ>
    )
endif()
