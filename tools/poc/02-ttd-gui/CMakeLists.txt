# tools/poc/02-ttd-gui — Qt TTD Scrubber & Screen Renderer PoC.
#
# A standalone Qt6 app for offline .ttd file scrubbing with video-editor-style
# timeline and instant ZX Spectrum screen rendering. No emulation — pure data.
#
# Built only when -DBUILD_POC=ON (default OFF).
cmake_minimum_required(VERSION 3.16)

project(poc_ttd_gui LANGUAGES CXX)

message(">> tools/poc/02-ttd-gui CMakeLists.txt")

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Output goes to the same bin/ as the rest of the project.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Locate the project root (we live at <root>/tools/poc/02-ttd-gui/).
get_filename_component(ROOT_DIR "../../../.." ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
message(STATUS "poc_ttd_gui: ROOT_DIR=${ROOT_DIR}")

# Qt6 — AUTOMOC for signals/slots.
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

# When built as part of the parent project, CMAKE_PREFIX_PATH is already
# set for Qt6. When built standalone, add Homebrew path as fallback.
if(NOT DEFINED Qt6_DIR AND APPLE)
    if(EXISTS "/opt/homebrew/lib/cmake/Qt6/Qt6Config.cmake")
        list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew")
    endif()
endif()

find_package(Qt6 COMPONENTS Widgets Gui REQUIRED)

# zstd — link against the embedded copy (same as 01-ttd-compression).
if(NOT TARGET zstd::libzstd)
    add_subdirectory(
        ${ROOT_DIR}/core/src/3rdparty/zstd
        ${CMAKE_CURRENT_BINARY_DIR}/embedded-zstd
    )
endif()

# ---------------------------------------------------------------------------
# Source files
# ---------------------------------------------------------------------------
set(POC_GUI_SOURCES
    src/main.cpp
    src/ttd/ttd_reader.cpp
    src/ttd/ttd_materialize.cpp
    src/ttd/screen_renderer.cpp
    src/model/marks_model.cpp
    src/widgets/main_window.cpp
    src/widgets/timeline_widget.cpp
    src/widgets/screen_widget.cpp
)

add_executable(poc_ttd_gui ${POC_GUI_SOURCES})

target_include_directories(poc_ttd_gui PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(poc_ttd_gui PRIVATE
    Qt6::Widgets
    Qt6::Gui
    zstd::libzstd
)

# macOS: need to link against the Cocoa framework for QApplication
if(APPLE)
    target_link_libraries(poc_ttd_gui PRIVATE
        "-framework Cocoa"
        "-framework AppKit"
    )
endif()
