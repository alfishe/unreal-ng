cmake_minimum_required(VERSION 3.16.0)

# Project named 'core' on C++
project(core LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

# Include CMake helper functions file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/func.cmake)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/common
    ${CMAKE_CURRENT_SOURCE_DIR}/src/emulator
)


# All sources in /src and subfolders will be compiled
file (GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set (SOURCES ${CPP_FILES})

message("CPP_FILES:${CPP_FILES}")

# Produce static library
add_library(${PROJECT_NAME} STATIC ${CPP_FILES})

# Declare _DEBUG symbol for compiler
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/stdafx.h)


