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
# cubiomes.
#
# cubiomes is a nested submodule of this repository:
#
#   RiverFinderBase/cubiomes      (this checkout)
#
# A standalone build therefore needs no external source tree. CI or an
# embedding project that already owns a cubiomes checkout can still override
# the path:
#
#   -DRIVER_CUBIOMES_DIR=<path-to-cubiomes>
# ---------------------------------------------------------------------------
set(RIVER_CUBIOMES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cubiomes"
    CACHE PATH "Path to the cubiomes checkout")
if (NOT EXISTS "${RIVER_CUBIOMES_DIR}/generator.c")
    message(FATAL_ERROR
        "RIVER_CUBIOMES_DIR does not look like cubiomes: ${RIVER_CUBIOMES_DIR}\n"
        "Run 'git submodule update --init --recursive' in RiverFinderBase, "
        "or pass -DRIVER_CUBIOMES_DIR=<path-to-cubiomes>.")
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

# The C ABI header selects __declspec(dllimport/export) only for DLL builds.
# Both the static library and the CLI use the static branch.
target_compile_definitions(riverfinder PUBLIC RIVERFINDER_STATIC)

if (NOT MSVC)
    target_compile_options(riverfinder PRIVATE
        $<$<COMPILE_LANGUAGE:C>:-O3;-DNDEBUG>
        $<$<COMPILE_LANGUAGE:CXX>:-O3;-DNDEBUG;-ffp-contract=off>
    )
endif()

# ---------------------------------------------------------------------------
# Standalone command-line program.
#
# The executable is self-contained: it links the static library above, which
# already contains the cubiomes sources. It can be copied to another machine
# without the submodule or the DLL.
# ---------------------------------------------------------------------------
add_executable(riverfinder-cli
    river_cli.cpp
)
target_link_libraries(riverfinder-cli PRIVATE riverfinder)
set_target_properties(riverfinder-cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

if (MINGW)
    target_link_options(riverfinder-cli PRIVATE
        -static
        -static-libgcc
        -static-libstdc++
    )
endif()

if (NOT WIN32)
    target_link_libraries(riverfinder PUBLIC m)
endif()
