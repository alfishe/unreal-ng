# The terminal debuggers: the classic UnrealSpeccy debugger (80 x 30, TDD-DBG-01) and its TS-Conf fork (157 x 30,
# TDD-DBG-02), over a running unreal-ng through its WebAPI (the WebSocket /api/v1/ws by default, REST on request) or
# over the mock machine of their golden tests.
#
# common/ holds the sources both use as they are; classic/ and tsconf/ the ones each has its own (the model, the
# backend interface, the painters, the key map, main). Each app compiles common/ against its own headers: its src/
# comes first on the include path.
#
# Vendored (lib/): z80ex 1.1.21 (Z80 CPU + disassembler, GPLv2: the mock machine), FTXUI 7.1.0 (MIT). SDL3 when it is
# found: the pixel window the apps open first; without it they run in the terminal.

# z80ex.c #includes ptables.c and opcodes/*.c itself: only the two top-level units compile
add_library(unreal-tui-z80ex STATIC lib/z80ex/z80ex.c lib/z80ex/z80ex_dasm.c)
target_include_directories(unreal-tui-z80ex PUBLIC lib/z80ex/include PRIVATE lib/z80ex)
set_target_properties(unreal-tui-z80ex PROPERTIES C_STANDARD 11)

set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
add_subdirectory(lib/ftxui EXCLUDE_FROM_ALL)

option(UNREAL_TUI_SDL "Open the terminal debuggers' pixel window through SDL3 when SDL3 is found" ON)
set(UNREAL_TUI_HAVE_SDL OFF)
if (UNREAL_TUI_SDL)
    find_package(SDL3 QUIET CONFIG)
    if (NOT TARGET SDL3::SDL3)
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(UNREAL_TUI_SDL3 QUIET IMPORTED_TARGET sdl3)
            if (UNREAL_TUI_SDL3_FOUND)
                add_library(SDL3::SDL3 ALIAS PkgConfig::UNREAL_TUI_SDL3)
            endif ()
        endif ()
    endif ()
    if (TARGET SDL3::SDL3)
        set(UNREAL_TUI_HAVE_SDL ON)
    endif ()
endif ()
message(STATUS "    terminal debuggers: SDL3 pixel window ${UNREAL_TUI_HAVE_SDL}")

set(UNREAL_TUI_COMMON
    common/src/model/hexfmt.cpp
    common/src/screen/font16.cpp
    common/src/screen/glyphs.cpp
    common/src/screen/palette.cpp
    common/src/screen/textscreen.cpp
    common/src/backend/disasm-unreal.cpp
    common/src/backend/asm-subset.cpp
    common/src/backend/expr-lang.cpp
    common/src/backend/net-socket.cpp
    common/src/backend/http-client.cpp
    common/src/backend/ws-client.cpp
    common/src/backend/webapi-backend.cpp
    common/src/panels/regs.cpp
    common/src/panels/trace.cpp
    common/src/panels/memory.cpp
    common/src/panels/preview.cpp
    common/src/ui/breakpoints.cpp
    common/src/ui/labels.cpp
    common/src/ui/modals.cpp
    common/src/ui/actions.cpp
    common/src/ui/dialogs.cpp
    common/src/ui/debugger-app.cpp
    common/src/gui/pixel-renderer.cpp
    common/src/tui/tui-runner.cpp
    common/src/tui/frame-render.cpp
    common/src/tui/key-input.cpp
)

function(unreal_tui_debugger name app)
    set(src ${CMAKE_CURRENT_SOURCE_DIR}/${app}/src)
    set(own ${src}/backend/sample-state.cpp ${src}/backend/mock-backend.cpp ${src}/panels/side.cpp
            ${src}/ui/keymap.cpp ${ARGN})
    if (UNREAL_TUI_HAVE_SDL)
        list(APPEND own ${src}/gui/sdl-runner.cpp)
    else ()
        list(APPEND own ${src}/gui/sdl-none.cpp)
    endif ()
    add_library(${name}-core STATIC ${UNREAL_TUI_COMMON} ${own})
    target_include_directories(${name}-core PUBLIC ${src} ${CMAKE_CURRENT_SOURCE_DIR}/common/src)
    target_link_libraries(${name}-core PUBLIC unreal-tui-z80ex ftxui::component)
    if (UNREAL_TUI_HAVE_SDL)
        target_link_libraries(${name}-core PUBLIC SDL3::SDL3)
    endif ()
    if (WIN32)
        target_link_libraries(${name}-core PUBLIC ws2_32)
    endif ()

    add_executable(${name} ${src}/main.cpp)
    target_link_libraries(${name} PRIVATE ${name}-core)
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # The golden dumps and unit tests (a micro-harness of their own, no test framework)
    add_executable(${name}-tests ${CMAKE_CURRENT_SOURCE_DIR}/${app}/tests/test-main.cpp)
    target_link_libraries(${name}-tests PRIVATE ${name}-core)
    target_compile_definitions(${name}-tests PRIVATE UNREAL_TUI_APP_DIR="${CMAKE_CURRENT_SOURCE_DIR}/${app}")
    set_target_properties(${name}-tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

    # Zero warnings on our own code
    foreach (t ${name}-core ${name} ${name}-tests)
        if (MSVC)
            target_compile_options(${t} PRIVATE /W4 /permissive-)
        else ()
            target_compile_options(${t} PRIVATE -Wall -Wextra -Wpedantic)
        endif ()
    endforeach ()
endfunction()

unreal_tui_debugger(unreal-tui-debugger classic)
unreal_tui_debugger(unreal-tui-debugger-tsconf tsconf ${CMAKE_CURRENT_SOURCE_DIR}/tsconf/src/panels/paint-tsconf.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/tsconf/src/backend/tsconf-webapi-backend.cpp)

# Both test programs at once: tools/build/build.sh tui-debugger-tests
add_custom_target(tui-debugger-tests DEPENDS unreal-tui-debugger-tests unreal-tui-debugger-tsconf-tests)
