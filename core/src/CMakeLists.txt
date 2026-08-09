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

# All C++ sources in /src and sub-folders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "liblzma/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/recording/" FALSE OUTVAR CPP_FILES) # Moved to separate library
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

# Recording support - if the option is set by parent CMake, define the symbol and add include path
if (ENABLE_RECORDING)
    target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_RECORDING)
    target_include_directories(${PROJECT_NAME} PUBLIC ${ROOT_DIR}/core/recording/src)
endif()

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
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/emulator
)


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
