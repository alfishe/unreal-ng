cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-screen-viewer CMakeLists.txt")

# Project named 'unreal-screen-viewer' on C++
project(unreal-screen-viewer LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif ()

# Set paths
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ========================================
# Platform-specific Qt path detection
# ========================================

# Default Qt paths (can be overridden via command line)
set(QT_FOUND_PATH "")

# Check if QT_INSTALL_PATH is defined and not empty
if (NOT DEFINED QT_INSTALL_PATH)
    set(QT_INSTALL_PATH "")
endif()

if (QT_INSTALL_PATH STREQUAL "")
    if (APPLE)  # macOS
        set(QT_POSSIBLE_PATHS
            "$ENV{HOME}/Qt/6.9.3/macos"
            "$ENV{HOME}/Qt/6.5.3/macos"
            "$ENV{HOME}/Qt/6.5.0/macos"
            "$ENV{QTDIR}"
        )
    elseif (UNIX AND NOT APPLE)  # Linux
        set(QT_POSSIBLE_PATHS
            "$ENV{HOME}/Qt/6.9.3/gcc_64"
            "$ENV{HOME}/Qt/6.5.3/gcc_64"
            "$ENV{HOME}/Qt/6.5.0/gcc_64"
            "$ENV{QTDIR}"
        )
    elseif (WIN32)  # Windows
        set(QT_POSSIBLE_PATHS
            "C:/Qt/6.9.3/msvc2019_64"
            "C:/Qt/6.5.3/msvc2019_64"
            "C:/Qt/6.5.0/msvc2019_64"
            "$ENV{QTDIR}"
        )
    endif ()
    
    # Try to find Qt6Config.cmake in possible paths
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

# If still not found, provide helpful error message
if(NOT QT_FOUND_PATH)
    message(FATAL_ERROR "Qt6 not found. Please install Qt6 or set QT_INSTALL_PATH manually.")
endif()

# Allow overriding via -DQT_INSTALL_PATH=...
set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")

# Append Qt path to CMAKE_PREFIX_PATH
if(QT_FOUND_PATH)
    list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
    message(STATUS "Added Qt path to CMAKE_PREFIX_PATH: ${QT_FOUND_PATH}")
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
    src/ScreenViewer.cpp
    src/ModeToolbar.cpp
)

# Put binary to /bin subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Set common target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
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
        rt  # POSIX shared memory
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
message(STATUS "<unreal-screen-viewer diagnostics>")
message(STATUS "  PROJECT_NAME: ${PROJECT_NAME}")
message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "</unreal-screen-viewer diagnostics>")
