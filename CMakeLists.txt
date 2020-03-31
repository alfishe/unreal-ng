cmake_minimum_required(VERSION 3.16)

message(">> Unreal CMakeLists.txt")

# THIS HAS TO COME BEFORE THE PROJECT LINE
if (APPLE)
	# Only macOS 15 has std::filesystem libraries in CLang, so use brew installed GCC instead
	set(CMAKE_C_COMPILER "/usr/local/bin/gcc")
	set(CMAKE_CXX_COMPILER "/usr/local/bin/g++")
endif (APPLE)
# THIS HAS TO COME BEFORE THE PROJECT LINE

project (unreal)

# Build options
option(BUILD_TESTS "Build the tests" ON)
option(BENCHMARK "Execute benchmarks" ON)

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif (NOT CMAKE_BUILD_TYPE)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Disable modern SDK warnings about insecurity for POSIX functions
if (MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif (MSVC)

# Emulator core
add_subdirectory(core/src)

# Emulator test console client
add_subdirectory(testclient)

# Unit tests
if (BUILD_TESTS)
	message("Building tests")
	enable_testing()
	add_subdirectory(core/tests)
endif (BUILD_TESTS)

# Benchmarks
if (BENCHMARK)
	message("Building benchmarks")
	#add_subdirectory(
endif (BENCHMARK)