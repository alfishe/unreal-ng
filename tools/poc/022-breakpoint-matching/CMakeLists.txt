# PoC 022: breakpoint matching on the debug hot path - which structure answers "does this access hit a
# breakpoint" fastest, for exact addresses, ranges, physical pages and masked ports, as the set grows.
#
# Self-contained (no emulator build). Configure and build through the repository's build wrapper:
#   cmake -S tools/poc/022-breakpoint-matching -B scratch/poc-022/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   BUILD_DIR=$PWD/scratch/poc-022/build tools/build/build.sh
# Traces and results of the published run: data/README.md. Then run an experiment, e.g.
#   UNREAL_NICE=0 scratch/poc-022/build/e01-baseline --benchmark_out=... --benchmark_out_format=json

cmake_minimum_required(VERSION 3.16)
project(poc022-breakpoint-matching LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

# Google Benchmark from lib/
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/lib/benchmark ${CMAKE_BINARY_DIR}/benchmark EXCLUDE_FROM_ALL)

# The trace recorder: a 128K Spectrum on the vendored unreal-z80 core
file(GLOB UNREAL_Z80_SOURCES ${REPO_ROOT}/core/src/3rdparty/unreal-z80/*.cpp)
add_executable(recorder recorder/recorder.cpp ${UNREAL_Z80_SOURCES})
target_include_directories(recorder PRIVATE ${REPO_ROOT}/core/src/3rdparty/unreal-z80)

# One binary per experiment
function(add_experiment name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE benchmark::benchmark)
    target_compile_options(${name} PRIVATE -Wall -Wextra -O2)
endfunction()

add_experiment(e01-baseline 01-baseline/baseline.cpp)
add_experiment(e02-exact-lookup 02-exact-lookup/exact.cpp)
add_experiment(e03-ranges 03-ranges/ranges.cpp)
add_experiment(e04-physical 04-physical/physical.cpp)
add_experiment(e05-ports 05-ports/ports.cpp)
add_experiment(e06-combined 06-combined/combined.cpp)
