cmake_minimum_required(VERSION 3.14)

# Configurable Lua version
set(LUA_VERSION "5.4.7" CACHE STRING "Lua version to build")
set(LUA_SOURCE_DIR "lua-${LUA_VERSION}" CACHE PATH "Path to Lua source directory")

project(lua VERSION ${LUA_VERSION} LANGUAGES C CXX)

# Options to build static and shared libraries
option(LUA_BUILD_STATIC "Build static library" ON)
option(LUA_BUILD_SHARED "Build shared library" OFF)

# Set C++17 standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define source files
set(LUA_SOURCES
    ${LUA_SOURCE_DIR}/src/lapi.c
    ${LUA_SOURCE_DIR}/src/lauxlib.c
    ${LUA_SOURCE_DIR}/src/lbaselib.c
    ${LUA_SOURCE_DIR}/src/lcode.c
    ${LUA_SOURCE_DIR}/src/lcorolib.c
    ${LUA_SOURCE_DIR}/src/lctype.c
    ${LUA_SOURCE_DIR}/src/ldblib.c
    ${LUA_SOURCE_DIR}/src/ldebug.c
    ${LUA_SOURCE_DIR}/src/ldo.c
    ${LUA_SOURCE_DIR}/src/ldump.c
    ${LUA_SOURCE_DIR}/src/lfunc.c
    ${LUA_SOURCE_DIR}/src/lgc.c
    ${LUA_SOURCE_DIR}/src/linit.c
    ${LUA_SOURCE_DIR}/src/liolib.c
    ${LUA_SOURCE_DIR}/src/llex.c
    ${LUA_SOURCE_DIR}/src/lmathlib.c
    ${LUA_SOURCE_DIR}/src/lmem.c
    ${LUA_SOURCE_DIR}/src/loadlib.c
    ${LUA_SOURCE_DIR}/src/lobject.c
    ${LUA_SOURCE_DIR}/src/lopcodes.c
    ${LUA_SOURCE_DIR}/src/loslib.c
    ${LUA_SOURCE_DIR}/src/lparser.c
    ${LUA_SOURCE_DIR}/src/lstate.c
    ${LUA_SOURCE_DIR}/src/lstring.c
    ${LUA_SOURCE_DIR}/src/lstrlib.c
    ${LUA_SOURCE_DIR}/src/ltable.c
    ${LUA_SOURCE_DIR}/src/ltablib.c
    ${LUA_SOURCE_DIR}/src/ltm.c
    ${LUA_SOURCE_DIR}/src/lundump.c
    ${LUA_SOURCE_DIR}/src/lutf8lib.c
    ${LUA_SOURCE_DIR}/src/lvm.c
    ${LUA_SOURCE_DIR}/src/lzio.c
)

# Define header files
set(LUA_HEADERS
    ${LUA_SOURCE_DIR}/src/lapi.h
    ${LUA_SOURCE_DIR}/src/lauxlib.h
    ${LUA_SOURCE_DIR}/src/lcode.h
    ${LUA_SOURCE_DIR}/src/lctype.h
    ${LUA_SOURCE_DIR}/src/ldebug.h
    ${LUA_SOURCE_DIR}/src/ldo.h
    ${LUA_SOURCE_DIR}/src/lfunc.h
    ${LUA_SOURCE_DIR}/src/lgc.h
    ${LUA_SOURCE_DIR}/src/ljumptab.h
    ${LUA_SOURCE_DIR}/src/llex.h
    ${LUA_SOURCE_DIR}/src/llimits.h
    ${LUA_SOURCE_DIR}/src/lmem.h
    ${LUA_SOURCE_DIR}/src/lobject.h
    ${LUA_SOURCE_DIR}/src/lopcodes.h
    ${LUA_SOURCE_DIR}/src/lopnames.h
    ${LUA_SOURCE_DIR}/src/lparser.h
    ${LUA_SOURCE_DIR}/src/lprefix.h
    ${LUA_SOURCE_DIR}/src/lstate.h
    ${LUA_SOURCE_DIR}/src/lstring.h
    ${LUA_SOURCE_DIR}/src/ltable.h
    ${LUA_SOURCE_DIR}/src/ltm.h
    ${LUA_SOURCE_DIR}/src/lua.h
    ${LUA_SOURCE_DIR}/src/lua.hpp
    ${LUA_SOURCE_DIR}/src/luaconf.h
    ${LUA_SOURCE_DIR}/src/lualib.h
    ${LUA_SOURCE_DIR}/src/lundump.h
    ${LUA_SOURCE_DIR}/src/lvm.h
    ${LUA_SOURCE_DIR}/src/lzio.h
)

# Define public headers that will be installed
set(LUA_PUBLIC_HEADERS
    ${LUA_SOURCE_DIR}/src/lua.h
    ${LUA_SOURCE_DIR}/src/luaconf.h
    ${LUA_SOURCE_DIR}/src/lualib.h
    ${LUA_SOURCE_DIR}/src/lauxlib.h
    ${LUA_SOURCE_DIR}/src/lua.hpp
)

# Create library targets based on options
if(LUA_BUILD_STATIC)
    add_library(lua_static STATIC ${LUA_SOURCES} ${LUA_HEADERS})
    set_target_properties(lua_static PROPERTIES 
        OUTPUT_NAME lua
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Set include directories for static library
    target_include_directories(lua_static
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${LUA_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
    )
    
    # Add compile definitions for different platforms
    if(WIN32)
        if(MSVC)
            target_compile_definitions(lua_static PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    elseif(UNIX)
        target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
        if(APPLE)
            target_compile_definitions(lua_static PRIVATE LUA_USE_DLOPEN)
        else()
            target_compile_definitions(lua_static PRIVATE LUA_USE_DLOPEN LUA_USE_READLINE)
        endif()
    endif()
    
    # Add alias for namespaced usage in this build
    add_library(lua::static ALIAS lua_static)
endif()

if(LUA_BUILD_SHARED)
    add_library(lua_shared SHARED ${LUA_SOURCES} ${LUA_HEADERS})
    set_target_properties(lua_shared PROPERTIES 
        OUTPUT_NAME lua
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        PUBLIC_HEADER "${LUA_PUBLIC_HEADERS}"
        POSITION_INDEPENDENT_CODE ON
    )
    
    # Set include directories for shared library
    target_include_directories(lua_shared
        PUBLIC
            $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${LUA_SOURCE_DIR}/src>
            $<INSTALL_INTERFACE:include>
    )
    
    # Add compile definitions for different platforms
    if(WIN32)
        target_compile_definitions(lua_shared PRIVATE LUA_BUILD_AS_DLL)
        if(MSVC)
            target_compile_definitions(lua_shared PRIVATE _CRT_SECURE_NO_WARNINGS)
        endif()
    elseif(UNIX)
        target_compile_definitions(lua_shared PRIVATE LUA_USE_POSIX)
        if(APPLE)
            target_compile_definitions(lua_shared PRIVATE LUA_USE_DLOPEN)
        else()
            target_compile_definitions(lua_shared PRIVATE LUA_USE_DLOPEN LUA_USE_READLINE)
        endif()
    endif()
    
    # Add alias for namespaced usage in this build
    add_library(lua::shared ALIAS lua_shared)
endif()

# Error if no library type is selected
if(NOT LUA_BUILD_STATIC AND NOT LUA_BUILD_SHARED)
    message(FATAL_ERROR "At least one of LUA_BUILD_STATIC or LUA_BUILD_SHARED must be enabled")
endif()

# For compatibility, create a default target that points to either static or shared
if(LUA_BUILD_STATIC)
    add_library(lua ALIAS lua_static)
elseif(LUA_BUILD_SHARED)
    add_library(lua ALIAS lua_shared)
endif()

# Install targets
include(GNUInstallDirs)

# Create the list of targets to install
set(LUA_INSTALL_TARGETS)
if(LUA_BUILD_STATIC)
    list(APPEND LUA_INSTALL_TARGETS lua_static)
endif()
if(LUA_BUILD_SHARED)
    list(APPEND LUA_INSTALL_TARGETS lua_shared)
endif()

# Install all enabled targets
install(TARGETS ${LUA_INSTALL_TARGETS}
    EXPORT lua-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Export targets
install(EXPORT lua-targets
    FILE lua-targets.cmake
    NAMESPACE lua::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lua
)

#------------------------------------------------------------------------------
# CMake Package Configuration
#------------------------------------------------------------------------------
# This section creates the necessary files to make this library usable by other
# CMake projects via find_package(lua). There are three key components:
#
# 1. lua-config.cmake - Main package configuration file that defines the targets
# 2. lua-config-version.cmake - Version information for compatibility checking
# 3. lua-targets.cmake - Exported targets with their properties and dependencies
#
# When a user calls find_package(lua) in their project, CMake searches for these
# files in standard locations to load the package.
#------------------------------------------------------------------------------

# Create and install config file from template
include(CMakePackageConfigHelpers)
configure_package_config_file(
    # Source template file that contains @PACKAGE_INIT@ and other includes
    ${CMAKE_CURRENT_SOURCE_DIR}/lua-config.cmake.in
    # Destination file generated during build
    ${CMAKE_CURRENT_BINARY_DIR}/lua-config.cmake
    # Where the config file will be installed relative to CMAKE_INSTALL_PREFIX
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lua
)

# Create and install version file for compatibility checking
# This allows find_package(lua 5.4 REQUIRED) to check version compatibility
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/lua-config-version.cmake
    VERSION ${PROJECT_VERSION}
    # SameMajorVersion means 5.4 is compatible with 5.4.x but not with 5.5.y
    COMPATIBILITY SameMajorVersion
)

# Install config files
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/lua-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/lua-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/lua
)

# Export build tree
export(EXPORT lua-targets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/lua-targets.cmake
    NAMESPACE lua::
)

#------------------------------------------------------------------------------
# Usage Information for Upper Level CMake Files
#------------------------------------------------------------------------------
# This CMake file exports the following variables and targets:
#
# Targets:
#   - lua::static - The static Lua library target (when LUA_BUILD_STATIC=ON)
#   - lua::shared - The shared Lua library target (when LUA_BUILD_SHARED=ON)
#   - lua - Alias to either lua_static or lua_shared (for backward compatibility)
#
# Variables:
#   - LUA_VERSION - The version of Lua being built (default: 5.4.7)
#   - LUA_SOURCE_DIR - Path to the Lua source directory (default: lua-${LUA_VERSION})
#   - LUA_BUILD_STATIC - Whether to build static library (default: ON)
#   - LUA_BUILD_SHARED - Whether to build shared library (default: OFF)
#
# Usage in parent CMake projects:
#
# Option 1: Using find_package (recommended for external projects):
#   find_package(lua 5.4 REQUIRED)
#   
#   # Link with static library (preferred if available)
#   if(TARGET lua::static)
#     target_link_libraries(your_target PRIVATE lua::static)
#   else()
#     target_link_libraries(your_target PRIVATE lua::shared)
#   endif()
#   
#   # Or explicitly choose the library type
#   target_link_libraries(your_target PRIVATE lua::static)  # Static library
#   target_link_libraries(your_target PRIVATE lua::shared)  # Shared library
#
# Option 2: Including this directory directly (for in-tree builds):
#   add_subdirectory(path/to/lua)
#   
#   # Link with default library (static if available, otherwise shared)
#   target_link_libraries(your_target PRIVATE lua)
#   
#   # Or explicitly choose the library type
#   target_link_libraries(your_target PRIVATE lua_static)  # Static library
#   target_link_libraries(your_target PRIVATE lua_shared)  # Shared library
#
# Customizing the build:
#   # To build a specific Lua version:
#   set(LUA_VERSION "5.4.6" CACHE STRING "" FORCE)
#   
#   # To specify a custom source directory:
#   set(LUA_SOURCE_DIR "custom-lua-path" CACHE PATH "" FORCE)
#   
#   # To control which libraries to build:
#   set(LUA_BUILD_STATIC ON CACHE BOOL "" FORCE)   # Build static library
#   set(LUA_BUILD_SHARED ON CACHE BOOL "" FORCE)   # Build shared library
#   
#   # To build both static and shared libraries:
#   set(LUA_BUILD_STATIC ON CACHE BOOL "" FORCE)
#   set(LUA_BUILD_SHARED ON CACHE BOOL "" FORCE)
#   
#   # To build only shared library:
#   set(LUA_BUILD_STATIC OFF CACHE BOOL "" FORCE)
#   set(LUA_BUILD_SHARED ON CACHE BOOL "" FORCE)
#
# Note: Always set these variables before including this CMake file.
#------------------------------------------------------------------------------
