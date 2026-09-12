# tools/poc/011-ttd-v2-capture-analysis/CMakeLists.txt
cmake_minimum_required(VERSION 3.13)
project(ttd_v2_capture_analysis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Redirect binaries to the local bin/ folder inside the POC directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

# Fetch root dir path
get_filename_component(ROOT_DIR ../../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(GOOGLEBENCHMARK_DIR ${ROOT_DIR}/lib/benchmark)

# Include Google Benchmark if not already included
if (NOT TARGET benchmark::benchmark)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GOOGLEBENCHMARK_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark)
endif()

# Include directories needed for the POC
include_directories(
    ${ROOT_DIR}/core/src
    ${ROOT_DIR}/core/src/3rdparty
    ${ROOT_DIR}/core/src/3rdparty/zstd/lib
    ${ROOT_DIR}/core/recording/src
    ${ROOT_DIR}/lib/googletest/googletest/include
    ${GOOGLEBENCHMARK_DIR}/include
    /opt/homebrew/opt/lz4/include
    /usr/local/include
)

link_directories(
    ${ROOT_DIR}/cmake-build-release/lib
    /opt/homebrew/opt/lz4/lib
    /usr/local/lib
)

# Collect all benchmark sources
file(GLOB BENCH_SOURCES "benchmarks/*.cpp")

if(BENCH_SOURCES)
    # We create a single executable containing all benchmarks
    add_executable(ttd_v2_benchmarks ${BENCH_SOURCES})

    # Enable benchmark wrappers in code
    target_compile_definitions(ttd_v2_benchmarks PRIVATE _CODE_UNDER_BENCHMARK)

    target_link_libraries(ttd_v2_benchmarks PRIVATE
        core
        recording
        libzstd_static
        lz4
        benchmark::benchmark_main
    )

    if (APPLE)
        target_link_libraries(ttd_v2_benchmarks PRIVATE pthread iconv "-framework AVFoundation" "-framework CoreMedia" "-framework CoreVideo" "-framework Foundation" "-framework AudioToolbox")
    elseif (UNIX)
        target_link_libraries(ttd_v2_benchmarks PRIVATE pthread)
    endif()
endif()

# Collect all test sources
file(GLOB TEST_SOURCES "tests/*.cpp")

if(TEST_SOURCES)
    # We create a single executable for the Google Tests
    add_executable(ttd_v2_tests ${TEST_SOURCES})

    target_link_libraries(ttd_v2_tests PRIVATE
        core
        recording
        gtest_main
    )

    if (APPLE)
        target_link_libraries(ttd_v2_tests PRIVATE pthread iconv)
    elseif (UNIX)
        target_link_libraries(ttd_v2_tests PRIVATE pthread)
    endif()
endif()

# Copy resources using modern CMake syntax
set(DATA_PATH "${ROOT_DIR}/data")
add_custom_command(
    TARGET ttd_v2_benchmarks POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/configs" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/configs"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/testrom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMENT "Copying runtime resource files to output directory"
)
