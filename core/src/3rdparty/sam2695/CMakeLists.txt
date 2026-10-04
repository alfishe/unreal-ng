# libsam2695 — Dream SAM2695 General MIDI synthesizer: serial MIDI receiver, the chip's MIDI
# implementation and voice model over SoundFont 2 banks, the render layer.
# No dependencies beyond the C++ standard library.
#
# Embedded use: add_subdirectory(3rdparty/sam2695) and link `sam2695`. Standalone:
# cmake -S . -B build -G Ninja && ninja -C build sam2695tests && ./build/sam2695tests
cmake_minimum_required(VERSION 3.14)
project(sam2695 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(sam2695 STATIC
    src/sam2695.cpp
    src/synthcore.cpp
    src/bank/generators.cpp
    src/bank/sf2bank.cpp
    src/bank/sha256.cpp
    src/render/resampler.cpp
    src/voice/interpolator.cpp
    src/voice/voice.cpp
)
target_include_directories(sam2695 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(sam2695 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (NOT MSVC)
    target_compile_options(sam2695 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Self-contained test harness (no gtest: the library stays std-lib-only). Kept out of default
# builds when embedded; registered with CTest when this directory is the top-level project.
add_executable(sam2695tests EXCLUDE_FROM_ALL
    tests/sam2695tests.cpp   # main() + framework counters
    tests/miditests.cpp      # UART, parser
    tests/banktests.cpp      # SF2 loader, SHA-256
    tests/voicetests.cpp     # envelopes, LFO, filter, interpolation, loops, modulators
    tests/synthtests.cpp     # allocation, exclusive classes, state, render, chart rows, golden digests
)
target_include_directories(sam2695tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR}/tests)
target_link_libraries(sam2695tests PRIVATE sam2695)
if (NOT MSVC)
    target_compile_options(sam2695tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Command-line renderer (Standard MIDI File + bank -> WAV / report) used by the FluidSynth reference
# harness and the bank corpus check in tools/verification/sam2695/.
add_executable(sam2695render EXCLUDE_FROM_ALL
    tools/sam2695render.cpp
    tools/smfreader.cpp
)
target_include_directories(sam2695render PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/tools)
target_link_libraries(sam2695render PRIVATE sam2695)
if (NOT MSVC)
    target_compile_options(sam2695render PRIVATE -Wall -Wextra -Wpedantic)
endif()

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()
    add_test(NAME sam2695tests COMMAND sam2695tests)
endif()
