# eve-emu - FT812 emulator library.
#
# Stand-alone:
#   cmake -S . -B build -G Ninja
#   ninja -C build -j <half the cores>
#   ./build/eve-tests
#
# Inside a host project:
#   add_subdirectory(<path-to-eve-emu>)
#   target_link_libraries(<target> PRIVATE eve::emu)

cmake_minimum_required(VERSION 3.16)
project(eve-emu VERSION 0.1.0 LANGUAGES CXX C)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(EVE_TOP_LEVEL ON)
else()
    set(EVE_TOP_LEVEL OFF)
endif()

set(EVE_DECODER_INFLATE "BUILTIN" CACHE STRING "zlib inflate: BUILTIN or EXTERNAL")
set(EVE_DECODER_PNG     "BUILTIN" CACHE STRING "PNG: BUILTIN or EXTERNAL")
set(EVE_DECODER_JPEG    "BUILTIN" CACHE STRING "baseline JPEG: BUILTIN or EXTERNAL")
set_property(CACHE EVE_DECODER_INFLATE EVE_DECODER_PNG EVE_DECODER_JPEG
             PROPERTY STRINGS BUILTIN EXTERNAL)
option(EVE_BUILD_TESTS "Build eve-emu tests" ${EVE_TOP_LEVEL})
option(EVE_BUILD_BENCHMARKS "Build eve-emu benchmarks" OFF)
option(EVE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in eve-emu sources" ON)
option(EVE_BUILD_TOOLS "Build eve-emu tools (eve-replay)" ${EVE_TOP_LEVEL})
option(EVE_SIMD "Use the NEON / SSE2 kernels (OFF: the plain C++ fallback, same results)" ON)

foreach(kind INFLATE PNG JPEG)
    if(NOT EVE_DECODER_${kind} STREQUAL "BUILTIN" AND NOT EVE_DECODER_${kind} STREQUAL "EXTERNAL")
        message(FATAL_ERROR "EVE_DECODER_${kind} must be BUILTIN or EXTERNAL, not '${EVE_DECODER_${kind}}'")
    endif()
endforeach()

if(EVE_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_BUILD_TYPE Release)
    endif()
endif()

# --- Warning flags for the library's own sources and its tests --------------------------
set(EVE_WARNING_FLAGS)
if(MSVC)
    list(APPEND EVE_WARNING_FLAGS /W4)
    if(EVE_WARNINGS_AS_ERRORS)
        list(APPEND EVE_WARNING_FLAGS /WX)
    endif()
else()
    list(APPEND EVE_WARNING_FLAGS -Wall -Wextra -Wshadow -Wconversion)
    if(EVE_WARNINGS_AS_ERRORS)
        list(APPEND EVE_WARNING_FLAGS -Werror)
    endif()
endif()

# No fused multiply-add in the library: the same floating-point results (antialiasing
# coverage) on every platform. MSVC does not contract by default.
set(EVE_FP_FLAGS)
if(NOT MSVC)
    list(APPEND EVE_FP_FLAGS -ffp-contract=off)
endif()

# Vendored code is compiled with its warnings off: it is not ours to fix.
if(MSVC)
    set(EVE_VENDOR_FLAGS /W0)
else()
    set(EVE_VENDOR_FLAGS -w)
endif()

set(EVE_SOURCES
    src/eve-chip.cpp
    src/eve-spi.cpp
    src/eve-memory.cpp
    src/eve-registers.cpp
    src/eve-timing.cpp
    src/eve-dl.cpp
    src/eve-aa-table.cpp
    src/eve-raster.cpp
    src/eve-bitmap.cpp
    src/eve-pixel.cpp
    src/eve-output.cpp
    src/eve-copro.cpp
    src/eve-copro-mem.cpp
    src/eve-copro-matrix.cpp
    src/eve-copro-text.cpp
    src/eve-copro-media.cpp
    src/eve-audio.cpp
    src/eve-state.cpp
    src/eve-debug.cpp
    src/eve-decoders.cpp
    src/eve-profile.cpp
)

set(EVE_VENDOR_SOURCES)
if(EVE_DECODER_INFLATE STREQUAL "BUILTIN")
    list(APPEND EVE_VENDOR_SOURCES src/eve-vendor-tinfl.c)
endif()
if(EVE_DECODER_PNG STREQUAL "BUILTIN" OR EVE_DECODER_JPEG STREQUAL "BUILTIN")
    list(APPEND EVE_VENDOR_SOURCES src/eve-vendor-stb.cpp)
endif()
set_source_files_properties(${EVE_VENDOR_SOURCES} PROPERTIES COMPILE_OPTIONS "${EVE_VENDOR_FLAGS}")

add_library(eve-emu STATIC ${EVE_SOURCES} ${EVE_VENDOR_SOURCES})
add_library(eve::emu ALIAS eve-emu)
target_include_directories(eve-emu
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/vendor
            ${CMAKE_CURRENT_SOURCE_DIR}/vendor/config)
target_compile_features(eve-emu PUBLIC cxx_std_17)
if(NOT EVE_SIMD)
    target_compile_definitions(eve-emu PRIVATE EVE_NO_SIMD=1)
endif()
target_compile_definitions(eve-emu PUBLIC
    EVE_HAS_BUILTIN_INFLATE=$<STREQUAL:${EVE_DECODER_INFLATE},BUILTIN>
    EVE_HAS_BUILTIN_PNG=$<STREQUAL:${EVE_DECODER_PNG},BUILTIN>
    EVE_HAS_BUILTIN_JPEG=$<STREQUAL:${EVE_DECODER_JPEG},BUILTIN>)
foreach(src ${EVE_SOURCES})
    set_property(SOURCE ${src} APPEND PROPERTY COMPILE_OPTIONS ${EVE_WARNING_FLAGS} ${EVE_FP_FLAGS})
endforeach()
set_target_properties(eve-emu PROPERTIES CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE ON)

# --- Symbol check: the archive exports only Eve* symbols ---------------------------------
if(NOT MSVC)
    find_program(EVE_NM_TOOL NAMES ${CMAKE_NM} nm)
    if(EVE_NM_TOOL)
        add_custom_target(eve-check-symbols
            COMMAND ${CMAKE_COMMAND} -DNM=${EVE_NM_TOOL} -DARCHIVE=$<TARGET_FILE:eve-emu>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckSymbols.cmake
            DEPENDS eve-emu
            COMMENT "Checking that eve-emu exports only Eve* symbols")
    endif()
endif()

# --- Tests and benchmarks ---------------------------------------------------------------
if(EVE_BUILD_TESTS)
    if(NOT TARGET gtest)
        set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
        set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        add_subdirectory(lib/googletest EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(tests)
endif()

if(EVE_BUILD_BENCHMARKS)
    if(NOT TARGET benchmark::benchmark)
        set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
        set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
        add_subdirectory(lib/benchmark EXCLUDE_FROM_ALL)
    endif()
    add_subdirectory(benchmarks)
endif()

# --- Tools ------------------------------------------------------------------------------
# eve-replay: replays an .evr bus capture, checks every answer and picture, measures speed
if(EVE_BUILD_TOOLS)
    add_executable(eve-replay tools/replay/eve-replay.cpp)
    target_link_libraries(eve-replay PRIVATE eve::emu)
    target_compile_options(eve-replay PRIVATE ${EVE_WARNING_FLAGS})

    # eve-replay-profile: eve-replay on a library built with the profiling counters
    # (EVE_PROFILE, src/eve-profile.h): what a capture's frames cost by bitmap format,
    # filter and path. Not part of the default build
    add_library(eve-emu-profile STATIC EXCLUDE_FROM_ALL ${EVE_SOURCES} ${EVE_VENDOR_SOURCES})
    target_include_directories(eve-emu-profile
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/src
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/vendor ${CMAKE_CURRENT_SOURCE_DIR}/vendor/config)
    target_compile_features(eve-emu-profile PUBLIC cxx_std_17)
    if(NOT EVE_SIMD)
        target_compile_definitions(eve-emu-profile PRIVATE EVE_NO_SIMD=1)
    endif()
    target_compile_definitions(eve-emu-profile PUBLIC EVE_PROFILE=1
        EVE_HAS_BUILTIN_INFLATE=$<STREQUAL:${EVE_DECODER_INFLATE},BUILTIN>
        EVE_HAS_BUILTIN_PNG=$<STREQUAL:${EVE_DECODER_PNG},BUILTIN>
        EVE_HAS_BUILTIN_JPEG=$<STREQUAL:${EVE_DECODER_JPEG},BUILTIN>)
    set_target_properties(eve-emu-profile PROPERTIES CXX_EXTENSIONS OFF)
    add_executable(eve-replay-profile EXCLUDE_FROM_ALL tools/replay/eve-replay.cpp)
    target_link_libraries(eve-replay-profile PRIVATE eve-emu-profile)
    target_compile_options(eve-replay-profile PRIVATE ${EVE_WARNING_FLAGS})
    # eve-tests-profile: the golden cases on the library with the profiling counters (not
    # built by default): per case, the pixels the fast paths left to the general path
    if(TARGET eve-test-images)
        add_executable(eve-tests-profile EXCLUDE_FROM_ALL tests/eve-golden_test.cpp tests/eve-test-host.cpp)
        target_link_libraries(eve-tests-profile PRIVATE eve-emu-profile eve-test-images gtest_main)
        target_include_directories(eve-tests-profile PRIVATE ${PROJECT_SOURCE_DIR}/tests ${PROJECT_SOURCE_DIR}/src)
        target_compile_options(eve-tests-profile PRIVATE ${EVE_WARNING_FLAGS})
        target_compile_definitions(eve-tests-profile PRIVATE
            EVE_TESTDATA_DIR="${PROJECT_SOURCE_DIR}/testdata"
            EVE_TEST_OUTPUT_DIR="${PROJECT_BINARY_DIR}/profile")
        set_target_properties(eve-tests-profile PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
    endif()
endif()
