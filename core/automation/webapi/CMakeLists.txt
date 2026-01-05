cmake_minimum_required (VERSION 3.10)

project(automation_webapi
    VERSION 1.0.0
    LANGUAGES CXX
)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

    # Add zlib as a subproject
    add_subdirectory(
        ${zlib_SOURCE_DIR}
        ${zlib_BINARY_DIR}
        EXCLUDE_FROM_ALL
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
    set(ZLIB_FOUND TRUE CACHE BOOL "ZLIB found (bundled)" FORCE)
    set(ZLIB_INCLUDE_DIRS ${zlib_BINARY_DIR} ${ZLIB_SOURCE_DIR} CACHE PATH "ZLIB include directories" FORCE)
    set(ZLIB_LIBRARIES ${ZLIB_TARGET_NAME} CACHE STRING "ZLIB libraries" FORCE)
    set(ZLIB_LIBRARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/zlib/libz.a" CACHE INTERNAL "Path to zlib library" FORCE)
    set(ZLIB_VERSION_STRING "1.3.1" CACHE STRING "ZLIB version" FORCE)

    # Make sure the library is built before anything that depends on it
    add_dependencies(${ZLIB_TARGET_NAME} ${ZLIB_TARGET_NAME})
    set(ZLIB_INCLUDE_DIRS 
        ${ZLIB_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/zlib
        CACHE INTERNAL "ZLIB include directories (bundled)" FORCE
    )
    set(ZLIB_INCLUDE_DIR 
        ${ZLIB_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/zlib
        CACHE INTERNAL "ZLIB include directory (bundled)" FORCE
    )
    set(ZLIB_LIBRARY ${ZLIB_LIBRARY_PATH} CACHE INTERNAL "ZLIB library (bundled)" FORCE)

    # Set output name to just 'z' for compatibility
    set_target_properties(${ZLIB_TARGET_NAME} PROPERTIES
        OUTPUT_NAME z
        POSITION_INDEPENDENT_CODE ON
    )
    
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

# Set include directories for jsoncpp
target_include_directories(jsoncpp_static INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include>
    $<INSTALL_INTERFACE:include>
)

# Create an alias for easier linking
add_library(Jsoncpp::Jsoncpp ALIAS jsoncpp_static)

# Export jsoncpp_static target
install(TARGETS jsoncpp_static
    EXPORT DrogonTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Also export to JsoncppTargets for backward compatibility
install(TARGETS jsoncpp_static
    EXPORT JsoncppTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Create a simple interface library for Drogon to find
add_library(Jsoncpp_lib INTERFACE)
target_link_libraries(Jsoncpp_lib INTERFACE jsoncpp_static)
target_include_directories(Jsoncpp_lib INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include>
    $<INSTALL_INTERFACE:include>
)

# Export Jsoncpp_lib target for Drogon to find
# Using DrogonTargets to match Drogon's export set
install(TARGETS Jsoncpp_lib
    EXPORT DrogonTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Also export to JsoncppTargets for backward compatibility
install(TARGETS Jsoncpp_lib
    EXPORT JsoncppTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include
)

# Set up JSONCPP package configuration
set(JSONCPP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include CACHE PATH "JSONCPP include directory" FORCE)
set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIR} CACHE PATH "JSONCPP include directories" FORCE)
set(JSONCPP_LIBRARY jsoncpp_static CACHE STRING "JSONCPP library" FORCE)
set(JSONCPP_LIBRARIES jsoncpp_static CACHE STRING "JSONCPP libraries" FORCE)
set(JSONCPP_FOUND TRUE CACHE BOOL "JSONCPP found" FORCE)

# Export variables to parent scope
set(JSONCPP_INCLUDE_DIR ${JSONCPP_INCLUDE_DIR} PARENT_SCOPE)
set(JSONCPP_INCLUDE_DIRS ${JSONCPP_INCLUDE_DIRS} PARENT_SCOPE)
set(JSONCPP_LIBRARY ${JSONCPP_LIBRARY} PARENT_SCOPE)
set(JSONCPP_LIBRARIES ${JSONCPP_LIBRARIES} PARENT_SCOPE)
set(JSONCPP_FOUND ${JSONCPP_FOUND} PARENT_SCOPE)

# Create a simple config file for the build tree
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp)

# Create a simple JsoncppConfig.cmake file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppConfig.cmake"
"# JSONCPP CMake configuration file
include(CMakeFindDependencyMacro)

# Get the current directory
get_filename_component(JSONCPP_CMAKE_DIR \"\${CMAKE_CURRENT_LIST_DIR}\" ABSOLUTE)
get_filename_component(JSONCPP_INSTALL_PREFIX \"\${JSONCPP_CMAKE_DIR}/../../..\" ABSOLUTE)

# Import the jsoncpp_static target
if(NOT TARGET jsoncpp_static)
    add_library(jsoncpp_static STATIC IMPORTED)
    
    # Set the import location
    if(WIN32)
        set(JSONCPP_LIBRARY_PATH \"\${JSONCPP_INSTALL_PREFIX}/lib/jsoncpp_static.lib\")
    else()
        set(JSONCPP_LIBRARY_PATH \"\${JSONCPP_INSTALL_PREFIX}/lib/libjsoncpp_static.a\")
    endif()
    
    set_target_properties(jsoncpp_static PROPERTIES
        IMPORTED_LOCATION \"\${JSONCPP_LIBRARY_PATH}\"
        INTERFACE_INCLUDE_DIRECTORIES \"\${JSONCPP_INSTALL_PREFIX}/include\"
    )
    add_library(Jsoncpp::Jsoncpp ALIAS jsoncpp_static)
endif()

set(JSONCPP_FOUND TRUE)
set(JSONCPP_INCLUDE_DIRS \"\${JSONCPP_INSTALL_PREFIX}/include\")
set(JSONCPP_LIBRARIES jsoncpp_static)
")

# Create a simple version file
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppConfigVersion.cmake"
"set(PACKAGE_VERSION \"1.9.5\")

# Check whether the requested version is compatible
if(\"\${PACKAGE_FIND_VERSION}\" VERSION_GREATER \"\${PACKAGE_VERSION}\")
    set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
    set(PACKAGE_VERSION_COMPATIBLE TRUE)
    if(\"\${PACKAGE_FIND_VERSION}\" VERSION_EQUAL \"\${PACKAGE_VERSION}\")
        set(PACKAGE_VERSION_EXACT TRUE)
    endif()
endif()
")

# Create a simple JsoncppTargets.cmake
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppTargets.cmake"
"# JSONCPP CMake targets file

# Create imported target jsoncpp_static
add_library(jsoncpp_static STATIC IMPORTED)

set_target_properties(jsoncpp_static PROPERTIES
  INTERFACE_INCLUDE_DIRECTORIES \"${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include\"
)

# Import target \"jsoncpp_static\" for configuration \"\"
set_property(TARGET jsoncpp_static APPEND PROPERTY IMPORTED_CONFIGURATIONS NOCONFIG)
set_target_properties(jsoncpp_static PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_NOCONFIG \"CXX\"
  IMPORTED_LOCATION_NOCONFIG \"${CMAKE_CURRENT_BINARY_DIR}/libjsoncpp_static.a\"
  )

# Create alias target Jsoncpp::Jsoncpp
add_library(Jsoncpp::Jsoncpp ALIAS jsoncpp_static)
")

# Help Drogon find jsoncpp
set(Jsoncpp_DIR ${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp CACHE PATH "Path to JsoncppConfig.cmake" FORCE)

# Install the config files
install(
    FILES
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppConfigVersion.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/cmake/Jsoncpp/JsoncppTargets.cmake"
    DESTINATION
        lib/cmake/Jsoncpp
)

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



else () # Called from upper level CMake => build static library
    # Create the webapi library
    add_library(${PROJECT_NAME} STATIC
        ${COMMON_SOURCES}
    )

    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${JSONCPP_INCLUDE_DIRS}
        ${ROOT_DIR}/core/src
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
        drogon
        jsoncpp_static
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
endif ()



