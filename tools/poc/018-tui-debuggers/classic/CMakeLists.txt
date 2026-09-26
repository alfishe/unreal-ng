# CMakeLists.txt - PoC 018a: Classic UnrealSpeccy Debugger TUI (TDD-DBG-01, 80x30).
#
# Fully self-contained build (no integration with the emulator build system):
#   cmake -S . -B build -G Ninja
#   ninja -C build
#   ./build/dbgclassic            # interactive TUI (mock backend by default)
#   ./build/dbgclassic-tests      # golden dump + unit tests
#
# Vendored third-party code (lib/):
#   z80ex  1.1.21 - Z80 CPU + disassembler, GPLv2 (one-time stub for the
#                   server-side disasm path; the real transport serves it)
#   ftxui  7.1.0  - terminal UI component library, MIT

cmake_minimum_required(VERSION 3.16)
project(dbgclassic LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Vendored z80ex (C library: CPU + disassembler)
# ---------------------------------------------------------------------------
# z80ex.c #includes ptables.c and opcodes/*.c itself - only the two
# top-level units are compiled standalone.
add_library(z80ex STATIC
    lib/z80ex/z80ex.c
    lib/z80ex/z80ex_dasm.c
)
target_include_directories(z80ex PUBLIC lib/z80ex/include PRIVATE lib/z80ex)
if(MSVC)
    target_compile_options(z80ex PRIVATE /W3)
else()
    target_compile_options(z80ex PRIVATE -Wall)
endif()

# ---------------------------------------------------------------------------
# Vendored FTXUI
# ---------------------------------------------------------------------------
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/ftxui EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------
# SDL3
# ---------------------------------------------------------------------------
find_package(SDL3 QUIET)
if(NOT TARGET SDL3::SDL3)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(SDL3 REQUIRED sdl3)
    add_library(SDL3::SDL3 INTERFACE IMPORTED)
    target_include_directories(SDL3::SDL3 INTERFACE ${SDL3_INCLUDE_DIRS})
    target_link_libraries(SDL3::SDL3 INTERFACE ${SDL3_LIBRARIES})
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
add_library(dbgcore STATIC
    src/model/hexfmt.cpp
    src/screen/font16.cpp
    src/screen/glyphs.cpp
    src/screen/palette.cpp
    src/screen/textscreen.cpp
    src/backend/sample-state.cpp
    src/backend/mock-backend.cpp
    src/backend/disasm-unreal.cpp
    src/backend/asm-subset.cpp
    src/backend/expr-lang.cpp
    src/backend/http-client.cpp
    src/backend/rest-backend.cpp
    src/panels/regs.cpp
    src/panels/trace.cpp
    src/panels/memory.cpp
    src/panels/side.cpp
    src/panels/preview.cpp
    src/ui/keymap.cpp
    src/ui/breakpoints.cpp
    src/ui/labels.cpp
    src/ui/modals.cpp
    src/ui/dialogs.cpp
    src/ui/debugger-app.cpp
    src/gui/pixel-renderer.cpp
    src/gui/sdl-runner.cpp
    src/tui/tui-runner.cpp
    src/tui/frame-render.cpp
)
target_include_directories(dbgcore PUBLIC src)
target_link_libraries(dbgcore PUBLIC z80ex ftxui::component SDL3::SDL3)

add_executable(dbgclassic src/main.cpp)
target_link_libraries(dbgclassic PRIVATE dbgcore)

# Zero-warnings policy on our own code.
if(MSVC)
    target_compile_options(dbgcore PRIVATE /W4 /permissive-)
    target_compile_options(dbgclassic PRIVATE /W4 /permissive-)
else()
    target_compile_options(dbgcore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dbgclassic PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests (self-contained micro-harness, no external test framework)
# ---------------------------------------------------------------------------
add_executable(dbgclassic-tests tests/test-main.cpp)
target_link_libraries(dbgclassic-tests PRIVATE dbgcore)
if(MSVC)
    target_compile_options(dbgclassic-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(dbgclassic-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_definitions(dbgclassic-tests PRIVATE
    DBGPOC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME dbgclassic-tests COMMAND dbgclassic-tests)
