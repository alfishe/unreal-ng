cmake_minimum_required(VERSION 3.16.0)

message(">> unreal-videowall CMakeLists.txt")

# Project named 'unreal-videowall' on C++
project(unreal-videowall LANGUAGES CXX)

# Define global options - simpler than unreal-qt, only need CLI
set(ENABLE_AUTOMATION ON CACHE BOOL "Enable automation features" FORCE)
set(ENABLE_LUA_AUTOMATION OFF CACHE BOOL "Enable Lua automation" FORCE)
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

# Build core library
add_subdirectory("${ROOT_DIR}/core/src" core)

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
    "${ROOT_DIR}/core/src"
    "${ROOT_DIR}/core"
    "${ROOT_DIR}/core/automation"
)

# Add automation include directories if enabled
if (ENABLE_AUTOMATION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_AUTOMATION)

    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:ENABLE_CLI_AUTOMATION>
        $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:ENABLE_WEBAPI_AUTOMATION>
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
