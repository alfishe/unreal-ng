cmake_minimum_required(VERSION 3.14)

project(automation_lua VERSION 1.0.0 LANGUAGES CXX)

#------------------------------------------------------------------------------
# Options for building automation_lua
#------------------------------------------------------------------------------
# Option to control whether automation_lua is built as static or shared library
option(AUTOMATION_LUA_SHARED "Build automation_lua as shared library instead of static" OFF)

# Options to pass to Lua library (lib/lua)
option(LUA_BUILD_STATIC "Build static library for Lua" ON)
option(LUA_BUILD_SHARED "Build shared library for Lua" OFF)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define project root directory for consistent path references
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../..")

# Define core include directory
set(CORE_INCLUDE_DIR "${ROOT_DIR}/core/src/include")

#------------------------------------------------------------------------------
# Add dependencies
#------------------------------------------------------------------------------
# Add Lua library
add_subdirectory(lib/lua)

# Define sol2 include directory
set(SOL2_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/sol2/sol2-3.5.0/single/include")

#------------------------------------------------------------------------------
# Define source files
#------------------------------------------------------------------------------
set(AUTOMATION_LUA_SOURCES
    src/automation-lua.cpp
    # NOTE: automation-main.cpp is excluded because it contains a main() function
    # which conflicts when linking with other executables like unreal-qt
)

set(AUTOMATION_LUA_HEADERS
    src/automation-lua.h
    src/emulator/lua_emulator.h
)

#------------------------------------------------------------------------------
# Create library targets based on options
#------------------------------------------------------------------------------
# List of created targets for installation
set(AUTOMATION_LUA_TARGETS)

# Create the automation_lua library (either static or shared based on option)
if (AUTOMATION_LUA_SHARED)
    add_library(${PROJECT_NAME} SHARED ${AUTOMATION_LUA_SOURCES} ${AUTOMATION_LUA_HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Add platform-specific compile definitions for shared libraries
    if (WIN32)
        target_compile_definitions(${PROJECT_NAME} PRIVATE AUTOMATION_LUA_BUILD_DLL)
    endif ()
    
    # Link with Lua (prefer shared if available for shared library)
    if (TARGET lua::shared)
        target_link_libraries(${PROJECT_NAME} PUBLIC lua::shared)
    else ()
        target_link_libraries(${PROJECT_NAME} PUBLIC lua::static)
    endif ()
else()
    # Static library is the default
    add_library(${PROJECT_NAME} STATIC ${AUTOMATION_LUA_SOURCES} ${AUTOMATION_LUA_HEADERS})
    set_target_properties(${PROJECT_NAME} PROPERTIES
        VERSION ${PROJECT_VERSION}
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Link with Lua (prefer static if available for static library)
    if (TARGET lua::static)
        target_link_libraries(${PROJECT_NAME} PUBLIC lua::static)
    else ()
        target_link_libraries(${PROJECT_NAME} PUBLIC lua::shared)
    endif ()
endif()

# MinGW: sol2's heavy template usage can exceed the COFF section limit (65535 sections).
# The -Wa,-mbig-obj flag allows the assembler to create larger object files.
if (MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wa,-mbig-obj)
endif()

# Add include directories (same for both static and shared)
target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<BUILD_INTERFACE:${SOL2_INCLUDE_DIR}>
        $<INSTALL_INTERFACE:include>
)

# Add alias for namespaced usage in this build
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Add to list of targets to install
list(APPEND AUTOMATION_LUA_TARGETS ${PROJECT_NAME})

#------------------------------------------------------------------------------
# Installation
#------------------------------------------------------------------------------
include(GNUInstallDirs)

# Install header files with the correct directory structure
foreach(HEADER ${AUTOMATION_LUA_HEADERS})
    get_filename_component(HEADER_PATH ${HEADER} DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}/src" "" RELATIVE_PATH ${HEADER_PATH})
    install(FILES ${HEADER}
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}${RELATIVE_PATH}
    )
endforeach()

# Make headers available to other targets in the build tree
# Export the source directory as a public include path so that emulator/lua_emulator.h can be found
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:include>
)
# Install targets (without export for now)
install(TARGETS ${AUTOMATION_LUA_TARGETS}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Add core include directories to the target
target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${ROOT_DIR}/core/src>
    $<INSTALL_INTERFACE:include>
)

# Note: core library is linked by the parent project (unreal-qt)
# Static libraries should only use headers, not link dependencies

#------------------------------------------------------------------------------
# Export targets (disabled for now due to core dependency issues)
#------------------------------------------------------------------------------
# NOTE: Export functionality is disabled for now to allow building with unrealng:core dependency
# Uncomment this section when the export issues are resolved

# # Export targets to make the package discoverable with find_package
# install(EXPORT automation_lua-targets
#     FILE automation_lua-targets.cmake
#     NAMESPACE automation_lua::
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/automation_lua
# )
# 
# # Create and install config file
# include(CMakePackageConfigHelpers)
# configure_package_config_file(
#     ${CMAKE_CURRENT_SOURCE_DIR}/automation_lua-config.cmake.in
#     ${CMAKE_CURRENT_BINARY_DIR}/automation_lua-config.cmake
#     INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/automation_lua
# )
# 
# # Create and install version file
# write_basic_package_version_file(
#     ${CMAKE_CURRENT_BINARY_DIR}/automation_lua-config-version.cmake
#     VERSION ${PROJECT_VERSION}
#     COMPATIBILITY SameMajorVersion
# )
# 
# # Install config files
# install(FILES
#     ${CMAKE_CURRENT_BINARY_DIR}/automation_lua-config.cmake
#     ${CMAKE_CURRENT_BINARY_DIR}/automation_lua-config-version.cmake
#     DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/automation_lua
# )
# 
# # Export build tree
# export(EXPORT automation_lua-targets
#     FILE ${CMAKE_CURRENT_BINARY_DIR}/automation_lua-targets.cmake
#     NAMESPACE automation_lua::
# )

#------------------------------------------------------------------------------
# Usage Examples (for upper level CMake files)
#------------------------------------------------------------------------------
# This CMake file exports the following targets:
#
# - automation_lua::static - The static library target (when AUTOMATION_LUA_BUILD_STATIC=ON)
# - automation_lua::shared - The shared library target (when AUTOMATION_LUA_BUILD_SHARED=ON)
# - automation_lua::automation_lua - Alias to either static or shared (for convenience)
#
# Dependencies:
# - unrealng::core - Core library dependency (explicitly included from project root)
#
# Example 1: Basic usage with find_package
# -----------------------------------------
# find_package(automation_lua REQUIRED)
# target_link_libraries(your_target PRIVATE automation_lua::automation_lua)
#
# Example 2: Explicitly choosing static or shared library
# ------------------------------------------------------
# find_package(automation_lua REQUIRED)
# # Use static library
# target_link_libraries(your_target PRIVATE automation_lua::static)
# # Or use shared library
# target_link_libraries(your_target PRIVATE automation_lua::shared)
#
# Example 3: Direct inclusion with add_subdirectory
# ------------------------------------------------
# add_subdirectory(path/to/automation_lua)
# target_link_libraries(your_target PRIVATE automation_lua)
#
# Example 4: Controlling build options from parent CMake
# -----------------------------------------------------
# # Build only static library
# set(AUTOMATION_LUA_BUILD_STATIC ON CACHE BOOL "" FORCE)
# set(AUTOMATION_LUA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
#
# # Build only shared library
# set(AUTOMATION_LUA_BUILD_STATIC OFF CACHE BOOL "" FORCE)
# set(AUTOMATION_LUA_BUILD_SHARED ON CACHE BOOL "" FORCE)
#
# # Build both static and shared libraries
# set(AUTOMATION_LUA_BUILD_STATIC ON CACHE BOOL "" FORCE)
# set(AUTOMATION_LUA_BUILD_SHARED ON CACHE BOOL "" FORCE)
#
# # Control Lua library build type
# set(LUA_BUILD_STATIC ON CACHE BOOL "" FORCE)
# set(LUA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
#
# # Then include the directory
# add_subdirectory(path/to/automation_lua)
