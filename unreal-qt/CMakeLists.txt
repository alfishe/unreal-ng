cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-qt CMakeLists.txt")

# Project named 'core' on C++
project(unreal-qt LANGUAGES CXX)

# Define global options with CACHE and FORCE to ensure they propagate to /core/automation projects
set(ENABLE_AUTOMATION ON CACHE BOOL "Enable automation features (Lua, Python, WebAPI)" FORCE)
set(ENABLE_LUA_AUTOMATION ON CACHE BOOL "Enable Lua automation" FORCE)
set(ENABLE_PYTHON_AUTOMATION OFF CACHE BOOL "Enable Python automation" FORCE)
set(ENABLE_WEBAPI_AUTOMATION ON CACHE BOOL "Enable WebAPI automation" FORCE)
set(ENABLE_CLI_AUTOMATION ON CACHE BOOL "Enable CLI automation" FORCE)

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

# Add automation if enabled
if (ENABLE_AUTOMATION)
    add_subdirectory("${ROOT_DIR}/core/automation" automation)

    set(INCLUDE_AUTOMATION True)
endif ()

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
        # Check common Qt6 versions in ~/Qt (most recent first)
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
    # User specified a path (could be a list or single path)
    # In CMake, lists are semicolon-separated, so we can iterate directly
    # Check each path in the list (works for both single path and list)
    foreach(POSSIBLE_PATH ${QT_INSTALL_PATH})
        # Remove any whitespace
        string(STRIP "${POSSIBLE_PATH}" POSSIBLE_PATH)
        if(EXISTS "${POSSIBLE_PATH}/lib/cmake/Qt6/Qt6Config.cmake")
            set(QT_FOUND_PATH "${POSSIBLE_PATH}")
            message(STATUS "Found Qt6 at user-specified path: ${QT_FOUND_PATH}")
            break()
        else()
            message(STATUS "Checking Qt path: ${POSSIBLE_PATH} (Qt6Config.cmake not found)")
        endif()
    endforeach()
endif ()

# If still not found, provide helpful error message
if(NOT QT_FOUND_PATH)
    message(WARNING "Qt6 not found. Please install Qt6 or set QT_INSTALL_PATH manually.")
    if(DEFINED QT_INSTALL_PATH AND NOT QT_INSTALL_PATH STREQUAL "")
        message(WARNING "Checked user-specified paths:")
        foreach(POSSIBLE_PATH ${QT_INSTALL_PATH})
            message(WARNING "  - ${POSSIBLE_PATH}")
        endforeach()
    else()
        message(WARNING "Checked standard paths:")
        foreach(POSSIBLE_PATH ${QT_POSSIBLE_PATHS})
            message(WARNING "  - ${POSSIBLE_PATH}")
        endforeach()
    endif()
endif()

# Allow overriding via -DQT_INSTALL_PATH=...
set(QT_INSTALL_PATH "${QT_FOUND_PATH}" CACHE PATH "Path to Qt installation")

# Append Qt path to CMAKE_PREFIX_PATH if found
if(QT_FOUND_PATH)
    list(APPEND CMAKE_PREFIX_PATH "${QT_FOUND_PATH}")
    message(STATUS "Added Qt path to CMAKE_PREFIX_PATH: ${QT_FOUND_PATH}")
endif()

# Qt specific settings
find_package(Qt6 REQUIRED)
find_package(Qt6 COMPONENTS Gui REQUIRED)
find_package(Qt6 COMPONENTS Multimedia REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# Enable CTest for test discovery (inherited from core/tests)
enable_testing()
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${SOURCE_DIR}/ui)
add_definitions( ${QT_DEFINITIONS} )
include_directories( ${CMAKE_BINARY_DIR} )

# Default to Release mode
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release)
endif ()

# Resolve base paths for Core library and Data
get_filename_component(CORE_LIBRARY_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../core"
    ABSOLUTE
)
message("CORE_LIBRARY_PATH: ${CORE_LIBRARY_PATH}")
get_filename_component(DATA_PATH
    "${CMAKE_CURRENT_LIST_DIR}/../data"
    ABSOLUTE
)
message("DATA_PATH: ${DATA_PATH}")

# Set include paths for compiler
include_directories(
    ${SOURCE_DIR}/
    ${SOURCE_DIR}/3rdparty/QHexView-5.0
)

# Build all dependency libraries
add_subdirectory("3rdparty/QHexView-5.0")
add_subdirectory("${ROOT_DIR}/core/src" core)
add_subdirectory("${ROOT_DIR}/core/tests" tests)

# All sources in /src and subfolders will be compiled
file (GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES) # Exclude /build folder
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES) # Exclude /CMakeFile autogenerated files
set (SOURCES ${CPP_FILES})
message("CPP_FILES:${CPP_FILES}")

# UI files
file (GLOB_RECURSE UI_FILES ${SOURCE_DIR}/*.ui)
message("UI_FILES:${UI_FILES}")
QT6_WRAP_UI(UI_HEADERS ${UI_FILES})
message("UI_HEADERS:${UI_HEADERS}")

# Put binary and configuration files to /bin subfolder
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Create the executable
add_executable(${PROJECT_NAME} ${CPP_FILES} ${UI_HEADERS})


# Set common target properties
target_include_directories(${PROJECT_NAME} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty"
    "${ROOT_DIR}/core/src"
    "${ROOT_DIR}/core"  # Added parent directory for proper automation includes
    "${ROOT_DIR}/core/automation"  # For automation headers
    "${ROOT_DIR}/core/automation/lua/3rdparty/lua/lua-5.4.6/include"  # For Lua headers
    "${ROOT_DIR}/core/automation/lua/3rdparty/sol2/include"  # For sol2 headers
)

# Add automation include directories if enabled
if (ENABLE_AUTOMATION)
    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::automation,INTERFACE_INCLUDE_DIRECTORIES>
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_AUTOMATION)

    # Must exactly match the definitions in core/automation/CMakeLists.txt!!!
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
    ${CORE_LIBRARY_PATH}/src
    3rdparty/QHexView-5.0/include
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
    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::automation,INTERFACE_INCLUDE_DIRECTORIES>
    )

    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_AUTOMATION)

    # Link with appropriate libraries based on automation type
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${common_libraries}
        Qt6::Widgets
        Qt6::Multimedia
        QHexView

        # unrealng::core # REMOVED - comes through automation
        unrealng::automation # This brings in core as PUBLIC dependency
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
        Qt6::Multimedia
        QHexView

        unrealng::core  # Keep this when automation is disabled
        Threads::Threads
    )
endif ()

# Define data paths
set(DATA_PATH "${ROOT_DIR}/data")
set(CONFIG_FILE "${DATA_PATH}/configs/pentagon512k/unreal.ini")

# Copy resources using modern CMake syntax
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
    COMMENT "Copying runtime resource files to output directory"
)

# These install rules are already covered by the common install rules above

# Debug output
message(STATUS "CONFIG_FILE: ${CONFIG_FILE}")
message(STATUS "Binary: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}")

# ======================================================
# Installer Configuration
# ======================================================

# Include CPack for packaging
# No CPack needed for direct packaging
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
    add_custom_target(package_macos
        COMMAND ${CMAKE_COMMAND} -E echo "Building macOS package..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND hdiutil create -volname "UnrealNG" -srcfolder "${CMAKE_BINARY_DIR}/bin/unreal-qt.app" -ov -format UDZO "${CMAKE_BINARY_DIR}/packages/UnrealNG.dmg"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating macOS DMG package in ${CMAKE_BINARY_DIR}/packages"
    )
    
    # Create a macOS application bundle
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "UnrealNG"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PACKAGE_VERSION}"
        MACOSX_BUNDLE_ICON_FILE "unreal.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.unrealng.emulator"
        MACOSX_BUNDLE_COPYRIGHT "Copyright © 2025 UnrealNG Team"
    )
    
    # Copy the icon file and required resources to the bundle
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_CURRENT_SOURCE_DIR}/install/macos/unreal.icns" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/fonts" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/rom" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/rom"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${ROOT_DIR}/data/testrom" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${ROOT_DIR}/data/configs/pentagon512k/unreal.ini" "$<TARGET_FILE_DIR:${PROJECT_NAME}>/../Resources/unreal.ini"
    )
endif()

# Windows specific packaging
if(WIN32)
    # Create a custom target for Windows packaging that builds without tests
    add_custom_target(package_windows
        COMMAND ${CMAKE_COMMAND} -E echo "Building Windows package..."
        # First configure and build without tests
        COMMAND ${CMAKE_COMMAND} -DBUILD_TESTS=OFF ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target ${PROJECT_NAME}
        # Create package directory
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages/UnrealNG"
        # Only copy the main executable
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/${PROJECT_NAME}.exe" "${CMAKE_BINARY_DIR}/packages/UnrealNG/"
        # Copy only necessary resources
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/fonts" "${CMAKE_BINARY_DIR}/packages/UnrealNG/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/rom" "${CMAKE_BINARY_DIR}/packages/UnrealNG/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal.ini" "${CMAKE_BINARY_DIR}/packages/UnrealNG/unreal.ini"
        # Create the final zip package
        COMMAND ${CMAKE_COMMAND} -E tar "cf" "${CMAKE_BINARY_DIR}/packages/UnrealNG-Windows.zip" "--format=zip" "--" "${CMAKE_BINARY_DIR}/packages/UnrealNG"
        COMMENT "Creating Windows package in ${CMAKE_BINARY_DIR}/packages"
    )
    
    # Add Windows RC file for icon and version information
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/src/install/windows/app.rc.in"
                   "${CMAKE_CURRENT_BINARY_DIR}/app.rc" @ONLY)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/app.rc")
endif()

# Linux specific packaging
if(UNIX AND NOT APPLE)
    # Create a custom target for Linux packaging
    add_custom_target(package_linux
        COMMAND ${CMAKE_COMMAND} -E echo "Building Linux package..."
        COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal-qt" "${CMAKE_BINARY_DIR}/packages/unreal-qt"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/fonts" "${CMAKE_BINARY_DIR}/packages/fonts"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_BINARY_DIR}/bin/rom" "${CMAKE_BINARY_DIR}/packages/rom"
        COMMAND ${CMAKE_COMMAND} -E copy "${CMAKE_BINARY_DIR}/bin/unreal.ini" "${CMAKE_BINARY_DIR}/packages/unreal.ini"
        COMMAND ${CMAKE_COMMAND} -E tar "cfz" "${CMAKE_BINARY_DIR}/packages/UnrealNG-Linux.tar.gz" "--" "${CMAKE_BINARY_DIR}/packages/unreal-qt" "${CMAKE_BINARY_DIR}/packages/fonts" "${CMAKE_BINARY_DIR}/packages/rom" "${CMAKE_BINARY_DIR}/packages/unreal.ini"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Creating Linux package in ${CMAKE_BINARY_DIR}/packages"
    )
    # No desktop integration for simple packaging
endif()

# Add a general packaging target that builds the appropriate package for the current platform
add_custom_target(package_all
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/packages"
    COMMAND ${CMAKE_CPACK_COMMAND} -C $<CONFIG> --config "${CMAKE_BINARY_DIR}/CPackConfig.cmake" -B "${CMAKE_BINARY_DIR}/packages"
    DEPENDS ${PROJECT_NAME}
    COMMENT "Creating package for current platform in ${CMAKE_BINARY_DIR}/packages"
)

# <Diagnostics>
message(STATUS "<unreal-qt diagnostics>")
message(STATUS "Options:")
message(STATUS "  ENABLE_AUTOMATION: ${ENABLE_AUTOMATION}")
message(STATUS "    ENABLE_LUA_AUTOMATION: ${ENABLE_LUA_AUTOMATION}")
message(STATUS "    ENABLE_PYTHON_AUTOMATION: ${ENABLE_PYTHON_AUTOMATION}")
message(STATUS "    ENABLE_WEBAPI_AUTOMATION: ${ENABLE_WEBAPI_AUTOMATION}")
message(STATUS "    ENABLE_CLI_AUTOMATION: ${ENABLE_CLI_AUTOMATION}")
message(STATUS "Settings:")
message(STATUS "  PROJECT_NAME:        ${PROJECT_NAME}")
message(STATUS "  SRC_DIR:             ${SRC_DIR}")
message(STATUS "  LIB_DIR:             ${LIB_DIR}")

message(STATUS "  INCLUDE_DIRECTORIES:")
get_property(include_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach (dir ${include_dirs})
    message(STATUS "                       ${dir}")
endforeach ()

message(STATUS "</unreal-qt diagnostics>")
# </Diagnostics>