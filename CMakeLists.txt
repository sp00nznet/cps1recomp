cmake_minimum_required(VERSION 3.16)
project(cps1recomp C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(CPS1RECOMP_DEBUG "Enable debug tracing and breakpoints" OFF)

# SDL2
find_package(SDL2 REQUIRED)

# --- cps1recomp library ---
add_library(cps1recomp STATIC
    src/cps1recomp.c
    src/m68k.c
    src/bus.c
    src/func_table.c
    src/video.c
    src/palette.c
    src/io.c
    src/timer.c
    src/ym2151.c
    src/oki6295.c
    src/z80.c
    src/rom.c
    src/platform.c
    src/debug.c
)

target_include_directories(cps1recomp
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cps1recomp
    PUBLIC SDL2::SDL2
)

if(CPS1RECOMP_DEBUG)
    target_compile_definitions(cps1recomp PUBLIC CPS1RECOMP_DEBUG=1)
endif()

# Suppress specific MSVC warnings for clean builds
if(MSVC)
    target_compile_options(cps1recomp PRIVATE /W4)
else()
    target_compile_options(cps1recomp PRIVATE -Wall -Wextra)
endif()
