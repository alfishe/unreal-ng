cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-videowall CMakeLists.txt")

# Project named 'unreal-videowall' on C++
project(unreal-videowall LANGUAGES CXX)

# Define automation options - use defaults if not already set by parent CMakeLists.txt
if (NOT DEFINED ENABLE_AUTOMATION)
    option(ENABLE_AUTOMATION "Enable automation features" ON)
endif()
if (NOT DEFINED ENABLE_LUA_AUTOMATION)
    option(ENABLE_LUA_AUTOMATION "Enable Lua automation" OFF)
endif()
if (NOT DEFINED ENABLE_PYTHON_AUTOMATION)
    option(ENABLE_PYTHON_AUTOMATION "Enable Python automation" OFF)
endif()
if (NOT DEFINED ENABLE_WEBAPI_AUTOMATION)
    option(ENABLE_WEBAPI_AUTOMATION "Enable WebAPI automation" ON)
endif()
if (NOT DEFINED ENABLE_CLI_AUTOMATION)
    option(ENABLE_CLI_AUTOMATION "Enable CLI automation" ON)
endif()

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (CMAKE_VERSION VERSION_LESS "3.7.0")
    set(CMAKE_INCLUDE_CURRENT_DIR ON)
endif ()

# Set paths
get_filename_component(ROOT_DIR ../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# Add automation if enabled (skip if target already exists from parent build)
if (ENABLE_AUTOMATION)
    if (NOT TARGET unrealng::automation)
        add_subdirectory("${ROOT_DIR}/core/automation" automation)
    endif()
    set(INCLUDE_AUTOMATION True)
endif ()

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
endif()

# Allow overriding via -DQT_INSTALL_PATH=...
set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")

# Append Qt path to CMAKE_PREFIX_PATH
if(QT_FOUND_PATH)
    list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
    message(STATUS "Added Qt path to CMAKE_PREFIX_PATH: ${QT_FOUND_PATH}")
endif()

# Qt specific settings
find_package(Qt6 REQUIRED COMPONENTS Widgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)
add_definitions( ${QT_DEFINITIONS} )
include_directories( ${CMAKE_BINARY_DIR} )

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

# Resolve base paths for Core library
get_filename_component(CORE_LIBRARY_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../core"
    ABSOLUTE
)
message("CORE_LIBRARY_PATH: ${CORE_LIBRARY_PATH}")

# Set include paths for compiler
include_directories(
    ${SOURCE_DIR}/
)

# Build core library (skip if target already exists from parent build)
if (NOT TARGET unrealng::core)
    add_subdirectory("${ROOT_DIR}/core/src" core)
endif()

# All sources in /src and subfolders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
set (SOURCES ${CPP_FILES})
message("CPP_FILES:${CPP_FILES}")

# Put binary to /bin subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${CPP_FILES})

# Set common target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}"  # For any generated headers
    "${ROOT_DIR}/core/src"
    "${ROOT_DIR}/core"
    "${ROOT_DIR}/core/automation"
)

# Add automation include directories if enabled
if (ENABLE_AUTOMATION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_AUTOMATION)

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:ENABLE_LUA_AUTOMATION>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:ENABLE_PYTHON_AUTOMATION>
        $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:ENABLE_WEBAPI_AUTOMATION>
        $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:ENABLE_CLI_AUTOMATION>
    )
endif ()

# Debug configuration
if (CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "CMAKE_BUILD_TYPE:${CMAKE_BUILD_TYPE} - defining '_DEBUG'")
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif ()

# Add include paths
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CORE_LIBRARY_PATH}/src/common
    ${CORE_LIBRARY_PATH}/src/emulator
)

# Link all dependencies
if (APPLE)
    target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
    set(common_libraries iconv)
    find_package(Threads REQUIRED)
elseif (UNIX AND NOT APPLE)
    set(common_libraries -static-libstdc++)
    find_package(Threads REQUIRED)
endif ()

# Configure automation if enabled
if (INCLUDE_AUTOMATION)
    # Link with appropriate libraries
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${common_libraries}
        Qt6::Widgets
        unrealng::automation
        ${CMAKE_DL_LIBS}
        Threads::Threads
    )
    
    # Link Winsock on Windows for automation
    if (WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    endif ()
else ()
    # Link without automation
    target_link_libraries(${PROJECT_NAME} PRIVATE 
        ${common_libraries}
        Qt6::Widgets
        unrealng::core
        Threads::Threads
    )
endif ()

# Debug output
message(STATUS "Binary: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}")

# Diagnostics
message(STATUS "<unreal-videowall diagnostics>")
message(STATUS "Options:")
message(STATUS "  ENABLE_AUTOMATION: ${ENABLE_AUTOMATION}")
message(STATUS "  ENABLE_CLI_AUTOMATION: ${ENABLE_CLI_AUTOMATION}")
message(STATUS "Settings:")
message(STATUS "  PROJECT_NAME: ${PROJECT_NAME}")
message(STATUS "</unreal-videowall diagnostics>")

# ========================================
# Post-build: Copy all required resource files
# ========================================
# Copy fonts, configuration, and ROM files needed for emulator operation
# This matches the setup from unreal-qt for consistency

# Define data paths
set(DATA_PATH "${ROOT_DIR}/data")
set(CONFIG_FILE "${DATA_PATH}/configs/pentagon512k/unreal.ini")

# Copy all required resources using modern CMake syntax
add_custom_command(
    TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/fonts" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy
        "${CONFIG_FILE}" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unreal.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/rom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${DATA_PATH}/testrom" "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom"
    COMMENT "Copying runtime resource files (fonts, config, ROM) to output directory"
)

message(STATUS "Post-build: Will copy resources from ${DATA_PATH} to ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
message(STATUS "  - Fonts: ${DATA_PATH}/fonts")
message(STATUS "  - Config: ${CONFIG_FILE}")
message(STATUS "  - ROM: ${DATA_PATH}/rom and ${DATA_PATH}/testrom")

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
    add_custom_target(package_videowall_macos
        COMMAND ${CMAKE_COMMAND} -E echo "Building macOS package for Video Wall..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND hdiutil create -volname "UnrealNG-VideoWall" -srcfolder "${CMAKE_BINARY_DIR}/bin/unreal-videowall.app" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall.dmg"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating macOS DMG package for Video Wall in ${CMAKE_BINARY_DIR}/packages"
    )
    
    # Create a macOS application bundle
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "UnrealNG Video Wall"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unrealng.videowall"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 UnrealNG Team"
    )
    
    # Copy resources to bundle
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/fonts" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/rom" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/rom"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/testrom" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${ROOT_DIR}/data/configs/pentagon512k/unreal.ini" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/unreal.ini"
        COMMENT "Copying resources to Video Wall app bundle"
    )
endif()

# Windows specific packaging
if(WIN32)
    # Create a custom target for Windows packaging
    add_custom_target(package_videowall_windows
        COMMAND ${CMAKE_COMMAND} -E echo "Building Windows package for Video Wall..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.exe" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/fonts" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/rom" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal.ini" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall/unreal.ini"
        COMMAND ${CMAKE_COMMAND} -E tar "cf" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall-Windows.zip" "--format=zip" "--" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating Windows package for Video Wall in ${CMAKE_BINARY_DIR}/packages"
    )
endif()

# Linux specific packaging
if(UNIX AND NOT APPLE)
    # Create a custom target for Linux packaging
    add_custom_target(package_videowall_linux
        COMMAND ${CMAKE_COMMAND} -E echo "Building Linux package for Video Wall..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages/unreal-videowall"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal-videowall" "${CMAKE_BINARY_DIR}/packages/unreal-videowall/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/fonts" "${CMAKE_BINARY_DIR}/packages/unreal-videowall/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/rom" "${CMAKE_BINARY_DIR}/packages/unreal-videowall/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal.ini" "${CMAKE_BINARY_DIR}/packages/unreal-videowall/unreal.ini"
        COMMAND ${CMAKE_COMMAND} -E tar "cfz" "${CMAKE_BINARY_DIR}/packages/UnrealNG-VideoWall-Linux.tar.gz" "--" "${CMAKE_BINARY_DIR}/packages/unreal-videowall"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating Linux package for Video Wall in ${CMAKE_BINARY_DIR}/packages"
    )
endif()
