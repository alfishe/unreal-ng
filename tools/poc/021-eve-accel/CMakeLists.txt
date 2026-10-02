# eve-accel - proof-of-concept experiments on accelerating the FT812 renderer (eve-emu).
#
# Builds the vendored eve-emu (core/src/3rdparty/eve-emu, unchanged) in several variants.
# A variant is the vendored src/ with some files replaced by the experiment's own copies
# ("overlays"): every file is copied into the build tree, so a changed header is seen by
# every source of that variant. The vendored copy itself is never modified.
#
#   cmake -S tools/poc/021-eve-accel -B tools/poc/021-eve-accel/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   JOBS=$(( $(sysctl -n hw.ncpu) / 2 ))
#   ninja -C tools/poc/021-eve-accel/build -j "$JOBS"
cmake_minimum_required(VERSION 3.16)
project(eve-accel LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(EVE_VENDORED ${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src/3rdparty/eve-emu)
get_filename_component(EVE_VENDORED ${EVE_VENDORED} ABSOLUTE)
set(EVE_COMMON ${CMAKE_CURRENT_SOURCE_DIR}/common)

set(EVE_LIB_SOURCES
    eve-chip.cpp eve-spi.cpp eve-memory.cpp eve-registers.cpp eve-timing.cpp eve-dl.cpp
    eve-aa-table.cpp eve-raster.cpp eve-bitmap.cpp eve-pixel.cpp eve-output.cpp eve-copro.cpp
    eve-copro-mem.cpp eve-copro-matrix.cpp eve-copro-text.cpp eve-copro-media.cpp eve-audio.cpp
    eve-state.cpp eve-debug.cpp eve-decoders.cpp)

# eve_variant(<name> [OVERLAY_DIR dir] [EXTRA src...] [DEFINES d...] [SIMD ON|OFF])
#   Builds eve-lib-<name> and eve-replay-<name>. Files in OVERLAY_DIR replace the vendored
#   files of the same name; EXTRA are additional library sources (paths relative to the
#   overlay dir or absolute).
function(eve_variant name)
    cmake_parse_arguments(V "" "OVERLAY_DIR;SIMD" "EXTRA;DEFINES" ${ARGN})
    set(dst ${CMAKE_BINARY_DIR}/src-${name})
    file(GLOB vendored_files RELATIVE ${EVE_VENDORED}/src ${EVE_VENDORED}/src/*)
    foreach(f ${vendored_files})
        if(V_OVERLAY_DIR AND EXISTS ${V_OVERLAY_DIR}/${f})
            configure_file(${V_OVERLAY_DIR}/${f} ${dst}/${f} COPYONLY)
        else()
            configure_file(${EVE_VENDORED}/src/${f} ${dst}/${f} COPYONLY)
        endif()
    endforeach()
    if(V_OVERLAY_DIR)
        # The overlay's own headers (files the vendored tree does not have)
        file(GLOB overlay_headers RELATIVE ${V_OVERLAY_DIR} ${V_OVERLAY_DIR}/*.h)
        foreach(f ${overlay_headers})
            configure_file(${V_OVERLAY_DIR}/${f} ${dst}/${f} COPYONLY)
        endforeach()
    endif()
    set(sources)
    foreach(f ${EVE_LIB_SOURCES})
        list(APPEND sources ${dst}/${f})
    endforeach()
    foreach(f ${V_EXTRA})
        get_filename_component(base ${f} NAME)
        if(IS_ABSOLUTE ${f})
            configure_file(${f} ${dst}/${base} COPYONLY)
        else()
            configure_file(${V_OVERLAY_DIR}/${f} ${dst}/${base} COPYONLY)
        endif()
        list(APPEND sources ${dst}/${base})
    endforeach()
    set(vendor_sources ${dst}/eve-vendor-tinfl.c ${dst}/eve-vendor-stb.cpp)
    set_source_files_properties(${vendor_sources} PROPERTIES COMPILE_OPTIONS "-w")
    add_library(eve-lib-${name} STATIC ${sources} ${vendor_sources})
    target_include_directories(eve-lib-${name}
        PUBLIC ${EVE_VENDORED}/include ${EVE_COMMON}
        PRIVATE ${dst} ${EVE_VENDORED}/vendor ${EVE_VENDORED}/vendor/config)
    target_compile_definitions(eve-lib-${name} PUBLIC
        EVE_HAS_BUILTIN_INFLATE=1 EVE_HAS_BUILTIN_PNG=1 EVE_HAS_BUILTIN_JPEG=1 ${V_DEFINES})
    if(V_SIMD STREQUAL "OFF")
        target_compile_definitions(eve-lib-${name} PRIVATE EVE_NO_SIMD=1)
    endif()
    target_compile_options(eve-lib-${name} PRIVATE -ffp-contract=off)
    find_package(Threads REQUIRED)
    target_link_libraries(eve-lib-${name} PUBLIC Threads::Threads)

    add_executable(eve-replay-${name} ${EVE_COMMON}/eve-replay.cpp)
    target_link_libraries(eve-replay-${name} PRIVATE eve-lib-${name})
endfunction()

# 01: the vendored library exactly as it is (plus the empty POC hooks)
eve_variant(base EXTRA ${EVE_COMMON}/eve-poc-stub.cpp)
eve_variant(scalar SIMD OFF EXTRA ${EVE_COMMON}/eve-poc-stub.cpp)

# Every experiment folder that builds something has a variants.cmake (in folder order)
file(GLOB experiment_files RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/*/variants.cmake)
list(SORT experiment_files)
foreach(f ${experiment_files})
    include(${CMAKE_CURRENT_SOURCE_DIR}/${f})
endforeach()
