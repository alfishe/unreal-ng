# libopl4 — YMF278B (OPL4) synthesis library: FM (OPL3-class) and PCM
# (wavetable) engines, the chip bus/timing model and the host render layer.
# No dependencies beyond the C++ standard library.
#
# Embedded use: add_subdirectory(3rdparty/opl4) and link `opl4`. Standalone:
# cmake -S . -B build && cmake --build build --target opl4tests && ctest --test-dir build
cmake_minimum_required(VERSION 3.14)
project(opl4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(opl4 STATIC
    src/opl4.cpp
    src/fm/fmbus.cpp
    src/fm/fmsynthopl4.cpp
    src/pcm/pcmsynthopl4.cpp
    src/opl4render.cpp
    src/wavememory.cpp
)
target_include_directories(opl4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(opl4 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (NOT MSVC)
    target_compile_options(opl4 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Self-contained test harness (no gtest: the library stays std-lib-only).
# Kept out of default builds when embedded; registered with CTest when this
# directory is the top-level project.
add_executable(opl4tests EXCLUDE_FROM_ALL
    tests/opl4tests.cpp   # units + main()
    tests/opl4vectors.cpp # behaviour vectors
    tests/opl4sweep.cpp   # per-field conformance sweeps
    tests/opl4fmtests.cpp # FM engine behaviour
)
target_include_directories(opl4tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(opl4tests PRIVATE opl4)
if (NOT MSVC)
    target_compile_options(opl4tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(opl4tests PRIVATE m)
endif()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_test(NAME opl4tests COMMAND opl4tests)
endif()
