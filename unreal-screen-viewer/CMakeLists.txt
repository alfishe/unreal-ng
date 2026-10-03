cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-screen-viewer CMakeLists.txt")

# Project named 'unreal-screen-viewer' on C++
# Minimum macOS version (must precede project(); shared with the root build)
include(${CMAKE_CURRENT_LIST_DIR}/../cmake/MacOSDeploymentTarget.cmake)

project(unreal-screen-viewer LANGUAGES CXX)

# C++ standard is inherited from root CMakeLists.txt (C++20)

if (CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif ()

# Set paths
get_filename_component(ROOT_DIR ../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Use the same discovery rules in suite and standalone builds.
include(${ROOT_DIR}/cmake/QtDiscovery.cmake)

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
    src/crashhandler/crashhandler.cpp
    $<$<PLATFORM_ID:Windows>:src/crashhandler/crashhandler_windows.cpp>
    $<$<PLATFORM_ID:Darwin>:src/crashhandler/crashhandler_macos.cpp>
    $<$<PLATFORM_ID:Linux>:src/crashhandler/crashhandler_linux.cpp>
)

# Put binary to /bin subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Application icon (shared with unreal-qt) as a Qt resource: window / taskbar
# icon on Windows and Linux. AUTORCC is off, so the .qrc is compiled explicitly.
qt6_add_resources(APP_ICON_RESOURCES "${ROOT_DIR}/unreal-qt/resources/appicon.qrc")
target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_RESOURCES})

# Set common target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_BINARY_DIR}"  # For Qt-generated headers when built from root
    "${CMAKE_BINARY_DIR}/generated"  # Configure-time generated headers (emulator/buildinfo.h)
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
        dbghelp
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
    # Find macdeployqt from Qt installation
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt
        HINTS "${QT_INSTALL_PATH}/bin" "${QT_FOUND_PATH}/bin"
    )
    
    # Create a custom target for macOS packaging
    set(SCREEN_VIEWER_STAGING_DIR "${CMAKE_BINARY_DIR}/packages/ScreenViewer-Staging")
    add_custom_target(package_screen_viewer_macos
        # 1. Staging Setup
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND ${CMAKE_COMMAND} -E remove_directory "${SCREEN_VIEWER_STAGING_DIR}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SCREEN_VIEWER_STAGING_DIR}"
        
        # 2. Copy App Bundle (preserving symlinks)
        COMMAND cp -a "${CMAKE_BINARY_DIR}/bin/unreal-screen-viewer.app" "${SCREEN_VIEWER_STAGING_DIR}/Screen Viewer.app"
        
        # 3. Deploy Qt frameworks
        COMMAND "${MACDEPLOYQT_EXECUTABLE}" "${SCREEN_VIEWER_STAGING_DIR}/Screen Viewer.app" -always-overwrite
        
        # 4. Sign with hardened runtime and entitlements
        COMMAND codesign --force --deep --options runtime --entitlements "${ROOT_DIR}/install/macos/entitlements.plist" --sign - "${SCREEN_VIEWER_STAGING_DIR}/Screen Viewer.app"
        
        # 5. Create DMG from staging
        COMMAND hdiutil create -volname "UnrealNG-ScreenViewer" -srcfolder "${SCREEN_VIEWER_STAGING_DIR}" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG-ScreenViewer.dmg"
        
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating individual macOS DMG package for Screen Viewer (signed with entitlements)"
    )
    
    # Create a macOS application bundle
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "UnrealNG Screen Viewer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "unreal.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unrealng.screen-viewer"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 UnrealNG Team"
    )
    
    # Copy the icon file to the bundle Resources
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${ROOT_DIR}/unreal-qt/install/macos/unreal.icns" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/"
        COMMENT "Copying icon to Screen Viewer app bundle"
    )
endif()

# Windows specific packaging
if(WIN32)
    # Include the shared Windows Qt deployment module
    include(${ROOT_DIR}/cmake/WinDeployQt.cmake)
    setup_windows_qt_deployment(${PROJECT_NAME})

    # Windows RC file for the executable icon (shared with unreal-qt) and version information
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/install/windows/app.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
    
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

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/QtLinuxDeployment.cmake)
