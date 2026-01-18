# CMake entry point - Unified build for all unreal-ng targets
cmake_minimum_required(VERSION 3.16)

message(">> Unreal CMakeLists.txt (Unified Build)")

# Set C++ standard to 17 globally
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ========================================
# Build Options
# ========================================
option(TESTS "Enable tests" ON)
option(BENCHMARKS "Enable benchmarks" ON)
option(BUILD_QT_APPS "Build Qt-based applications (unreal-qt, unreal-screen-viewer)" ON)

# Automation options (enabled by default for full builds)
option(ENABLE_AUTOMATION "Enable automation features (Lua, Python, WebAPI, CLI)" ON)
option(ENABLE_LUA_AUTOMATION "Enable Lua automation" ON)
option(ENABLE_PYTHON_AUTOMATION "Enable Python automation" ON)
option(ENABLE_WEBAPI_AUTOMATION "Enable WebAPI automation" ON)
option(ENABLE_CLI_AUTOMATION "Enable CLI automation" ON)

project(unreal)

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE Release)
endif (NOT CMAKE_BUILD_TYPE)

message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")

# Disable modern SDK warnings about insecurity for POSIX functions
if (MSVC)
	add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif (MSVC)

# Set the output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Include CMake helper functions
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/func.cmake)

# ========================================
# Qt Detection (for Qt-based applications)
# ========================================
if (BUILD_QT_APPS)
    set(QT_FOUND_PATH "")
    
    if (NOT DEFINED QT_INSTALL_PATH OR QT_INSTALL_PATH STREQUAL "")
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
            set(QT_POSSIBLE_PATHS
                "C:/Qt/6.9.3/msvc2019_64"
                "C:/Qt/6.5.3/msvc2019_64"
                "C:/Qt/6.5.0/msvc2019_64"
                "$ENV{QTDIR}"
            )
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
    
    if(QT_FOUND_PATH)
        list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
        set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")
        message(STATUS "Qt6 configured at: ${QT_FOUND_PATH}")
    else()
        message(WARNING "Qt6 not found. Qt-based applications will not be built.")
        message(WARNING "Set -DQT_INSTALL_PATH=/path/to/Qt6 or install Qt6.")
        set(BUILD_QT_APPS OFF)
    endif()
endif ()

# ========================================
# Core Library
# ========================================
message(STATUS ">>> Building core library")
add_subdirectory(core/src)

# ========================================
# Automation Library (if enabled)
# ========================================
if (ENABLE_AUTOMATION)
    message(STATUS ">>> Building automation library")
    add_subdirectory(core/automation)
endif ()

# ========================================
# Emulator Test Console Client
# ========================================
message(STATUS ">>> Building testclient")
add_subdirectory(testclient)

# ========================================
# Qt-based Applications
# ========================================
if (BUILD_QT_APPS)
    message(STATUS ">>> Building unreal-qt")
    add_subdirectory(unreal-qt)
    
    message(STATUS ">>> Building unreal-screen-viewer")
    add_subdirectory(unreal-screen-viewer)
    
    message(STATUS ">>> Building unreal-videowall")
    add_subdirectory(unreal-videowall)
endif ()

# ========================================
# Unit Tests
# ========================================
if (TESTS)
	message(STATUS ">>> Building tests")
	add_subdirectory(core/tests)
endif (TESTS)

# ========================================
# Benchmarks
# ========================================
if (BENCHMARKS)
	message(STATUS ">>> Building benchmarks")
	# Prevent Google benchmark from building its own tests
	set(BENCHMARK_ENABLE_TESTING OFF)
	add_subdirectory(core/benchmarks)
endif (BENCHMARKS)

# ========================================
# Summary
# ========================================
message(STATUS "")
message(STATUS "========== Unreal-NG Build Configuration ==========")
message(STATUS "  BUILD_QT_APPS:              ${BUILD_QT_APPS}")
message(STATUS "  TESTS:                      ${TESTS}")
message(STATUS "  BENCHMARKS:                 ${BENCHMARKS}")
message(STATUS "  ENABLE_AUTOMATION:          ${ENABLE_AUTOMATION}")
if (ENABLE_AUTOMATION)
    message(STATUS "    ENABLE_LUA_AUTOMATION:    ${ENABLE_LUA_AUTOMATION}")
    message(STATUS "    ENABLE_PYTHON_AUTOMATION: ${ENABLE_PYTHON_AUTOMATION}")
    message(STATUS "    ENABLE_WEBAPI_AUTOMATION: ${ENABLE_WEBAPI_AUTOMATION}")
    message(STATUS "    ENABLE_CLI_AUTOMATION:    ${ENABLE_CLI_AUTOMATION}")
endif ()
message(STATUS "====================================================")