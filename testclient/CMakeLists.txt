cmake_minimum_required(VERSION 3.16.0)

# Project named 'testclient' on C++
project(testclient LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

# Diagnostics
message("CMAKE_BUILD_TYPE: '${CMAKE_BUILD_TYPE}'")

# Include CMake helper functions file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/func.cmake)

include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)


file (GLOB_RECURSE CPP_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)
set (SOURCES ${CPP_FILES})

message("CPP_FILES:${CPP_FILES}")


# Produce executable
add_executable(${PROJECT_NAME} ${CPP_FILES})


# Resolve core library path
get_filename_component(CORE_LIBRARY_PATH
                       "${CMAKE_CURRENT_LIST_DIR}/../core"
                       ABSOLUTE)
message("CORE_LIBRARY_PATH: ${CORE_LIBRARY_PATH}")

# Add core library includes path for testclient
target_include_directories(${PROJECT_NAME} PRIVATE ${CORE_LIBRARY_PATH}/src)

# All standard libraries required
if (UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS " -static-libstdc++")
    set(common_libraries -static-libstdc++ stdc++fs pthread)
endif (UNIX AND NOT APPLE)

if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/local/lib)
    set(CMAKE_CXX_FLAGS " -static-libstdc++")
    set(common_libraries -static-libstdc++ pthread)
endif(APPLE)

# Link with core library
#target_link_directories(${PROJECT_NAME} PRIVATE ${CORE_LIBRARY_PATH}/build)
target_link_libraries(${PROJECT_NAME} PRIVATE core)
target_link_libraries(${PROJECT_NAME} PRIVATE ${common_libraries})


