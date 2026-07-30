# tools/poc/cpp — C++ codec comparison PoC (Google Benchmark).
#
# This is a research/diagnostic tool, not a production binary. It runs the
# candidate codecs (zstd, lz4, brotli, snappy, FastLZ, zlib) against the
# TTD workload (synthetic + real .ttd fixtures) and reports latency /
# throughput / ratio per codec per workload.
#
# Built only when -DBUILD_POC=ON (default OFF) to keep the default build
# surface small.
cmake_minimum_required(VERSION 3.14)

project(poc_codec_latency LANGUAGES C CXX)

message(">> tools/poc/cpp CMakeLists.txt")

# C++ standard inherited from top-level (C++20).
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output goes to the same bin/ as the rest of the project.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Locate the project root (we live at <root>/tools/poc/cpp/).
get_filename_component(ROOT_DIR "../../.." ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "poc_cpp: ROOT_DIR=${ROOT_DIR}")

# Google Benchmark — same path used by core/benchmarks/.
set(GOOGLEBENCHMARK_DIR ${ROOT_DIR}/lib/benchmark)
if(NOT EXISTS "${GOOGLEBENCHMARK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "poc_cpp: Google Benchmark not found at ${GOOGLEBENCHMARK_DIR}")
endif()

# When built as part of the parent project (which is the typical case),
# `benchmark::benchmark_main` is already created by core/benchmarks/. Only
# add_subdirectory Google Benchmark ourselves when running standalone.
if(NOT TARGET benchmark::benchmark_main)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${GOOGLEBENCHMARK_DIR} ${CMAKE_CURRENT_BINARY_DIR}/googlebenchmark)
endif()

# ---------------------------------------------------------------------------
# zstd — link against the embedded copy in core/src/3rdparty/zstd.
#
# When the PoC is built as part of the parent project (BUILD_POC=ON in the
# top-level CMakeLists.txt), `zstd::libzstd` is already created by
# core/src/CMakeLists.txt and we just link against it. When the PoC is built
# standalone (rare — only for codec-focused development), we add_subdirectory
# the zstd CMakeLists.txt ourselves.
# ---------------------------------------------------------------------------
if(NOT TARGET zstd::libzstd)
    add_subdirectory(
        ${ROOT_DIR}/core/src/3rdparty/zstd
        ${CMAKE_CURRENT_BINARY_DIR}/embedded-zstd
    )
endif()

# ---------------------------------------------------------------------------
# Other codec libraries — system-installed via Homebrew on macOS
# (apt/yum equivalents on Linux). find_package with REQUIRED OFF so the
# PoC still configures and runs with whatever subset is installed; each
# missing codec is silently dropped from the comparison.
# ---------------------------------------------------------------------------

# lz4 (Homebrew: brew install lz4)
set(LZ4_FOUND OFF)
find_path(LZ4_INCLUDE_DIR lz4.h)
find_library(LZ4_LIBRARY NAMES lz4)
if(LZ4_INCLUDE_DIR AND LZ4_LIBRARY)
    set(LZ4_FOUND ON)
    message(STATUS "poc_cpp: lz4 found at ${LZ4_LIBRARY}")
else()
    message(WARNING "poc_cpp: lz4 NOT found — `brew install lz4` to enable lz4 / lz4hc candidates")
endif()

# snappy (Homebrew: brew install snappy)
set(SNAPPY_FOUND OFF)
find_path(SNAPPY_INCLUDE_DIR snappy.h)
find_library(SNAPPY_LIBRARY NAMES snappy)
if(SNAPPY_INCLUDE_DIR AND SNAPPY_LIBRARY)
    set(SNAPPY_FOUND ON)
    message(STATUS "poc_cpp: snappy found at ${SNAPPY_LIBRARY}")
else()
    message(WARNING "poc_cpp: snappy NOT found — `brew install snappy` to enable snappy candidate")
endif()

# brotli (Homebrew: brew install brotli)
set(BROTLI_FOUND OFF)
find_path(BROTLI_INCLUDE_DIR brotli/encode.h)
find_library(BROTLIENC_LIBRARY NAMES brotlienc)
find_library(BROTLIDEC_LIBRARY NAMES brotlidec)
find_library(BROTLICOMMON_LIBRARY NAMES brotlicommon)
if(BROTLI_INCLUDE_DIR AND BROTLIENC_LIBRARY AND BROTLIDEC_LIBRARY AND BROTLICOMMON_LIBRARY)
    set(BROTLI_FOUND ON)
    message(STATUS "poc_cpp: brotli found at ${BROTLIENC_LIBRARY}")
else()
    message(WARNING "poc_cpp: brotli NOT found — `brew install brotli` to enable brotli candidates")
endif()

# zlib — always present on macOS (/usr/include/zlib.h). Linux usually has it too.
set(ZLIB_FOUND OFF)
find_path(ZLIB_INCLUDE_DIR zlib.h)
find_library(ZLIB_LIBRARY NAMES z)
if(ZLIB_INCLUDE_DIR AND ZLIB_LIBRARY)
    set(ZLIB_FOUND ON)
    message(STATUS "poc_cpp: zlib found at ${ZLIB_LIBRARY}")
else()
    message(WARNING "poc_cpp: zlib NOT found")
endif()

# Propagate found/not-found to the C++ preprocessor so the benchmark file
# can #ifdef out missing codecs.
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/codec_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/codec_config.h
    @ONLY
)

# ---------------------------------------------------------------------------
# FastLZ — vendored single-file (public domain).
# ---------------------------------------------------------------------------
add_library(fastlz STATIC ${CMAKE_CURRENT_SOURCE_DIR}/vendored/fastlz/fastlz.c)
target_include_directories(fastlz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/vendored/fastlz)
set_target_properties(fastlz PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    C_STANDARD 99
)

# ---------------------------------------------------------------------------
# PoC executable.
# ---------------------------------------------------------------------------
add_executable(poc_codec_latency
    src/poc_codec_latency.cpp
)

target_include_directories(poc_codec_latency PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_BINARY_DIR}/generated
)

target_link_libraries(poc_codec_latency PRIVATE
    benchmark::benchmark_main
    zstd::libzstd       # embedded
    fastlz              # vendored
)

# System libraries (only link what we found).
if(LZ4_FOUND)
    target_include_directories(poc_codec_latency PRIVATE ${LZ4_INCLUDE_DIR})
    target_link_libraries(poc_codec_latency PRIVATE ${LZ4_LIBRARY})
endif()
if(SNAPPY_FOUND)
    target_include_directories(poc_codec_latency PRIVATE ${SNAPPY_INCLUDE_DIR})
    target_link_libraries(poc_codec_latency PRIVATE ${SNAPPY_LIBRARY})
endif()
if(BROTLI_FOUND)
    target_include_directories(poc_codec_latency PRIVATE ${BROTLI_INCLUDE_DIR})
    target_link_libraries(poc_codec_latency PRIVATE
        ${BROTLIENC_LIBRARY} ${BROTLIDEC_LIBRARY} ${BROTLICOMMON_LIBRARY})
endif()
if(ZLIB_FOUND)
    target_include_directories(poc_codec_latency PRIVATE ${ZLIB_INCLUDE_DIR})
    target_link_libraries(poc_codec_latency PRIVATE ${ZLIB_LIBRARY})
endif()

# Suppress warnings from third-party headers.
if(NOT MSVC)
    target_compile_options(poc_codec_latency PRIVATE -Wno-unused-parameter)
endif()

# Default benchmark arguments: --benchmark_min_time=2s per benchmark.
# Override at runtime with --benchmark_* flags.
target_compile_definitions(poc_codec_latency PRIVATE
    BENCHMARK_DISABLE_THREAD_SAFETY_ATTRIBUTES
)
