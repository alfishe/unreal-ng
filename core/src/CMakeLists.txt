cmake_minimum_required(VERSION 3.16.0)

message(">> core/src CMakeLists.txt")

# Project named 'core' on C++
project(core LANGUAGES CXX)

# Emulated system memory mapping options
option(ENABLE_MEMORY_MAPPING "Enable memory-mapped file support fpr emulated system RAM" ON)
option(USE_SHAREDMEM_MAPPING "Use shared memory (shm_open) for memory mapping" ON)
option(USE_FILE_MAPPING "Use file-based memory mapping" OFF)


# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

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
message("CPP_FILES:${CPP_FILES}")

set (SOURCES ${CPP_FILES})
message("SOURCES:${SOURCES}")

# Produce static library;  Create unrealng::core alias for the target
add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Declare _DEBUG symbol for compiler
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

# Pass option to the compiler
if (ENABLE_MEMORY_MAPPING)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_MEMORY_MAPPING)

    if (USE_SHAREDMEM_MAPPING)
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_SHAREDMEM_MAPPING)
    endif ()

    if (USE_FILE_MAPPING)
        target_compile_definitions(${PROJECT_NAME} PRIVATE USE_FILE_MAPPING)
    endif ()
endif ()

# Set include paths for compiler
target_include_directories(${PROJECT_NAME} PUBLIC
    ${SOURCE_DIR}/
    ${SOURCE_DIR}/common
    ${SOURCE_DIR}/emulator
)


# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${SOURCE_DIR}/stdafx.h)


# Link lzma library
target_link_libraries(${PROJECT_NAME} PRIVATE lzma)

# Link uuid library (Linux/Unix)
if (UNIX AND NOT APPLE)
    find_package(PkgConfig QUIET)
    if (PKG_CONFIG_FOUND)
        pkg_check_modules(UUID uuid)
        if (UUID_FOUND)
            target_include_directories(${PROJECT_NAME} PRIVATE ${UUID_INCLUDE_DIRS})
            target_link_libraries(${PROJECT_NAME} PRIVATE ${UUID_LIBRARIES})
        else ()
            target_link_libraries(${PROJECT_NAME} PRIVATE uuid)
        endif ()
    else ()
        target_link_libraries(${PROJECT_NAME} PRIVATE uuid)
    endif ()
endif ()

# Link all dependencies into static lib
if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
    set(common_libraries)
endif (APPLE)

if (UNIX AND NOT APPLE)
    set(common_libraries -static-libstdc++)
endif (UNIX AND NOT APPLE)

if (WIN32)
    set(common_libraries -static-libstdc++ Shlwapi ws2_32)
endif()

# Link all components into target binary
target_link_libraries(
    ${PROJECT_NAME} PRIVATE

    ${common_libraries}
)
