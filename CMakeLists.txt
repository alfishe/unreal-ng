# CMake entry point
cmake_minimum_required(VERSION 3.16)

message(">> Unreal CMakeLists.txt")

# Set C++ standard to 17 globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(TESTS "Enable tests" ON)
option(BENCHMARKS "Enable benchmarks" ON)

# THIS HAS TO COME BEFORE THE PROJECT LINE
#if (APPLE)
#	execute_process(COMMAND sw_vers OUTPUT_VARIABLE OSX_VERSION)
#	string(REGEX MATCH "([0-9]+.[0-9]+)" OSX_VERSION ${OSX_VERSION})
#	message("OSX_VERSION: ${OSX_VERSION}")
#	string(REGEX MATCH "[0-9]+$" OSX_FAMILY ${OSX_VERSION})
#
#	if (OSX_FAMILY LESS 15)
#		# Only macOS 15 has std::filesystem libraries in CLang, so use brew installed GCC instead
#		message(STATUS "Using GCC compiler on macOS")
#		#set(CMAKE_C_COMPILER "/usr/local/bin/gcc-9")
#		#set(CMAKE_CXX_COMPILER "/usr/local/bin/g++-9")
#	endif (OSX_FAMILY LESS 15)
#endif (APPLE)
# THIS HAS TO COME BEFORE THE PROJECT LINE

project (unreal)

# Build options
option(TESTS "Build the tests" ON)
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

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build core library first
add_subdirectory(core/src)

# Emulator test console client
add_subdirectory(testclient)

# Unit tests
if (TESTS)
	message("Building tests")
	add_subdirectory(core/tests)
endif (TESTS)

# Benchmarks
if (BENCHMARKS)
	message("Building benchmarks")

	# Prevent Google benchmark from building it's own tests
	set(BENCHMARK_ENABLE_TESTING OFF)
	add_subdirectory(core/benchmarks)
endif (BENCHMARKS)