# CMake entry point
cmake_minimum_required (VERSION 3.13)

message(">> core/benchmarks CMakeLists.txt")

project (core-benchmarks LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif (NOT CMAKE_BUILD_TYPE)
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Set output directories to match root CMakeLists.txt
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)


# Root directory
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Set paths
set(CORE_DIR ${ROOT_DIR}/core/src)
set(BENCHMARKS_DIR ${ROOT_DIR}/core/benchmarks)
set(GOOGLEBENCHMARK_DIR ${ROOT_DIR}/lib/benchmark)
message("CORE_DIR:${CORE_DIR}")
message("BENCHMARKS_DIR:${BENCHMARKS_DIR}")
message("GOOGLEBENCHMARK_DIR:${GOOGLEBENCHMARK_DIR}")

# Allow CMake to find modules in /cmake
set(CMAKE_MODULE_PATH "${ROOT_DIR}/cmake" ${CMAKE_MODULE_PATH})

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

if (WIN32 AND MINGW)
    set(BENCHMARK_FILES
        ${GOOGLEBENCHMARK_DIR}/src/benchmark.cc
        ${GOOGLEBENCHMARK_DIR}/src/benchmark_api_internal.cc
        ${GOOGLEBENCHMARK_DIR}/src/benchmark_main.cc
        ${GOOGLEBENCHMARK_DIR}/src/benchmark_name.cc
        ${GOOGLEBENCHMARK_DIR}/src/benchmark_register.cc
        ${GOOGLEBENCHMARK_DIR}/src/benchmark_runner.cc
        ${GOOGLEBENCHMARK_DIR}/src/colorprint.cc
        ${GOOGLEBENCHMARK_DIR}/src/commandlineflags.cc
        ${GOOGLEBENCHMARK_DIR}/src/complexity.cc
        ${GOOGLEBENCHMARK_DIR}/src/console_reporter.cc
        ${GOOGLEBENCHMARK_DIR}/src/counter.cc
        ${GOOGLEBENCHMARK_DIR}/src/csv_reporter.cc
        ${GOOGLEBENCHMARK_DIR}/src/json_reporter.cc
        ${GOOGLEBENCHMARK_DIR}/src/reporter.cc
        ${GOOGLEBENCHMARK_DIR}/src/sleep.cc
        ${GOOGLEBENCHMARK_DIR}/src/statistics.cc
        ${GOOGLEBENCHMARK_DIR}/src/string_util.cc
        ${GOOGLEBENCHMARK_DIR}/src/sysinfo.cc
        ${GOOGLEBENCHMARK_DIR}/src/timers.cc
    )

    # Suppress benchmark deprecation messages
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations)
else (WIN32 AND MINGW)
    # Google benchmark folder
    set(BENCHMARK_ENABLE_TESTING OFF)   # Disable Google Benchmark self-testing
    add_subdirectory(${GOOGLEBENCHMARK_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark)
endif (WIN32 AND MINGW)

# Compile all *.cpp files both from core/src and core/benchmarks folders
file (GLOB_RECURSE BENCHMARK_CPP_FILES ${BENCHMARKS_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${BENCHMARK_CPP_FILES}" "/build/" FALSE OUTVAR BENCHMARK_CPP_FILES)      # Exclude /benchmarks/build folder
EXCLUDE_FILES_FROM_DIR_IN_LIST("${BENCHMARK_CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR BENCHMARK_CPP_FILES) # Exclude /CMakeFile autogenerated files

set(SOURCES ${BENCHMARK_CPP_FILES})
message("BENCHMARKS:${BENCHMARK_CPP_FILES}")
message("SOURCES:${SOURCES}")

# Set include files search paths
include_directories(
    ${BENCHMARKS_DIR}
    ${CORE_DIR}
    ${GOOGLEBENCHMARK_DIR}/include
)

# Check if we're being included from the top CMakeLists.txt
if (CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    # We're being built directly, so build core first
    add_subdirectory(${CORE_DIR} core)
else()
    # I am called from other project with add_subdirectory()
    # So skip /src/core dependency build
endif()

# Compile benchmarks into executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Enable benchmark wrappers in code
target_compile_definitions(${PROJECT_NAME} PRIVATE _CODE_UNDER_BENCHMARK)

# Declare _ALWAYS_USE_LOGGING for compiler
target_compile_definitions(${PROJECT_NAME} PRIVATE _ALWAYS_USE_LOGGING)

# Declare _DEBUG symbol for compiler
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif (CMAKE_BUILD_TYPE MATCHES Debug)

# All standard libraries required
if (WIN32 AND MSVC)
    set(common_libraries stdc++ iconv)
endif (WIN32 AND MSVC)

if (WIN32 AND MINGW)
    # Link all as static
    set_target_properties(${PROJECT_NAME} PROPERTIES LINK_FLAGS "-static-libgcc -static-libstdc++ -Wl,-Bstatic,--whole-archive -lwinpthread -liconv -Wl,--no-whole-archive")
    set(common_libraries shlwapi ws2_32)
endif (WIN32 AND MINGW)

if (UNIX AND NOT APPLE)
    set(CMAKE_CXX_FLAGS " -static-libstdc++")
    set(common_libraries -static-libstdc++ pthread)
endif (UNIX AND NOT APPLE)

if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
    set(common_libraries pthread iconv)
endif (APPLE)

# Link with Google Benchmark libraries
target_link_libraries(${PROJECT_NAME} PRIVATE 
    ${common_libraries}
    core
    benchmark::benchmark_main
)

# Define data paths
set(DATA_PATH "${ROOT_DIR}/data")
set(CONFIG_FILE "${DATA_PATH}/configs/pentagon512k/unreal.ini")

# Copy resources using modern CMake syntax
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CONFIG_FILE}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unreal.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/testrom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMENT "Copying runtime resource files to output directory"
)