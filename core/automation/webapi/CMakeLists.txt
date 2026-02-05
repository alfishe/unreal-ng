cmake_minimum_required (VERSION 3.10)

project(automation_webapi
    VERSION 1.0.0
    LANGUAGES CXX
)

# C++ standard is inherited from root CMakeLists.txt (C++20)

# Set paths
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Set CMake policy to allow linking to targets in other directories
cmake_policy(SET CMP0079 NEW)

# Find OpenSSL (required by Drogon/Trantor)
# Try to find OpenSSL, and if it fails, set the root directory explicitly
find_package(OpenSSL QUIET)
if (NOT OpenSSL_FOUND)
    # Fallback: set OpenSSL root directory for Homebrew on ARM64 macOS
    if (APPLE AND EXISTS "/opt/homebrew/opt/openssl")
        set(OPENSSL_ROOT_DIR "/opt/homebrew/opt/openssl")
        set(OPENSSL_LIBRARIES "/opt/homebrew/opt/openssl/lib/libssl.dylib;/opt/homebrew/opt/openssl/lib/libcrypto.dylib")
        set(OPENSSL_INCLUDE_DIR "/opt/homebrew/opt/openssl/include")
        message(STATUS "[automation_webapi] Using OpenSSL from Homebrew: ${OPENSSL_ROOT_DIR}")
    # Fallback: Windows OpenSSL installations
    elseif (WIN32)
        # Check common Windows OpenSSL installation paths
        set(OPENSSL_SEARCH_PATHS
            "C:/Program Files/OpenSSL-Win64"
            "C:/OpenSSL-Win64"
            "$ENV{OPENSSL_ROOT_DIR}"
        )
        
        # For MSVC, also check vcpkg paths
        if(MSVC AND DEFINED ENV{VCPKG_ROOT})
            list(APPEND OPENSSL_SEARCH_PATHS "$ENV{VCPKG_ROOT}/installed/x64-windows")
        endif()
        
        foreach(OPENSSL_PATH ${OPENSSL_SEARCH_PATHS})
            if(EXISTS "${OPENSSL_PATH}/include/openssl/ssl.h")
                set(OPENSSL_ROOT_DIR "${OPENSSL_PATH}")
                message(STATUS "[automation_webapi] Using OpenSSL from: ${OPENSSL_ROOT_DIR}")
                break()
            endif()
        endforeach()
    endif ()
    find_package(OpenSSL REQUIRED)
endif ()

# Find or build zlib for Drogon
option(BUILD_ZLIB "Build zlib from source" ON)

# ZLIB_TARGET_NAME will hold the actual CMake target for zlib
set(ZLIB_TARGET_NAME "")

if (BUILD_ZLIB)
    message(STATUS "[automation_webapi] Building bundled zlib")
    
    # Set zlib build options
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(Z_PREFIX "" CACHE STRING "" FORCE)
    
    # Set source and binary directories
    set(zlib_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/zlib" CACHE PATH "zlib source directory" FORCE)
    set(zlib_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/zlib CACHE PATH "zlib binary directory" FORCE)

    # Add zlib as a subproject (must be included in build to generate libz.a)
    add_subdirectory(
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
    )

    # Set the actual library target name
    set(ZLIB_TARGET_NAME zlibstatic)
    
    # Add include directories
    target_include_directories(${ZLIB_TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${zlib_BINARY_DIR}>
        $<BUILD_INTERFACE:${zlib_SOURCE_DIR}>
        $<INSTALL_INTERFACE:include>
    )
    
    # Set variables for find_package compatibility
    # Use the target name for linking - CMake will resolve the actual library path
    set(ZLIB_FOUND TRUE CACHE BOOL "ZLIB found (bundled)" FORCE)
    set(ZLIB_INCLUDE_DIRS ${zlib_BINARY_DIR} ${zlib_SOURCE_DIR} CACHE PATH "ZLIB include directories" FORCE)
    set(ZLIB_LIBRARIES ${ZLIB_TARGET_NAME} CACHE STRING "ZLIB libraries" FORCE)
    set(ZLIB_VERSION_STRING "1.3.1" CACHE STRING "ZLIB version" FORCE)

    # Set include directories for find_package compatibility
    set(ZLIB_INCLUDE_DIRS 
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
        CACHE INTERNAL "ZLIB include directories (bundled)" FORCE
    )
    set(ZLIB_INCLUDE_DIR 
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
        CACHE INTERNAL "ZLIB include directory (bundled)" FORCE
    )
    # Use target name instead of file path - CMake will resolve the actual file at link time
    set(ZLIB_LIBRARY ${ZLIB_TARGET_NAME} CACHE INTERNAL "ZLIB library (bundled)" FORCE)

    # Set output directory and name for predictable library location
    set_target_properties(${ZLIB_TARGET_NAME} PROPERTIES
        OUTPUT_NAME z
        POSITION_INDEPENDENT_CODE ON
        ARCHIVE_OUTPUT_DIRECTORY "${zlib_BINARY_DIR}"
    )
    
    # Create ZLIB::ZLIB IMPORTED target that Drogon expects from find_package(ZLIB)
    # Using INTERFACE IMPORTED that links to zlibstatic creates proper build dependency
    # while also being compatible with Drogon's export (INTERFACE targets don't need to be exported)
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB INTERFACE IMPORTED GLOBAL)
        set_target_properties(ZLIB::ZLIB PROPERTIES
            INTERFACE_LINK_LIBRARIES "${ZLIB_TARGET_NAME}"
            INTERFACE_INCLUDE_DIRECTORIES "${zlib_SOURCE_DIR};${CMAKE_CURRENT_BINARY_DIR}/zlib"
        )
    endif()
    
    # Export the target for other subdirectories
    set(ZLIB_TARGET ${ZLIB_TARGET_NAME} CACHE INTERNAL "ZLIB target" FORCE)
else ()
    message(STATUS "[automation_webapi] Finding system zlib")
    find_package(ZLIB REQUIRED)
    if(NOT ZLIB_FOUND) # Should be redundant due to REQUIRED
        message(FATAL_ERROR "ZLIB not found and BUILD_ZLIB is OFF")
    endif()
    # find_package(ZLIB) sets ZLIB_LIBRARIES (usually to 'z' or full path)
    # and ZLIB_INCLUDE_DIRS.
    # We need to ensure ZLIB_TARGET_NAME is set if ZLIB::ZLIB target exists
    if(TARGET ZLIB::ZLIB)
        set(ZLIB_TARGET_NAME ZLIB::ZLIB)
    else()
        # If no import target, use the library variable. This might be just "z".
        set(ZLIB_TARGET_NAME ${ZLIB_LIBRARIES})
    endif()
    message(STATUS "[automation_webapi] Using system zlib. Target/Library: ${ZLIB_TARGET_NAME}, Includes: ${ZLIB_INCLUDE_DIRS}")
endif ()

# Build JsonCpp as a static library
set(JSONCPP_WITH_TESTS OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_WARNING_AS_ERROR OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "" FORCE)
set(JSONCPP_WITH_EXAMPLE OFF CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(BUILD_OBJECT_LIBS OFF CACHE BOOL "" FORCE)

# Add jsoncpp subdirectory
add_subdirectory("lib/jsoncpp")

install(TARGETS jsoncpp_static EXPORT DrogonTargets)



# Set specific build options for Drogon framework (located in lib/drogon)
set(BUILD_CTL OFF CACHE BOOL "Build drogon_ctl" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "Build examples" FORCE)
set(BUILD_ORM OFF CACHE BOOL "Build orm" FORCE)
set(COZ_PROFILING OFF CACHE BOOL "Use coz for profiling" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build drogon as a shared lib" FORCE)
set(BUILD_DOC OFF CACHE BOOL "Build Doxygen documentation" FORCE)
set(BUILD_BROTLI OFF CACHE BOOL "Build Brotli" FORCE)
set(BUILD_YAML_CONFIG OFF CACHE BOOL "Build yaml config" FORCE)
# Drogon/trentor setting
set(BUILD_TESTING OFF CACHE BOOL "Disable testing" FORCE)

# Tell Drogon to use our zlib
set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES} CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} CACHE PATH "" FORCE)

# Add Drogon
add_subdirectory(lib/drogon)


set(COMMON_SOURCES
    src/hello_world_api.cpp
    src/hello_world_api.h
    src/emulator_api.cpp
    src/emulator_api.h
    src/openapi_spec.cpp
    src/api/lifecycle_api.cpp
    src/api/settings_api.cpp
    src/api/features_api.cpp
    src/api/state_memory_api.cpp
    src/api/state_screen_api.cpp
    src/api/state_audio_api.cpp
    src/api/tape_disk_api.cpp
    src/api/snapshot_api.cpp
    src/api/interpreter_api.cpp
    src/api/interpreter_api.h
    src/api/basic_api.cpp
    src/api/analyzers_api.cpp
    src/api/debug_api.cpp
    src/api/capture_api.cpp
    src/api/profiler_api.cpp
    src/api/keyboard_api.cpp
    src/api/batch_api.cpp
    src/emulator_websocket.cpp
    src/emulator_websocket.h
    src/automation-webapi.cpp
    src/automation-webapi.h
)

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Standalone build
    add_executable(${PROJECT_NAME}
        src/main.cpp

        ${COMMON_SOURCES}
    )

    # For standalone build
    target_include_directories(${PROJECT_NAME} PRIVATE
        ${JSONCPP_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIR}
        ${ROOT_DIR}/core/src
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        drogon
        jsoncpp_static
    )

    # Make sure dependencies are built first
    add_dependencies(${PROJECT_NAME}
        jsoncpp_static
    )
    
    # Note: zlib is linked transitively through Drogon, so we don't need to link it explicitly here
    # This avoids duplicate library warnings. Drogon will use the zlib we configured via ZLIB_LIBRARIES
    if (BUILD_ZLIB)
        # Ensure zlib is built before automation_webapi (Drogon depends on it)
        add_dependencies(${PROJECT_NAME} ${ZLIB_TARGET_NAME})
        
        # Include directories are still needed for compilation
        target_include_directories(${PROJECT_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})
    endif()

    # Copy HTML resources to build directory (multiple locations for flexibility)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/html
        ${CMAKE_BINARY_DIR}/resources/html
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/html
        ${CMAKE_BINARY_DIR}/bin/resources/html
        COMMENT "Copying HTML resources to build directory"
    )

else () # Called from upper level CMake => build static library
    # Find Python3 for pybind11 headers
    if (ENABLE_PYTHON_AUTOMATION)
        find_package(Python3 COMPONENTS Development)
    endif()

    # Create the webapi library
    add_library(${PROJECT_NAME} STATIC
        ${COMMON_SOURCES}
    )

    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${JSONCPP_INCLUDE_DIRS}
        ${ROOT_DIR}/core/src
        ${ROOT_DIR}/core/automation  # For automation/automation.h
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${ROOT_DIR}/core/automation/lua/src>
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${ROOT_DIR}/core/automation/lua/lib/sol2/sol2-3.5.0/include>
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${ROOT_DIR}/core/automation/lua/lib/lua/lua-5.4.7/src>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:${ROOT_DIR}/core/automation/python/src>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:${ROOT_DIR}/core/automation/python/3rdparty/pybind11/include>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:${Python3_INCLUDE_DIRS}>
        $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:${ROOT_DIR}/core/automation/cli/include>
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        drogon
        jsoncpp_static
    )

    # Add conditional compiler definitions for automation modules
    target_compile_definitions(${PROJECT_NAME} PRIVATE
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:ENABLE_LUA_AUTOMATION>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:ENABLE_PYTHON_AUTOMATION>
        $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:ENABLE_WEBAPI_AUTOMATION>
        $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:ENABLE_CLI_AUTOMATION>
    )

    # Ensure dependencies are built first
    add_dependencies(${PROJECT_NAME}
        jsoncpp_static
    )

    # Note: zlib is linked transitively through Drogon, so we don't need to link it explicitly here
    # This avoids duplicate library warnings. Drogon will use the zlib we configured via ZLIB_LIBRARIES
    if (BUILD_ZLIB)
        # Ensure zlib is built before automation_webapi (Drogon depends on it)
        add_dependencies(${PROJECT_NAME}
            ${ZLIB_TARGET_NAME}
        )
        
        # Include directories are still needed for compilation
        target_include_directories(${PROJECT_NAME} PRIVATE
            ${ZLIB_INCLUDE_DIRS}
        )
    endif ()

    # Create alias for the library
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
    
    # Copy HTML resources to build directory (multiple locations for flexibility)
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/html
        ${CMAKE_BINARY_DIR}/resources/html
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/html
        ${CMAKE_BINARY_DIR}/bin/resources/html
        COMMENT "Copying HTML resources to build directory"
    )
    
    # Install HTML resources for deployment
    # For macOS .app bundles
    if(APPLE)
        install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources/html/
                DESTINATION "${CMAKE_INSTALL_PREFIX}/unreal-qt.app/Contents/Resources/html"
                OPTIONAL)
    endif()
    
    # For standard installations (Linux, Windows, macOS non-bundle)
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources/html/
            DESTINATION "share/unreal-speccy/resources/html"
            OPTIONAL)
    
    # Also install alongside executable for portable deployments
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/resources/html/
            DESTINATION "bin/resources/html"
            OPTIONAL)
endif ()



