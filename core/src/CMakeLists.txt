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
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})


# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}
)


# All sources in /src and subfolders will be compiled
file (GLOB_RECURSE CORE_CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CORE_CPP_FILES}" "/build/" FALSE OUTVAR CORE_CPP_FILES) # Exclude /core/src/build folder
set (SOURCES ${CORE_CPP_FILES})

message("SOURCES:${SOURCES}")

# Produce static library
add_library(${PROJECT_NAME} STATIC ${SOURCES})

# 'stdafx.h' is our precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stdafx.h)


