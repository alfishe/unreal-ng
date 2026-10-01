# CMakeLists.txt - PoC 017: standalone Z80 CPU library extraction.
#
# Fully self-contained build (no integration with the emulator build system):
#   cmake -S . -B build -G Ninja
#   ninja -C build
#   ./build/z80demo && ./build/z80tests && ./build/z80bench
#
# Optional: benchmark comparison against the z80ex reference sources when
# they are available locally (default path: repo scratch/z80ex-dl):
#   cmake -S . -B build -G Ninja -DZ80EX_REF_DIR=<path-to-z80ex-sources>

cmake_minimum_required(VERSION 3.16)
project(z80-standalone-poc LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(z80lib STATIC
    z80lib/src/z80cpu.cpp
    z80lib/src/z80tables.cpp
    z80lib/src/z80daa.cpp
    z80lib/src/opcodes-base.cpp
    z80lib/src/opcodes-cb.cpp
    z80lib/src/opcodes-dd.cpp
    z80lib/src/opcodes-ed.cpp
    z80lib/src/opcodes-fd.cpp
    z80lib/src/opcodes-xxcb.cpp
)
target_include_directories(z80lib PUBLIC z80lib/include PRIVATE z80lib/src)

if(MSVC)
    target_compile_options(z80lib PRIVATE /W4)
else()
    target_compile_options(z80lib PRIVATE -Wall -Wextra -Wpedantic)
    # The register file keeps the core's Z80Registers layout (anonymous
    # struct/union pairs for bc/de/hl/af) so the port stays diffable against
    # core/src/emulator/cpu - clang -Wpedantic flags that GNU extension.
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(z80lib PRIVATE -Wno-gnu-anonymous-struct -Wno-nested-anon-types)
    endif()
endif()

# Host machine helper shared by the demo/tests/benchmarks: flat 64K RAM,
# RST 0x10 character capture, TAP code-block loading.
add_library(z80host STATIC
    tests/hostmachine.cpp
)
target_include_directories(z80host PUBLIC z80lib/include tests)
target_link_libraries(z80host PUBLIC z80lib)

add_executable(z80demo demo/demo-main.cpp)
target_link_libraries(z80demo PRIVATE z80host)

add_executable(z80tests tests/test-main.cpp)
target_link_libraries(z80tests PRIVATE z80host)
# Absolute path to the repo's ZEXALL/ZEXDOC/ZEXBIT/ZEXFIX tapes at build time
target_compile_definitions(z80tests PRIVATE
    Z80POC_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../data/testsoft/ZEXALL")

add_executable(z80bench benchmarks/bench-main.cpp)
target_link_libraries(z80bench PRIVATE z80host)

# Optional head-to-head benchmark against the z80ex reference (GPL v2,
# scratch/z80ex-dl). Built only when the sources are present. z80ex.c is a
# unity build (it #includes ptables.c and all opcode units), so only z80ex.c
# is compiled here.
set(Z80EX_REF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../scratch/z80ex-dl" CACHE PATH "z80ex reference sources")
if(EXISTS "${Z80EX_REF_DIR}/z80ex.c")
    message(STATUS "PoC017: z80ex reference found at ${Z80EX_REF_DIR} - building comparison benchmark")
    add_executable(z80bench-z80ex benchmarks/bench-z80ex.cpp ${Z80EX_REF_DIR}/z80ex.c)
    target_include_directories(z80bench-z80ex PRIVATE ${Z80EX_REF_DIR})
    # Upstream supplies these via autoconf; values from z80ex 0.16.x.
    target_compile_definitions(z80bench-z80ex PRIVATE
        Z80EX_REF_AVAILABLE=1
        Z80EX_API_REVISION=1
        Z80EX_VERSION_MAJOR=0
        Z80EX_VERSION_MINOR=16
        "Z80EX_RELEASE_TYPE=\"\""
        "Z80EX_VERSION_STR=\"0.16-scratch\"")
    target_link_libraries(z80bench-z80ex PRIVATE z80host)
endif()
