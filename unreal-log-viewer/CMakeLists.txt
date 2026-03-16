cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-log-viewer CMakeLists.txt")

project(unreal-log-viewer LANGUAGES CXX)

if (CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif ()

# Set paths
get_filename_component(ROOT_DIR ../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ========================================
# Platform-specific Qt path detection
# (Skip if included from parent - Qt already configured there)
# ========================================
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(QT_FOUND_PATH "")

    if (NOT DEFINED QT_INSTALL_PATH)
        set(QT_INSTALL_PATH "")
    endif()

    if (QT_INSTALL_PATH STREQUAL "")
        if (APPLE)
            set(QT_POSSIBLE_PATHS
                "$ENV{HOME}/Qt/6.9.3/macos"
                "$ENV{HOME}/Qt/6.5.3/macos"
                "$ENV{HOME}/Qt/6.5.0/macos"
                "$ENV{QTDIR}"
            )
        elseif (UNIX AND NOT APPLE)
            set(QT_POSSIBLE_PATHS
                "$ENV{HOME}/Qt/6.9.3/gcc_64"
                "$ENV{HOME}/Qt/6.5.3/gcc_64"
                "$ENV{HOME}/Qt/6.5.0/gcc_64"
                "$ENV{QTDIR}"
            )
        elseif (WIN32)
            if(MINGW)
                set(QT_POSSIBLE_PATHS
                    "C:/Qt/6.9.3/mingw_64"
                    "C:/Qt/6.5.3/mingw_64"
                    "C:/Qt/6.5.0/mingw_64"
                    "$ENV{QTDIR}"
                )
            elseif(MSVC)
                set(QT_POSSIBLE_PATHS
                    "C:/Qt/6.9.3/msvc2022_64"
                    "C:/Qt/6.9.3/msvc2019_64"
                    "C:/Qt/6.5.3/msvc2019_64"
                    "C:/Qt/6.5.0/msvc2019_64"
                    "$ENV{QTDIR}"
                )
            else()
                set(QT_POSSIBLE_PATHS
                    "C:/Qt/6.9.3/msvc2022_64"
                    "C:/Qt/6.9.3/mingw_64"
                    "C:/Qt/6.5.3/msvc2019_64"
                    "C:/Qt/6.5.3/mingw_64"
                    "$ENV{QTDIR}"
                )
            endif()
        endif ()

        foreach(POSSIBLE_PATH ${QT_POSSIBLE_PATHS})
            if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
                set(QT_FOUND_PATH "${POSSIBLE_PATH}")
                message(STATUS "Found Qt6 at: ${QT_FOUND_PATH}")
                break()
            endif()
        endforeach()
    else ()
        foreach(POSSIBLE_PATH ${QT_INSTALL_PATH})
            string(STRIP "${POSSIBLE_PATH}" POSSIBLE_PATH)
            if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
                set(QT_FOUND_PATH "${POSSIBLE_PATH}")
                message(STATUS "Found Qt6 at user-specified path: ${QT_FOUND_PATH}")
                break()
            endif()
        endforeach()
    endif ()

    if(NOT QT_FOUND_PATH)
        message(FATAL_ERROR "Qt6 not found. Please install Qt6 or set QT_INSTALL_PATH manually.")
    endif()

    set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")

    if(QT_FOUND_PATH)
        list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
        message(STATUS "Added Qt path to CMAKE_PREFIX_PATH: ${QT_FOUND_PATH}")
    endif()
endif()

# Qt specific settings
find_package(Qt6 REQUIRED COMPONENTS Widgets Network)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)
add_definitions(${QT_DEFINITIONS})
include_directories(${CMAKE_BINARY_DIR})

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif ()

# Set include paths for compiler
include_directories(
    ${SOURCE_DIR}/
)

# Source files
set(SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/WebAPIClient.cpp
    src/EmulatorList.cpp
    src/LogViewWidget.cpp
    src/CategoryPanel.cpp
    src/FramebufferWidget.cpp
    src/HeatmapWidget.cpp
    src/TraceWidget.cpp
    src/ChipStateWidget.cpp
    src/SparklineWidget.cpp
    src/EnvelopeShapeWidget.cpp
)

# Put binary to /bin subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Set common target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${ROOT_DIR}/core/src"    # Monitoring protocol headers (manifest.h, spsc_ringbuffer.h)
)

# Debug configuration
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif ()

# Link all dependencies
if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Network
        Threads::Threads
    )
elseif (UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Network
        rt
        Threads::Threads
    )
elseif (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Network
    )
endif ()

# Debug output
message(STATUS "Binary: ${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}")

# Diagnostics
message(STATUS "<unreal-log-viewer diagnostics>")
message(STATUS "  PROJECT_NAME: ${PROJECT_NAME}")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "</unreal-log-viewer diagnostics>")

# ======================================================
# macOS Application Bundle
# ======================================================
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "UnrealNG Log Viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unrealng.log-viewer"
        MACOSX_BUNDLE_COPYRIGHT "Copyright 2025-2026 UnrealNG Team"
    )
endif()
