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

# Find or build zlib for Drogon
option(BUILD_ZLIB "Build zlib from source" ON)

# ZLIB_TARGET_NAME will hold the actual CMake target for zlib
set(ZLIB_TARGET_NAME "")

if(BUILD_ZLIB)
    message(STATUS "[automation_webapi] Building bundled zlib")
    # Assuming zlib's CMakeLists.txt creates a target named 'zlibstatic'
    # Check zlib's CMakeLists.txt for the actual target name created by add_library()
    # Common names: zlibstatic, zlib_static, z
    set(ZLIB_ACTUAL_TARGET_FROM_SUBDIR zlibstatic) # <--- VERIFY THIS in lib/zlib/CMakeLists.txt

    # Options for zlib's build
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE) # These might be zlib specific options
    # The following are more standard CMake options for zlib if it uses them
    option(ZLIB_BUILD_EXAMPLES "Build zlib examples" OFF) # Example, check zlib's CMake
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE) # Often useful for subprojects
    # Force static build if zlib respects BUILD_SHARED_LIBS
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libs for subprojects" FORCE) # This can affect zlib

    add_subdirectory("lib/zlib" zlib_build_dir) # Use a distinct binary dir

    if(NOT TARGET ${ZLIB_ACTUAL_TARGET_FROM_SUBDIR})
        message(FATAL_ERROR "Zlib target '${ZLIB_ACTUAL_TARGET_FROM_SUBDIR}' not found after add_subdirectory. Check lib/zlib/CMakeLists.txt.")
    endif()
    set(ZLIB_TARGET_NAME ${ZLIB_ACTUAL_TARGET_FROM_SUBDIR})

    # Set variables for Drogon's FindZLIB.cmake to use the CMake TARGET
    # This is the most robust way if FindZLIB.cmake is modern.
    set(ZLIB_FOUND TRUE CACHE BOOL "ZLIB found (bundled)" FORCE)
    set(ZLIB_LIBRARIES ${ZLIB_TARGET_NAME} CACHE INTERNAL "ZLIB library target (bundled)" FORCE) # Use INTERNAL
    set(ZLIB_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/lib/zlib" CACHE INTERNAL "ZLIB include directory (bundled)" FORCE)
    # Some FindZLIB might use ZLIB_LIBRARY (singular) for the target or file
    set(ZLIB_LIBRARY ${ZLIB_TARGET_NAME} CACHE INTERNAL "ZLIB library target (bundled)" FORCE)

    # No need for: HAS_ZLIB_H, HAVE_ZLIB_H - FindZLIB.cmake handles these

    # No need to manually set target_include_directories on zlibstatic here if its own CMakeLists.txt is correct.

    # The alias 'zlib' is good for your own project's direct linking if you prefer it.
    add_library(zlib ALIAS ${ZLIB_TARGET_NAME})

    # The install rules for zlib here are fine if you intend to install this webapi project
    # and want zlib to be installed alongside it. If webapi is just an internal static lib,
    # you might not need to install zlib separately from here.
else()
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
endif()

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
add_library(jsoncpp ALIAS jsoncpp_static)

# Export jsoncpp_static target
install(TARGETS jsoncpp_static
    EXPORT JsoncppTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Export jsoncpp variables for parent scope
set(JSONCPP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include PARENT_SCOPE)
set(JSONCPP_LIBRARY jsoncpp_static PARENT_SCOPE)
set(JSONCPP_LIBRARIES jsoncpp_static PARENT_SCOPE)
set(JSONCPP_FOUND TRUE PARENT_SCOPE)

# Create and export a target for Drogon to find
add_library(Jsoncpp_lib INTERFACE)
target_include_directories(Jsoncpp_lib INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include
)
target_link_libraries(Jsoncpp_lib INTERFACE jsoncpp_static)

# Export Jsoncpp_lib target for Drogon
install(TARGETS Jsoncpp_lib
    EXPORT JsoncppTargets
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

install(EXPORT JsoncppTargets
    FILE JsoncppConfig.cmake
    NAMESPACE Jsoncpp::
    DESTINATION lib/cmake/Jsoncpp
)

# Help Drogon find jsoncpp
set(JSONCPP_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/jsoncpp/include CACHE PATH "" FORCE)
set(JSONCPP_LIBRARY jsoncpp_static CACHE STRING "" FORCE)
set(JSONCPP_LIBRARIES jsoncpp_static CACHE STRING "" FORCE)
set(JSONCPP_FOUND TRUE CACHE BOOL "" FORCE)

# Set specific build options for Drogon framework (located in lib/drogon)
option(BUILD_CTL "Build drogon_ctl" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_ORM "Build orm" OFF)
option(COZ_PROFILING "Use coz for profiling" OFF)
option(BUILD_SHARED_LIBS "Build drogon as a shared lib" OFF)
option(BUILD_DOC "Build Doxygen documentation" OFF)
option(BUILD_BROTLI "Build Brotli" OFF)

# Tell Drogon to use our zlib
set(ENABLE_BROTLI OFF CACHE BOOL "" FORCE)
set(ENABLE_ZLIB ON CACHE BOOL "" FORCE)
set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES} CACHE STRING "" FORCE)
set(ZLIB_INCLUDE_DIRS ${ZLIB_INCLUDE_DIR} CACHE PATH "" FORCE)

# Add Drogon with modified configuration
option(BUILD_DROGON_SHARED "Build Drogon as shared library" OFF)
option(ENABLE_REDIS "Build Redis support" OFF)
option(ENABLE_SQLITE "Build SQLite support" OFF)
option(ENABLE_MYSQL "Build MySQL support" OFF)
option(ENABLE_POSTGRESQL "Build PostgreSQL support" OFF)
# Drogon/trentor setting
option(BUILD_TESTING OFF FORCE)

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

    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    # For standalone build
    target_include_directories(${PROJECT_NAME} PRIVATE
            ${JSONCPP_INCLUDE_DIRS}
            ${ZLIB_INCLUDE_DIR}
            ${ROOT_DIR}/core/src
    )

    target_link_libraries(${PROJECT_NAME} PRIVATE
            drogon
            jsoncpp_static
            zlibstatic
    )

    # Make sure dependencies are built first
    add_dependencies(${PROJECT_NAME}
            zlibstatic
            jsoncpp_static
    )
else () # Called from upper level CMake => build static library
    # Create the webapi library
    add_library(automation_webapi STATIC
        ${COMMON_SOURCES}
    )

    target_include_directories(automation_webapi PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${JSONCPP_INCLUDE_DIRS}
        ${ZLIB_INCLUDE_DIR}
        ${ROOT_DIR}/core/src
    )

    target_link_libraries(automation_webapi PRIVATE
        drogon
        jsoncpp_static
        zlibstatic
    )

    # Ensure dependencies are built first
    add_dependencies(automation_webapi
        zlibstatic
        jsoncpp_static
    )

    # Create alias for the library
    add_library(unrealng::automation_webapi ALIAS automation_webapi)
endif ()



