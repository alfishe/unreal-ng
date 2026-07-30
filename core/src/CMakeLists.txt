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

# zstd (system-installed; used by TTD codec for RAM page compression).
# The official zstd cmake config exports `zstd::libzstd` with
# INTERFACE_INCLUDE_DIRECTORIES. We propagate that path via the core
# target's PUBLIC target_include_directories below so that consumers
# using either target_link_libraries(core) OR
# $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES> see zstd.h.
#
# CRITICAL: do NOT use directory-scoped include_directories(${_zstd_iface_inc})
# here — that would pollute every add_subdirectory under core/, including
# core/automation/webapi/lib/jsoncpp, which would then resolve `json/reader.h`
# to the Homebrew-installed jsoncpp (/opt/homebrew/include/json/reader.h)
# instead of the vendored 1.9.5 headers, breaking the build.
find_package(zstd REQUIRED)
message(STATUS "zstd found: ${zstd_VERSION}")
get_target_property(_zstd_iface_inc zstd::libzstd INTERFACE_INCLUDE_DIRECTORIES)

# All C++ sources in /src and sub-folders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "liblzma/" FALSE OUTVAR CPP_FILES) # Has own CMakeLists.txt
message("CPP_FILES:${CPP_FILES}")

set (SOURCES ${CPP_FILES})

# Include Objective-C++ files on macOS natively
if (APPLE)
    file (GLOB_RECURSE MM_FILES ${SOURCE_DIR}/*.mm)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/build/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/CMakeFiles/" FALSE OUTVAR MM_FILES)
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
    ${_zstd_iface_inc}
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
# For static libraries consumed externally, framework deps MUST be PUBLIC
# so that external linkers inherit them transitively.
if (APPLE)
    find_library(FOUNDATION_LIB Foundation REQUIRED)
    find_library(COREFOUNDATION_LIB CoreFoundation REQUIRED)
    find_library(AVFOUNDATION_LIB AVFoundation REQUIRED)
    find_library(COREMEDIA_LIB CoreMedia REQUIRED)
    find_library(COREVIDEO_LIB CoreVideo REQUIRED)
    find_library(VIDEOTOOLBOX_LIB VideoToolbox REQUIRED)
    find_library(AUDIOTOOLBOX_LIB AudioToolbox REQUIRED)

    target_link_libraries(${PROJECT_NAME} PUBLIC
        ${FOUNDATION_LIB}
        ${COREFOUNDATION_LIB}
        ${AVFOUNDATION_LIB}
        ${COREMEDIA_LIB}
        ${COREVIDEO_LIB}
        ${VIDEOTOOLBOX_LIB}
        ${AUDIOTOOLBOX_LIB}
    )
endif ()

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif ()

if (WIN32 AND MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++ Shlwapi ws2_32 d3d11 dxgi)
endif()

if (WIN32 AND MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE Shlwapi ws2_32 d3d11 dxgi)
endif()
