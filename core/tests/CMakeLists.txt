cmake_minimum_required(VERSION 3.16.0)

# THIS HAS TO COME BEFORE THE PROJECT LINE
if (APPLE)
	# Only macOS 15 has std::filesystem libraries in CLang, so use brew installed GCC instead
	set(CMAKE_C_COMPILER "/usr/local/bin/gcc")
	set(CMAKE_CXX_COMPILER "/usr/local/bin/g++")
endif (APPLE)
# THIS HAS TO COME BEFORE THE PROJECT LINE

# Project named 'core' on C++
project(core_tests LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Include CMake helper functions file
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/func.cmake)

# Set paths
get_filename_component(CORE_DIR ../src ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(TESTS_DIR ./ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
get_filename_component(GOOGLETEST_DIR ../../lib/googletest ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message("CORE_DIR:${CORE_DIR}")
message("TESTS_DIR:${TESTS_DIR}")
message("GOOGLETEST_DIR:${GOOGLETEST_DIR}")

# Set include files search paths
include_directories(
	${TESTS_DIR}
	${CORE_DIR}
	${GOOGLETEST_DIR}/googletest/include
)

# Compile GoogleTest
add_subdirectory(${GOOGLETEST_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googletest)



# Compile all *.cpp files both from core/src and core/tests folders
file (GLOB_RECURSE TEST_CPP_FILES ${TESTS_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${TEST_CPP_FILES}" "/build/" FALSE OUTVAR TEST_CPP_FILES) # Exclude /tests/build folder
file (GLOB_RECURSE CORE_CPP_FILES ${CORE_DIR}/*.cpp)
set (SOURCES "${CORE_CPP_FILES};${TEST_CPP_FILES}")
message("SOURCES:${SOURCES}")

# Produce executable
add_executable(${PROJECT_NAME} ${SOURCES})

# 'pch.h' is our precompiled header
#target_precompile_headers(${PROJECT_NAME} PRIVATE ${TESTS_DIR}/pch.h)

target_link_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/googletest)

# All standard libraries required
if (WINDOWS)
    set(common_libraries stdc++ stdc++fs gtest)
endif (WINDOWS)

if (UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS " -static-libstdc++")
    set(common_libraries -static-libstdc++ stdc++fs pthread gtest)
endif (UNIX AND NOT APPLE)

if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/local/lib)
    set(CMAKE_CXX_FLAGS " -static-libstdc++")
    set(common_libraries -static-libstdc++ stdc++fs pthread gtest)
endif (APPLE)

target_link_libraries(${PROJECT_NAME} ${common_libraries})

