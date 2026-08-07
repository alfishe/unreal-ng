cmake_minimum_required(VERSION 3.16)
project(recording LANGUAGES CXX)

# Enable Objective-C++ on Apple platforms (for VideoToolbox encoder)
if(APPLE)
    enable_language(OBJCXX)
endif()

# C++20 required - must match core library for ABI compatibility
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

# Set paths
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# Recording is enabled by default when this subdirectory is added
option(ENABLE_RECORDING "Enable audio/video recording support" ON)

if(NOT ENABLE_RECORDING)
    message(STATUS "Recording disabled - skipping ${PROJECT_NAME} library")
    return()
endif()

# Collect all source files
file(GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)

set(SOURCES ${CPP_FILES})

# Include Objective-C++ files on macOS
if(APPLE)
    file(GLOB_RECURSE MM_FILES ${SOURCE_DIR}/*.mm)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/build/" FALSE OUTVAR MM_FILES)
    EXCLUDE_FILES_FROM_DIR_IN_LIST("${MM_FILES}" "/CMakeFiles/" FALSE OUTVAR MM_FILES)
    set_source_files_properties(${MM_FILES} PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    list(APPEND SOURCES ${MM_FILES})
endif()

message(STATUS "Recording SOURCES: ${SOURCES}")

# Create static library with alias
add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Set include directories
target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${ROOT_DIR}/core/src/           # For emulator headers (EmulatorContext, etc.)
    ${ROOT_DIR}/core/src/common
    ${ROOT_DIR}/core/src/emulator
)

# Define ENABLE_RECORDING for code that includes recording headers
target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_RECORDING)

# Debug configuration
if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif()

# Platform-specific framework and library linkage
if(APPLE)
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
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++)
endif()

if(WIN32 AND MINGW)
    target_link_libraries(${PROJECT_NAME} PRIVATE -static-libstdc++ d3d11 dxgi mfplat mfuuid mf ole32)
endif()

if(WIN32 AND MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE d3d11 dxgi mfplat mfuuid mf ole32)
endif()

# <Diagnostics>
message(STATUS "<${PROJECT_NAME} diagnostics>")
message(STATUS "  ENABLE_RECORDING: ${ENABLE_RECORDING}")
message(STATUS "  SOURCE_DIR: ${SOURCE_DIR}")
message(STATUS "  SOURCES count: ")
list(LENGTH SOURCES SOURCES_COUNT)
message(STATUS "    ${SOURCES_COUNT} files")
message(STATUS "</${PROJECT_NAME} diagnostics>")
# </Diagnostics>

message("<< <${PROJECT_NAME}>")
