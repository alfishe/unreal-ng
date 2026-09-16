cmake_minimum_required(VERSION 3.16.0)

message(">> core/src CMakeLists.txt")

# Project named 'core' on C++
project(core LANGUAGES CXX)

# Enable Objective-C++ on Apple platforms (for VideoToolbox encoder)
if(APPLE)
    enable_language(OBJCXX)
endif()

# Note: Shared memory mapping is now controlled at runtime via the 'sharedmemory' feature flag
# (see FeatureManager). The old compile-time options ENABLE_MEMORY_MAPPING, USE_SHAREDMEM_MAPPING,
# and USE_FILE_MAPPING have been removed.

# C++ standard is set by the root CMakeLists.txt (C++20 for MSVC, C++17 for others)
# Do not override here to maintain cross-platform compatibility

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # GCC-specific flags
    add_compile_options(-Wall -Wextra -Werror -Wno-format-security)

    # Treat specific warnings as errors
    add_compile_options(-Werror=unused-result -Werror=format-security)

    # Disable specific warnings if needed
    add_compile_options(-Wno-error=deprecated-declarations)  # Example for disabling specific warnings
elseif (CMAKE_CXX_COMPILER_ID MATCHES "AppleClang" OR CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    # Clang-specific flags
    add_compile_options(-Wall -Wextra -Werror)
endif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")

# Set paths
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# liblzma
add_subdirectory(3rdparty/liblzma)

# zstd v1.5.7 (embedded source — no FetchContent, no find_package).
# The CMake project in 3rdparty/zstd/ compiles the checked-in source
# tree under 3rdparty/zstd/lib/ directly into libzstd_static and
# exposes the `zstd::libzstd` alias target.
#
# Why embed: see 3rdparty/zstd/README.md. The TTD codec depends on zstd
# at the .ttd on-disk format protocol level, so we cannot ship without
# a known-pinned version. The C++ codec PoC (tools/poc/cpp/) confirmed
# zstd-1 remains optimal for the TTD workload in v2 — see
# docs/inprogress/2026-07-19-time-travel/phase-5-codec-poc-results.md.
#
# CRITICAL: do NOT use directory-scoped include_directories() here — that
# would pollute every add_subdirectory under core/, including
# core/automation/webapi/lib/jsoncpp, which would then resolve
# `json/reader.h` to the Homebrew-installed jsoncpp
# (/opt/homebrew/include/json/reader.h) instead of the vendored 1.9.5
# headers, breaking the build. The libzstd_static target propagates its
# own include dir via INTERFACE_INCLUDE_DIRECTORIES, so we don't need to
# forward it manually.
add_subdirectory(3rdparty/zstd)

# libopl4 — MoonSound (YMF278B / OPL4) synthesis library.
# TEMPORARY home: tools/poc/015-opl4-synthesis; migrates to core/src/3rdparty
# once the integration stabilises. Declared here — not in the root file — so
# every entry point that builds core (root, unreal-qt, unreal-videowall,
# core/tests standalone) picks the target up exactly once; the guard also
# protects the -DBUILD_POC=ON path, where tools/poc/CMakeLists.txt adds the
# same directory and must skip it (see the matching guard there).
# EXCLUDE_FROM_ALL keeps the opl4tests research binary out of default
# builds; the opl4 library itself is still compiled as a dependency of core.
if (NOT TARGET opl4)
    add_subdirectory(${ROOT_DIR}/tools/poc/015-opl4-synthesis 015-opl4-synthesis EXCLUDE_FROM_ALL)
endif()

# All C++ sources in /src and sub-folders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "liblzma/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/recording/" FALSE OUTVAR CPP_FILES) # Moved to separate library

# ymfm (YM2203 core for the TurboSound FM device): vendored at 3rdparty/ymfm
# and picked up by the recursive *.cpp glob above. ymfm_opn.cpp includes
# ymfm_fm.ipp (template implementations), so the .ipp never compiles
# standalone. The vendored .cpp files are not warning-clean under our
# -Wall -Wextra -Werror (upstream keeps unused parameters) and must not be
# force-fed stdafx.h, so skip the PCH and relax that single warning on those
# files only. See 3rdparty/ymfm/PATCHES.md for the local source patch.
file (GLOB YMFM_CPP ${SOURCE_DIR}/3rdparty/ymfm/*.cpp)
set_source_files_properties(${YMFM_CPP} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set_source_files_properties(${YMFM_CPP} PROPERTIES COMPILE_OPTIONS "-Wno-unused-parameter")
endif()
message("CPP_FILES:${CPP_FILES}")

set (SOURCES ${CPP_FILES})

# Include Objective-C++ files on macOS natively
if (APPLE)
    file (GLOB_RECURSE MM_FILES ${SOURCE_DIR}/*.mm)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/build/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/CMakeFiles/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/recording/" FALSE OUTVAR MM_FILES) # Moved to separate library
    set_source_files_properties(${MM_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    list(APPEND SOURCES ${MM_FILES})
endif()

message("SOURCES:${SOURCES}")

# Produce static library;  Create unrealng::core alias for the target
add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Declare _DEBUG symbol for compiler
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

# Recording support - if the option is set by parent CMake, define the symbol, add include path, and link
if (ENABLE_RECORDING)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_RECORDING)
    target_include_directories(${PROJECT_NAME} PUBLIC ${ROOT_DIR}/core/recording/src)
    # Build recording library if not already built by parent
    if (NOT TARGET unrealng::recording)
        add_subdirectory("${ROOT_DIR}/core/recording" "${CMAKE_CURRENT_BINARY_DIR}/recording")
    endif()
    # Link to recording so linker resolves RecordingManager symbols
    target_link_libraries(${PROJECT_NAME} PUBLIC unrealng::recording)
endif()

# Set include paths for compiler

# Set include paths for compiler.
# zstd's include dir is added PUBLIC so:
#   - Direct linkers (target_link_libraries(... core)) inherit it.
#   - Sibling targets that use
#     $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES> (e.g.
#     automation-cli, automation-lua, automation-webapi) also see it.
# This is the only correct way to propagate a system include path without
# polluting the global include search order (which would shadow vendored
# headers like jsoncpp's).
target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${SOURCE_DIR}/3rdparty
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/emulator
    # Generated build fingerprint header (buildinfo.h) - configured by the
    # root CMakeLists.txt from git metadata. PUBLIC so every consumer of
    # core (automation-webapi/mcp/cli, tests, Qt apps) can
    # #include "emulator/buildinfo.h".
    ${CMAKE_BINARY_DIR}/generated
)

# ymfm include dir: SYSTEM so warnings originating in the upstream headers
# never surface in our translation units (-Werror). PUBLIC so consumers
# (core-tests, benchmarks, the future TSFM device) inherit it through the
# core link and can #include <ymfm_opn.h> flat
target_include_directories(${PROJECT_NAME} SYSTEM PUBLIC ${SOURCE_DIR}/3rdparty/ymfm)


# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${SOURCE_DIR}/stdafx.h)


# Link lzma library
target_link_libraries(${PROJECT_NAME} PRIVATE lzma)

# Link zstd (TTD codec)
# PUBLIC because ttd_compression.h is a public header (included by
# ttd_codec_page_store.h → timetravelmanager.h → consumed by unreal-qt,
# automation/lua, automation/webapi, tests). All consumers need zstd's
# include path available.
target_link_libraries(${PROJECT_NAME} PUBLIC $<TARGET_NAME:zstd::libzstd>)

# Link libopl4 (MoonSound / YMF278B synthesis). PRIVATE: only core sources
# include opl4 headers or call the library — public headers such as
# soundmanager.h only forward-declare the device class. The static-archive
# dependency still reaches final executables (CMake's $<LINK_ONLY:>
# propagation), so unreal-qt / automation binaries resolve opl4 symbols
# without inheriting the PoC include path.
target_link_libraries(${PROJECT_NAME} PRIVATE opl4)

# Compile gate for the MoonSound device implementation. PUBLIC: core-tests
# compiles core sources directly into its own binary and must see the same
# gate as the core library build (a mismatch would be an ODR hazard).
target_compile_definitions(${PROJECT_NAME} PUBLIC UNREALNG_HAVE_OPL4=1)

# Mirror the libopl4 FM backend selection (tools/poc CMake option
# OPL4_FM_BACKEND=ymfm) as a PUBLIC define: opl4 is linked PRIVATE, so the
# option's own PUBLIC define stops at core's translation units, but
# core-tests compiles the core sources (and test helpers that program the
# reference FM voice) directly and must match the selected register map.
if (OPL4_FM_BACKEND STREQUAL "ymfm")
    target_compile_definitions(${PROJECT_NAME} PUBLIC OPL4_FM_YMFM=1)
endif()

# Platform-specific framework and library linkage
# Note: Recording-specific frameworks (VideoToolbox, AVFoundation, CoreMedia, CoreVideo, AudioToolbox)
# are now linked via the recording library (core/recording/CMakeLists.txt)
if (APPLE)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${FOUNDATION_LIB}
        ${COREFOUNDATION_LIB}
    )
endif ()

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif ()

# Note: Recording-specific Windows libs (d3d11, dxgi, mfplat, mfuuid, mf, ole32)
# are now linked via the recording library (core/recording/CMakeLists.txt)
if (WIN32 AND MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++ Shlwapi ws2_32)
endif()

if (WIN32 AND MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE Shlwapi ws2_32)
endif()
