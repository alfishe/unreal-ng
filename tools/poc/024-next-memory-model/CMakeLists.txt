# PoC 024: the ZX Spectrum Next's memory model (decision D6 of docs/inprogress/2026-10-07-zx-next) and the engine's per-step cost.
# Self-contained (no emulator build): the vendored CPU libraries and Google Benchmark from lib/.
#   cmake -S tools/poc/024-next-memory-model -B scratch/poc-024/build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   BUILD_DIR=$PWD/scratch/poc-024/build tools/build/build.sh
# Run from the repository root (the benchmark reads data/rom/48.rom):
#   UNREAL_NICE=0 scratch/poc-024/build/p2-shift14 --benchmark_out=... and p2-shift13

cmake_minimum_required(VERSION 3.16)
project(poc024-next-memory-model LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${REPO_ROOT}/lib/benchmark ${CMAKE_BINARY_DIR}/benchmark EXCLUDE_FROM_ALL)

file(GLOB Z80_SOURCES ${REPO_ROOT}/core/src/3rdparty/unreal-z80/*.cpp)
file(GLOB Z84_SOURCES ${REPO_ROOT}/core/src/3rdparty/z84c15/*.cpp)
file(GLOB NEXT_SOURCES ${REPO_ROOT}/core/src/3rdparty/unreal-next-z80/*.cpp)

# The paged bus of unreal-z80 takes its page size at compile time: one executable per size
foreach(SHIFT 14 13)
    add_executable(p2-shift${SHIFT} p2.cpp ${Z80_SOURCES} ${Z84_SOURCES} ${NEXT_SOURCES})
    target_compile_definitions(p2-shift${SHIFT} PRIVATE Z80CPU_PAGE_SHIFT=${SHIFT} POC_PAGE_SHIFT=${SHIFT})
    target_include_directories(p2-shift${SHIFT} PRIVATE
        ${REPO_ROOT}/core/src/3rdparty/unreal-z80 ${REPO_ROOT}/core/src/3rdparty/z84c15 ${REPO_ROOT}/core/src/3rdparty/unreal-next-z80)
    target_link_libraries(p2-shift${SHIFT} PRIVATE benchmark::benchmark)
    target_compile_options(p2-shift${SHIFT} PRIVATE -O2)
endforeach()
