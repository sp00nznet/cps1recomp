cmake_minimum_required(VERSION 3.16)
project(cps1recomp C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(CPS1RECOMP_DEBUG "Enable debug tracing and breakpoints" OFF)

# SDL2
find_package(SDL2 REQUIRED)

# --- Genesis-Plus-GX Z80 core (from genrecomp submodule) ---
# Adjust this path to point to your local genrecomp or provide Z80 sources
# Try multiple possible locations for external deps
set(Z80_DIR_CANDIDATES
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/z80
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../gen/genrecomp/ext/Genesis-Plus-GX/core/z80
    D:/recomp/gen/genrecomp/ext/Genesis-Plus-GX/core/z80
)
set(Z80_DIR "")
foreach(dir ${Z80_DIR_CANDIDATES})
    if(EXISTS ${dir}/z80.c)
        set(Z80_DIR ${dir})
        break()
    endif()
endforeach()
if(Z80_DIR STREQUAL "")
    set(Z80_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/z80)  # fallback
endif()

# Z80 sound CPU: built in from the ported z80recomp interpreter
# (src/z80/z80_cpu.c, z80_decode.c, z80_interp.c — driven by src/z80.c).
# The external GenPlusGX/z80ex path below is no longer used.
set(HAVE_Z80_CORE FALSE)
message(STATUS "Z80 core: interpreter (ported from z80recomp)")

# --- ymfm library (for YM2151) ---
set(YMFM_DIR_CANDIDATES
    ${CMAKE_CURRENT_SOURCE_DIR}/ext/ymfm/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../gen/genrecomp/ext/ymfm/src
    D:/recomp/gen/genrecomp/ext/ymfm/src
)
set(YMFM_DIR "")
foreach(dir ${YMFM_DIR_CANDIDATES})
    if(EXISTS ${dir}/ymfm_opm.cpp)
        set(YMFM_DIR ${dir})
        break()
    endif()
endforeach()
if(YMFM_DIR STREQUAL "")
    set(YMFM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext/ymfm/src)
endif()

if(EXISTS ${YMFM_DIR}/ymfm_opm.cpp)
    add_library(ymfm STATIC
        ${YMFM_DIR}/ymfm_opm.cpp
        ${YMFM_DIR}/ymfm_fm.ipp
    )
    target_include_directories(ymfm PUBLIC ${YMFM_DIR})
    if(MSVC)
        target_compile_options(ymfm PRIVATE /W0)
    else()
        target_compile_options(ymfm PRIVATE -w)
    endif()
    set(HAVE_YMFM TRUE)
    message(STATUS "Found ymfm at ${YMFM_DIR}")
else()
    set(HAVE_YMFM FALSE)
    message(WARNING "ymfm not found at ${YMFM_DIR} — YM2151 will output silence")
endif()

# --- cps1recomp library ---
set(CPS1_SOURCES
    src/cps1recomp.c
    src/m68k.c
    src/bus.c
    src/func_table.c
    src/video.c
    src/palette.c
    src/io.c
    src/timer.c
    src/oki6295.c
    src/z80.c
    src/z80/z80_cpu.c
    src/z80/z80_decode.c
    src/z80/z80_interp.c
    src/rom.c
    src/platform.c
    src/debug.c
)

# YM2151: use ymfm C++ bridge if available, otherwise C stub
if(HAVE_YMFM)
    list(APPEND CPS1_SOURCES src/ym2151_ymfm.cpp)
else()
    list(APPEND CPS1_SOURCES src/ym2151.c)
endif()

add_library(cps1recomp STATIC ${CPS1_SOURCES})

target_include_directories(cps1recomp
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(cps1recomp
    PUBLIC SDL2::SDL2
)

if(HAVE_Z80_CORE)
    target_link_libraries(cps1recomp PRIVATE z80_core)
    target_include_directories(cps1recomp PRIVATE ${Z80_DIR} ${Z80_DIR}/..)
endif()

if(HAVE_YMFM)
    target_link_libraries(cps1recomp PRIVATE ymfm)
    target_include_directories(cps1recomp PRIVATE ${YMFM_DIR})
endif()

if(CPS1RECOMP_DEBUG)
    target_compile_definitions(cps1recomp PUBLIC CPS1RECOMP_DEBUG=1)
endif()

if(MSVC)
    target_compile_options(cps1recomp PRIVATE /W4)
else()
    target_compile_options(cps1recomp PRIVATE -Wall -Wextra)
endif()
