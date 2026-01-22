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
# (Skip if included from parent - Qt already configured there)
# ========================================
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Default Qt paths (can be overridden via command line)
    set(QT_FOUND_PATH "")
    
    # Check if QT_INSTALL_PATH is defined and not empty
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
    "${CMAKE_CURRENT_BINARY_DIR}"  # For Qt-generated headers when built from root
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

# ======================================================
# Installer Configuration
# ======================================================

# Include CPack for packaging
include(InstallRequiredSystemLibraries)

# Set package output directory
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")

# Package version information
set(PACKAGE_VERSION "1.0.0")
set(PACKAGE_VERSION_MAJOR "1")
set(PACKAGE_VERSION_MINOR "0")
set(PACKAGE_VERSION_PATCH "0")

# ======================================================
# Platform-specific installer configurations
# ======================================================

# macOS specific packaging
if(APPLE)
    # Create a custom target for macOS packaging
    add_custom_target(package_screen_viewer_macos
        COMMAND ${CMAKE_COMMAND} -E echo "Building macOS package for Screen Viewer..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND hdiutil create -volname "UnrealNG-ScreenViewer" -srcfolder "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.app" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer.dmg"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating macOS DMG package for Screen Viewer in ${CMAKE_BINARY_DIR}/packages"
    )
    
    # Create a macOS application bundle
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "UnrealNG Screen Viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unrealng.screen-viewer"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 UnrealNG Team"
    )
endif()

# Windows specific packaging
if(WIN32)
    # Create a custom target for Windows packaging
    add_custom_target(package_screen_viewer_windows
        COMMAND ${CMAKE_COMMAND} -E echo "Building Windows package for Screen Viewer..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.exe" "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer/"
        COMMAND ${CMAKE_COMMAND} -E tar "cf" "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer-Windows.zip" "--format=zip" "--" "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating Windows package for Screen Viewer in ${CMAKE_BINARY_DIR}/packages"
    )
endif()

# Linux specific packaging
if(UNIX AND NOT APPLE)
    # Create a custom target for Linux packaging
    add_custom_target(package_screen_viewer_linux
        COMMAND ${CMAKE_COMMAND} -E echo "Building Linux package for Screen Viewer..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer" "${CMAKE_BINARY_DIR}/packages/unreal-screen-viewer"
        COMMAND ${CMAKE_COMMAND} -E tar "cfz" "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer-Linux.tar.gz" "--" "${CMAKE_BINARY_DIR}/packages/unreal-screen-viewer"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating Linux package for Screen Viewer in ${CMAKE_BINARY_DIR}/packages"
    )
endif()
