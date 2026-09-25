# CMakeLists.txt - PoC 018b: TSConf Debugger TUI (TDD-DBG-02, 157-col fork).
#
# Independent project: no cross-references to ../classic. Today only the
# vendored libraries and a placeholder shell build; the tsconf-base milestone
# copies the classic base (model/backend/painters) and applies the fork
# overrides. Build identical to classic:
#   cmake -S . -B build -G Ninja
#   ninja -C build
#   ./build/dbgtsconf
#
# Vendored third-party code (lib/):
#   z80ex  1.1.21 - Z80 CPU + disassembler, GPLv2
#   ftxui  7.1.0  - terminal UI component library, MIT

cmake_minimum_required(VERSION 3.16)
project(dbgtsconf LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---------------------------------------------------------------------------
# Vendored z80ex (z80ex.c #includes ptables.c and opcodes/*.c itself)
# ---------------------------------------------------------------------------
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
# Application (classic base + fork band; no cross-references to ../classic)
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
    src/panels/paint-tsconf.cpp
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

add_executable(dbgtsconf src/main.cpp)
target_link_libraries(dbgtsconf PRIVATE dbgcore)

# Zero-warnings policy on our own code.
if(MSVC)
    target_compile_options(dbgcore PRIVATE /W4 /permissive-)
    target_compile_options(dbgtsconf PRIVATE /W4 /permissive-)
else()
    target_compile_options(dbgcore PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(dbgtsconf PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Tests (self-contained micro-harness; golden fixtures from the oracle)
# ---------------------------------------------------------------------------
add_executable(dbgtsconf-tests tests/test-main.cpp)
target_link_libraries(dbgtsconf-tests PRIVATE dbgcore)
if(MSVC)
    target_compile_options(dbgtsconf-tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(dbgtsconf-tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
target_compile_definitions(dbgtsconf-tests PRIVATE
    DBGPOC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
enable_testing()
add_test(NAME dbgtsconf-tests COMMAND dbgtsconf-tests)
