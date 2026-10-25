# POC 035 (README.md): the same debugger screen in Dear ImGui, RmlUi and Qt 6 (Widgets, Quick), with and without a GPU.
#
# Off unless configured with -DBUILD_POC=ON -DPOC035_GUI_BENCH=ON -DPOC035_DEPS_PREFIX=<prefix>, where <prefix> is what
# scripts/fetch-deps.sh built (SDL3, FreeType, RmlUi at pinned commits, plus the Dear ImGui sources next to it). Every
# target is EXCLUDE_FROM_ALL; `tools/build/build.sh poc035-all` builds them. Not part of any product build.
#
# Also configures on its own (cmake -S tools/poc/035-debugger-gui-bench ...) for machines without the emulator's
# dependencies; then pass Qt through CMAKE_PREFIX_PATH.

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.24)
    project(poc035_debugger_gui_bench C CXX)
    if (APPLE)
        enable_language(OBJCXX)
    endif ()
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if (NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE Release)
    endif ()
    set(POC035_GUI_BENCH ON)
endif ()

option(POC035_GUI_BENCH "POC 035: build the GUI framework benchmark (needs scripts/fetch-deps.sh and Qt 6.7+)" OFF)
if (NOT POC035_GUI_BENCH)
    return()
endif ()
if (APPLE)
    enable_language(OBJCXX)
endif ()

set(POC035_DEPS_PREFIX "" CACHE PATH "POC 035: install prefix made by scripts/fetch-deps.sh (SDL3, FreeType, RmlUi)")
if (NOT POC035_DEPS_PREFIX OR NOT EXISTS "${POC035_DEPS_PREFIX}")
    message(WARNING "POC 035: POC035_DEPS_PREFIX is not set or missing; run scripts/fetch-deps.sh first. POC 035 skipped.")
    return()
endif ()
get_filename_component(_poc035_deps_root "${POC035_DEPS_PREFIX}" DIRECTORY)
set(POC035_IMGUI_DIR "${_poc035_deps_root}/imgui" CACHE PATH "POC 035: Dear ImGui (docking) sources")
set(POC035_RMLUI_SRC "${_poc035_deps_root}/rmlui" CACHE PATH "POC 035: RmlUi sources (for the SDL3 + GL3 backend files)")

# Directory scope only: the emulator's own dependency lookup is not affected
list(PREPEND CMAKE_PREFIX_PATH "${POC035_DEPS_PREFIX}")
find_package(SDL3 CONFIG REQUIRED PATHS "${POC035_DEPS_PREFIX}" NO_DEFAULT_PATH)
find_package(Freetype QUIET)
find_package(RmlUi CONFIG QUIET PATHS "${POC035_DEPS_PREFIX}" NO_DEFAULT_PATH)
find_package(OpenGL REQUIRED)
find_package(Qt6 COMPONENTS Gui Widgets Quick Qml)
if (Qt6_FOUND)
    set(QT_NO_PRIVATE_MODULE_WARNING ON)  # deliberate, POC only
    find_package(Qt6 COMPONENTS GuiPrivate)  # <rhi/qrhi.h>: QRhi::finish() ends a measured Quick frame on the GPU
endif ()
if (Qt6_FOUND AND Qt6Quick_VERSION VERSION_LESS 6.7)
    message(WARNING "POC 035: Qt ${Qt6Quick_VERSION}: the QSGTextNode variant needs Qt 6.7+, poc035-qtquick-sg skipped")
endif ()

set(_poc035_cpp "${CMAKE_CURRENT_SOURCE_DIR}/cpp")
set(_poc035_font "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/DejaVuSansMono.ttf")
set(_poc035_targets "")

# Our code: warnings on; third-party sources go into their own libraries compiled with -w (like hidapi in the companions)
function(poc035_flags target)
    target_compile_features(${target} PRIVATE cxx_std_20)
    target_compile_definitions(${target} PRIVATE POC035_FONT_PATH="${_poc035_font}" GL_SILENCE_DEPRECATION=1)
    if (MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else ()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif ()
    set_target_properties(${target} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/poc035"
                                               MACOSX_BUNDLE OFF BUILD_RPATH "${POC035_DEPS_PREFIX}/lib")
endfunction()

function(poc035_quiet target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PUBLIC GL_SILENCE_DEPRECATION=1)
    if (MSVC)
        target_compile_options(${target} PRIVATE /w)
    else ()
        target_compile_options(${target} PRIVATE -w)
    endif ()
endfunction()

# --- shared harness + workload -------------------------------------------------------------------------------------
set(_poc035_common_src ${_poc035_cpp}/common/workload.cpp ${_poc035_cpp}/common/harness.cpp)
if (APPLE)
    list(APPEND _poc035_common_src ${_poc035_cpp}/common/platform-macos.mm)
endif ()
add_library(poc035-common STATIC EXCLUDE_FROM_ALL ${_poc035_common_src})
poc035_flags(poc035-common)
target_include_directories(poc035-common PUBLIC ${_poc035_cpp}/common)
target_include_directories(poc035-common SYSTEM PUBLIC ${_poc035_cpp}/3rdparty/stb)
if (APPLE)
    target_link_libraries(poc035-common PUBLIC "-framework Foundation")
endif ()

# --- Dear ImGui ----------------------------------------------------------------------------------------------------
if (EXISTS "${POC035_IMGUI_DIR}/imgui.cpp")
    add_library(poc035-imgui-lib STATIC EXCLUDE_FROM_ALL
        ${POC035_IMGUI_DIR}/imgui.cpp ${POC035_IMGUI_DIR}/imgui_draw.cpp ${POC035_IMGUI_DIR}/imgui_tables.cpp
        ${POC035_IMGUI_DIR}/imgui_widgets.cpp ${POC035_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
        ${POC035_IMGUI_DIR}/backends/imgui_impl_opengl3.cpp)
    poc035_quiet(poc035-imgui-lib)
    target_include_directories(poc035-imgui-lib SYSTEM PUBLIC ${POC035_IMGUI_DIR} ${POC035_IMGUI_DIR}/backends)
    target_link_libraries(poc035-imgui-lib PUBLIC SDL3::SDL3 OpenGL::GL)

    add_executable(poc035-imgui EXCLUDE_FROM_ALL ${_poc035_cpp}/imgui/main.cpp)
    poc035_flags(poc035-imgui)
    target_link_libraries(poc035-imgui PRIVATE poc035-common poc035-imgui-lib)
    list(APPEND _poc035_targets poc035-imgui)
else ()
    message(WARNING "POC 035: no Dear ImGui sources in ${POC035_IMGUI_DIR}, poc035-imgui skipped")
endif ()

# --- RmlUi: DOM views and custom-element views ---------------------------------------------------------------------
if (RmlUi_FOUND AND EXISTS "${POC035_RMLUI_SRC}/Backends/RmlUi_Backend_SDL_GL3.cpp")
    add_library(poc035-rmlui-backend STATIC EXCLUDE_FROM_ALL
        ${POC035_RMLUI_SRC}/Backends/RmlUi_Backend_SDL_GL3.cpp ${POC035_RMLUI_SRC}/Backends/RmlUi_Platform_SDL.cpp
        ${POC035_RMLUI_SRC}/Backends/RmlUi_Renderer_GL3.cpp)
    poc035_quiet(poc035-rmlui-backend)
    target_include_directories(poc035-rmlui-backend SYSTEM PUBLIC ${POC035_RMLUI_SRC}/Backends ${_poc035_cpp}/rmlui/stub)
    target_compile_definitions(poc035-rmlui-backend PUBLIC RMLUI_SDL_VERSION_MAJOR=3)
    target_link_libraries(poc035-rmlui-backend PUBLIC RmlUi::RmlUi SDL3::SDL3 OpenGL::GL ${CMAKE_DL_LIBS})
    foreach (mode dom custom)
        add_executable(poc035-rmlui-${mode} EXCLUDE_FROM_ALL ${_poc035_cpp}/rmlui/main.cpp ${_poc035_cpp}/rmlui/elements.cpp)
        poc035_flags(poc035-rmlui-${mode})
        string(TOUPPER ${mode} _mode)
        target_compile_definitions(poc035-rmlui-${mode} PRIVATE BENCH_RML_MODE_${_mode}=1
                                   BENCH_RML_DIR="${_poc035_cpp}/rmlui")
        target_link_libraries(poc035-rmlui-${mode} PRIVATE poc035-common poc035-rmlui-backend)
        list(APPEND _poc035_targets poc035-rmlui-${mode})
    endforeach ()
else ()
    message(WARNING "POC 035: RmlUi not found in ${POC035_DEPS_PREFIX}, the RmlUi variants skipped")
endif ()

# --- Qt 6 -----------------------------------------------------------------------------------------------------------
if (TARGET Qt6::Widgets)
    add_executable(poc035-qtwidgets EXCLUDE_FROM_ALL ${_poc035_cpp}/qtwidgets/main.cpp)
    add_executable(poc035-qtwidgets-raster EXCLUDE_FROM_ALL ${_poc035_cpp}/qtwidgets-raster/main.cpp
                   ${_poc035_cpp}/qtcommon/rasterviews.h)
    foreach (t poc035-qtwidgets poc035-qtwidgets-raster)
        poc035_flags(${t})
        target_link_libraries(${t} PRIVATE poc035-common Qt6::Widgets)
        list(APPEND _poc035_targets ${t})
    endforeach ()
endif ()

if (TARGET Qt6::Quick AND TARGET Qt6::GuiPrivate)
    add_executable(poc035-qtquick EXCLUDE_FROM_ALL ${_poc035_cpp}/qtquick/main.cpp ${_poc035_cpp}/qtquick/items.cpp
                   ${_poc035_cpp}/qtquick/items.h ${_poc035_cpp}/qtquick/qml.qrc)
    add_executable(poc035-qtquick-raster EXCLUDE_FROM_ALL ${_poc035_cpp}/qtquick-raster/main.cpp
                   ${_poc035_cpp}/qtquick-raster/rasteritem.cpp ${_poc035_cpp}/qtquick-raster/rasteritem.h
                   ${_poc035_cpp}/qtquick-raster/qml.qrc)
    set(_quick poc035-qtquick poc035-qtquick-raster)
    if (Qt6Quick_VERSION VERSION_GREATER_EQUAL 6.7)
        add_executable(poc035-qtquick-sg EXCLUDE_FROM_ALL ${_poc035_cpp}/qtquick-sg/main.cpp
                       ${_poc035_cpp}/qtquick-sg/items.cpp ${_poc035_cpp}/qtquick-sg/items.h ${_poc035_cpp}/qtquick-sg/qml.qrc)
        list(APPEND _quick poc035-qtquick-sg)
    endif ()
    foreach (t ${_quick})
        poc035_flags(${t})
        set_target_properties(${t} PROPERTIES AUTOMOC ON AUTORCC ON)
        target_link_libraries(${t} PRIVATE poc035-common Qt6::Quick Qt6::Qml Qt6::Gui Qt6::GuiPrivate)
        list(APPEND _poc035_targets ${t})
    endforeach ()
endif ()

if (_poc035_targets)
    add_custom_target(poc035-all DEPENDS ${_poc035_targets})
    message(STATUS "POC 035: ${_poc035_targets}")
endif ()
