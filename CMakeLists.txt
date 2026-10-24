cmake_minimum_required(VERSION 3.15)
project(riverfinder C CXX)

if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

if (MSVC)
    set(CMAKE_C_FLAGS_DEBUG "/Od /Zi /DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "/O2 /DNDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "/Od /Zi /DDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "/O2 /DNDEBUG")
else()
    set(CMAKE_C_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
    set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g3 -DDEBUG")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()

if(WIN32)
    add_compile_definitions(_WIN32)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

# ---------------------------------------------------------------------------
# External cubiomes.
#
# This repository no longer vendors cubiomes. It builds against the cubiomes
# checkout that the caller points at. The default layout is the All-In-One
# Finder workspace:
#
#   <repo>/../cubiomes      (i.e. tools/cubiomes in the app tree)
#
# For a standalone checkout pass:
#   -DRIVER_CUBIOMES_DIR=<path-to-cubiomes>
# ---------------------------------------------------------------------------
set(RIVER_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cubiomes"
    CACHE PATH "Path to an external cubiomes checkout")
if (NOT EXISTS "${RIVER_CUBIOMES_DIR}/generator.c")
    message(FATAL_ERROR
        "RIVER_CUBIOMES_DIR does not look like cubiomes: ${RIVER_CUBIOMES_DIR}\n"
        "Pass -DRIVER_CUBIOMES_DIR=<path-to-cubiomes>.")
endif()

# The sources include "cubiomes/generator.h", so the include root is the
# parent directory of the cubiomes checkout.
get_filename_component(CUBIOMES_INCLUDE_ROOT "${RIVER_CUBIOMES_DIR}" DIRECTORY)

set(RIVER_CUBIOMES_SOURCES
    "${RIVER_CUBIOMES_DIR}/generator.c"
    "${RIVER_CUBIOMES_DIR}/layers.c"
    "${RIVER_CUBIOMES_DIR}/biomenoise.c"
    "${RIVER_CUBIOMES_DIR}/biomes.c"
    "${RIVER_CUBIOMES_DIR}/noise.c"
    "${RIVER_CUBIOMES_DIR}/util.c"
    "${RIVER_CUBIOMES_DIR}/quadbase.c"
)

# riverFinderLib.cpp includes river_finder.cpp; compiling both would duplicate
# every symbol.
add_library(riverfinder STATIC
    ${RIVER_CUBIOMES_SOURCES}
    riverFinderLib.cpp
)
set_target_properties(riverfinder PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(riverfinder PUBLIC
    "${CUBIOMES_INCLUDE_ROOT}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
)

target_compile_options(riverfinder PRIVATE
    $<$<COMPILE_LANGUAGE:C>:-O3;-DNDEBUG>
    $<$<COMPILE_LANGUAGE:CXX>:-O3;-DNDEBUG;-ffp-contract=off>
)

if (NOT WIN32)
    target_link_libraries(riverfinder PUBLIC m)
endif()
