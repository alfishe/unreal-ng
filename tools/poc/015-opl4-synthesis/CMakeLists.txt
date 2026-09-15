# tools/poc/015-opl4-synthesis/CMakeLists.txt
#
# libopl4 — standalone YMF278B (OPL4) synthesis library PoC.
# No dependencies beyond the C++ standard library (spec R10).
# Built only when -DBUILD_POC=ON at the top-level CMakeLists.txt.
cmake_minimum_required(VERSION 3.14)
project(opl4_synthesis LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Redirect binaries to a backend-specific folder inside the POC directory:
# interleaved builds from two build directories into one shared output path
# defeat ninja's up-to-date check (the other backend's fresher binary looks
# "already built") and silently serve the wrong backend's tests.
if (OPL4_FM_BACKEND STREQUAL "ymfm")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin-ymfm)
else()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)
endif()

add_library(opl4 STATIC
    src/opl4.cpp
    src/opl4fm.cpp
    src/opl4pcm.cpp
    src/opl4render.cpp
    src/wavememory.cpp
)
target_include_directories(opl4 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
if (NOT MSVC)
    target_compile_options(opl4 PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Temporary FM verification backend (-DOPL4_FM_BACKEND=ymfm): swaps the
# chip's FM half for ymfm's OPL3 core (A/B synthesis verification, the
# openMSX-audited PCM half stays ours). Sources come from the vendored ymfm
# tree — same pinned commit as the TSFM device — so the DEFAULT build keeps
# libopl4 std-lib-only (R10) while the backend build and the comparator test
# link the support library below.
set(OPL4_FM_BACKEND "opl4" CACHE STRING "libopl4 FM engine: opl4 (in-tree) or ymfm (verification A/B)")
set_property(CACHE OPL4_FM_BACKEND PROPERTY STRINGS opl4 ymfm)
set(YMFM_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/src/3rdparty/ymfm" CACHE PATH "vendored ymfm sources")

if (NOT EXISTS ${YMFM_DIR}/ymfm_opl.h)
    message(FATAL_ERROR "vendored ymfm not found at ${YMFM_DIR} (OPL4_FM_BACKEND=ymfm and the comparator test need it)")
endif()

# ymfm OPL support lib: the three upstream .cpp files (warnings suppressed —
# the zero-warnings policy covers our code, not refs) plus our adapter,
# which compiles warning-clean under the project flags.
add_library(opl4ymfm STATIC EXCLUDE_FROM_ALL
    ${YMFM_DIR}/ymfm_opl.cpp
    ${YMFM_DIR}/ymfm_pcm.cpp
    ${YMFM_DIR}/ymfm_adpcm.cpp
    src/opl4fmymfm.cpp
)
target_include_directories(opl4ymfm SYSTEM PUBLIC ${YMFM_DIR})
target_include_directories(opl4ymfm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (NOT MSVC)
    target_compile_options(opl4ymfm PRIVATE -Wall -Wextra -Wpedantic)
    set_source_files_properties(${YMFM_DIR}/ymfm_opl.cpp ${YMFM_DIR}/ymfm_pcm.cpp
                                ${YMFM_DIR}/ymfm_adpcm.cpp
                                PROPERTIES COMPILE_OPTIONS "-w")
else()
    set_source_files_properties(${YMFM_DIR}/ymfm_opl.cpp ${YMFM_DIR}/ymfm_pcm.cpp
                                ${YMFM_DIR}/ymfm_adpcm.cpp
                                PROPERTIES COMPILE_OPTIONS "/w")
endif()

if (OPL4_FM_BACKEND STREQUAL "ymfm")
    target_compile_definitions(opl4 PUBLIC OPL4_FM_YMFM=1)
    target_link_libraries(opl4 PUBLIC opl4ymfm)
endif()

# Self-contained test harness (no gtest dependency — the library must stay
# std-lib-only and the PoC standalone-buildable with a single compiler call).
add_executable(opl4tests
    tests/opl4tests.cpp   # core units (§12.1) + main()
    tests/opl4vectors.cpp # golden/behaviour vectors (§12.1/12.2)
    tests/opl4sweep.cpp   # conformance sweeps (§12.2.1, exhaustive fields)
    tests/opl4fmcompare.cpp # differential: in-tree FM vs ymfm backend
)
target_include_directories(opl4tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (NOT MSVC)
    target_compile_options(opl4tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(opl4tests PRIVATE opl4 opl4ymfm)

if (NOT MSVC)
    target_link_libraries(opl4tests PRIVATE m)
endif()

# Co-simulation harnesses (§12.2/12.3) — opt-in: the differential harness
# needs the pinned ymfm reference sources, fetched once over the network by
# cosim/fetch-refs.sh (refs/ is gitignored, no vendored third-party code).
option(COSIM "Build the co-simulation harnesses in cosim/" OFF)
if (COSIM)
    add_subdirectory(cosim)
endif()
