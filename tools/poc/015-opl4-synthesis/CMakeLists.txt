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

# Redirect binaries to the local bin/ folder inside the POC directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bin)

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

# Self-contained test harness (no gtest dependency — the library must stay
# std-lib-only and the PoC standalone-buildable with a single compiler call).
add_executable(opl4tests
    tests/opl4tests.cpp   # core units (§12.1) + main()
    tests/opl4vectors.cpp # golden/behaviour vectors (§12.1/12.2)
)
target_include_directories(opl4tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
if (NOT MSVC)
    target_compile_options(opl4tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_link_libraries(opl4tests PRIVATE opl4)

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
