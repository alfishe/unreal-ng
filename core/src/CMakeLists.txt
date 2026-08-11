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
target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${SOURCE_DIR}/3rdparty
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/emulator
)


# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${SOURCE_DIR}/stdafx.h)


# Link lzma library
target_link_libraries(${PROJECT_NAME} PRIVATE lzma)

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
