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

if (BUILD_ZLIB)
    message(STATUS "[automation_webapi] Building bundled zlib")
    
    # Set zlib build options
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_MINIZIP OFF CACHE BOOL "" FORCE)
    set(ZLIB_INSTALL OFF CACHE BOOL "" FORCE)
    set(ZLIB_PREFIX "" CACHE STRING "" FORCE)
    
    # Set the source directory
    set(ZLIB_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/zlib")
    
    # Add zlib as a subproject
    add_subdirectory(
        ${ZLIB_SOURCE_DIR}
        ${CMAKE_CURRENT_BINARY_DIR}/zlib
    )
    
    # Set the actual library target name
    set(ZLIB_TARGET_NAME zlibstatic)
    
    # Set variables for Drogon's FindZLIB.cmake
    set(ZLIB_FOUND TRUE CACHE BOOL "ZLIB found (bundled)" FORCE)
    # Use the actual library path instead of the target name to avoid linking issues
    set(ZLIB_LIBRARY_PATH "${CMAKE_CURRENT_BINARY_DIR}/zlib/libz.a" CACHE INTERNAL "Path to zlib library" FORCE)
    set(ZLIB_LIBRARIES ${ZLIB_LIBRARY_PATH} CACHE INTERNAL "ZLIB libraries (bundled)" FORCE)
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
    )

    # Make sure dependencies are built first
    add_dependencies(${PROJECT_NAME}
        jsoncpp_static
    )
    
    # Link with zlib if it's being built
    if (BUILD_ZLIB)
        message(STATUS "[${PROJECT_NAME}] Linking with zlib: ${ZLIB_TARGET_NAME}")
        
        # Add dependency on zlib
        add_dependencies(${PROJECT_NAME} ${ZLIB_TARGET_NAME})
        
        # Link with zlib
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ZLIB_TARGET_NAME})
        
        # Add zlib include directories
        target_include_directories(${PROJECT_NAME} PRIVATE ${ZLIB_INCLUDE_DIRS})
    endif()



else () # Called from upper level CMake => build static library
    # Create the webapi library
    add_library(${PROJECT_NAME} STATIC
        ${COMMON_SOURCES}
    )

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

    if (BUILD_ZLIB)
        message(STATUS "[${PROJECT_NAME}] Linking with zlib: ${ZLIB_TARGET_NAME}")
        add_dependencies(${PROJECT_NAME}
            ${ZLIB_TARGET_NAME}
        )

        target_link_libraries(${PROJECT_NAME} PRIVATE
            ${ZLIB_TARGET_NAME}
        )

        target_include_directories(${PROJECT_NAME} PRIVATE
            ${ZLIB_INCLUDE_DIRS}
        )
    endif ()

    # Create alias for the library
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
endif ()



