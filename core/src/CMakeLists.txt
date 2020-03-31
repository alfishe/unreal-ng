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

# Set paths
get_filename_component(ROOT_DIR ../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})



# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# Set include paths for compiler
include_directories(
        ${SOURCE_DIR}/
        ${SOURCE_DIR}/common
        ${SOURCE_DIR}/emulator
)


# All sources in /src and subfolders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
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
target_precompile_headers(${PROJECT_NAME} PRIVATE ${SOURCE_DIR}/stdafx.h)


