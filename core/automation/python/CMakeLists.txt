cmake_minimum_required(VERSION 3.16)
project(automation_python LANGUAGES CXX C)

# C++ standard is inherited from root CMakeLists.txt (C++20)

message(">> <${PROJECT_NAME}>")

# Set paths
get_filename_component(ROOT_DIR ../../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR src)
set(DATA_PATH ${ROOT_DIR}/data)

# Include CMake helper functions file
include(${ROOT_DIR}/cmake/func.cmake)

# ========================================
# Static Python Configuration
# ========================================
# Build CPython from source as a static library to avoid version mismatch
# crashes between bundled and system Python installations.
#
# We use ExternalProject to clone CPython from git and build using autotools.
#
# Platform support:
# - macOS: Clang with autotools - static library
# - Linux: GCC with autotools - static library
# - Windows MSVC: Not yet supported (Python builds differently on Windows)
# - Windows MinGW: MSYS2's Python (dynamic linking required)

option(PYTHON_USE_STATIC "Build and link Python statically (not available on MinGW)" ON)

# MinGW cannot build CPython from source - use MSYS2's Python instead
if(WIN32 AND MINGW)
    message(STATUS "[automation_python] MinGW detected - using MSYS2 Python (dynamic linking)")
    set(PYTHON_USE_STATIC OFF)
endif()

# Windows MSVC static Python build is complex - fall back to system Python
if(WIN32 AND MSVC)
    message(STATUS "[automation_python] MSVC detected - using system Python (static not yet supported)")
    set(PYTHON_USE_STATIC OFF)
endif()

if(PYTHON_USE_STATIC)
    message(STATUS "[automation_python] Building static Python from source")
    
    include(ExternalProject)
    
    # Python version and paths
    set(PYTHON_VERSION "3.13.1")
    set(PYTHON_TAG "v${PYTHON_VERSION}")
    set(PYTHON_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/python_install")
    set(PYTHON_STATIC_INCLUDE_DIR "${PYTHON_INSTALL_PREFIX}/include/python3.13")
    set(PYTHON_STATIC_LIB_DIR "${PYTHON_INSTALL_PREFIX}/lib")
    set(PYTHON_STATIC_LIB "${PYTHON_STATIC_LIB_DIR}/libpython3.13.a")
    
    # Find OpenSSL for Python SSL module
    find_package(OpenSSL QUIET)
    if(OPENSSL_FOUND)
        get_filename_component(OPENSSL_ROOT ${OPENSSL_INCLUDE_DIR} DIRECTORY)
        set(PYTHON_SSL_ARG "--with-openssl=${OPENSSL_ROOT}")
    else()
        set(PYTHON_SSL_ARG "")
    endif()
    
    # Configure args for Python
    set(PYTHON_CONFIGURE_ARGS
        --prefix=${PYTHON_INSTALL_PREFIX}
        --disable-shared
        --enable-optimizations
        ${PYTHON_SSL_ARG}
    )
    
    # Platform-specific compiler and flags
    if(APPLE)
        set(PYTHON_CC "clang")
        # Match the project's deployment target to avoid linker warnings
        set(MACOS_DEPLOYMENT_TARGET "10.13")
        set(PYTHON_CFLAGS "-mmacosx-version-min=${MACOS_DEPLOYMENT_TARGET}")
        set(PYTHON_LDFLAGS "-mmacosx-version-min=${MACOS_DEPLOYMENT_TARGET}")
    else()
        set(PYTHON_CC "gcc")
        set(PYTHON_CFLAGS "")
        set(PYTHON_LDFLAGS "")
    endif()
    
    # Build Python from source using ExternalProject
    # UPDATE_DISCONNECTED prevents re-checking git on every build
    ExternalProject_Add(python_build_ep
        GIT_REPOSITORY https://github.com/python/cpython.git
        GIT_TAG ${PYTHON_TAG}
        GIT_SHALLOW TRUE
        PREFIX ${CMAKE_BINARY_DIR}/python_ep
        UPDATE_COMMAND ""
        UPDATE_DISCONNECTED TRUE
        CONFIGURE_COMMAND <SOURCE_DIR>/configure ${PYTHON_CONFIGURE_ARGS}
            CC=${PYTHON_CC}
            CFLAGS=${PYTHON_CFLAGS}
            LDFLAGS=${PYTHON_LDFLAGS}
        BUILD_COMMAND make -j
        INSTALL_COMMAND make install
        BUILD_BYPRODUCTS ${PYTHON_STATIC_LIB}
        LOG_DOWNLOAD ON
        LOG_CONFIGURE ON
        LOG_BUILD ON
        LOG_INSTALL ON
    )
    
    # Use PYBIND11_NOPYTHON mode since Python isn't built yet during configure
    set(PYBIND11_NOPYTHON ON CACHE BOOL "" FORCE)
    
    # Add pybind11 in NOPYTHON mode
    add_subdirectory(3rdparty/pybind11)
    
    # Create an imported target for the static Python library
    add_library(python_static STATIC IMPORTED GLOBAL)
    set_target_properties(python_static PROPERTIES
        IMPORTED_LOCATION ${PYTHON_STATIC_LIB}
    )
    add_dependencies(python_static python_build_ep)
    
else()
    # Use system Python (dynamic linking) - for MinGW, MSVC, or when static is disabled
    message(STATUS "[automation_python] Using system Python (dynamic linking)")
    
    set(PYBIND11_FINDPYTHON ON)
    add_subdirectory(3rdparty/pybind11)
    find_package(Python3 REQUIRED COMPONENTS Interpreter Development)
endif()

if(APPLE)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -mmacosx-version-min=10.13")
endif()

# All C++ sources in /src and sub-folders will be compiled
file(GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)
set(SOURCES ${CPP_FILES})
message("SOURCES:${SOURCES}")


if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(${ROOT_DIR}/core/src core)

    # Put binary and configuration files to /bin subfolder
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

    add_executable(${PROJECT_NAME} ${SOURCES})

    if(PYTHON_USE_STATIC)
        # Link against static Python built from source
        target_link_libraries(${PROJECT_NAME}
            pybind11::headers
            python_static
            unrealng::core
        )
        target_include_directories(${PROJECT_NAME} PRIVATE
            ${PYTHON_STATIC_INCLUDE_DIR}
        )
        add_dependencies(${PROJECT_NAME} python_build_ep)
    else()
        target_link_libraries(${PROJECT_NAME}
            pybind11::embed
            Python3::Python
            unrealng::core
        )
    endif()

    # Copy default config file to binary folder after successful compilation
    SET(CONFIG_FILE ${DATA_PATH}/configs/pentagon512k/unreal.ini)
    message("CONFIG_FILE: ${CONFIG_FILE}")
    message("Binary: ${CMAKE_BINARY_DIR}/${PROJECT_NAME}")
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
        copy ${CONFIG_FILE} "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/unreal.ini"
    )

    # Copy all ROM files to /rom sub-folder
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
        copy_directory ${DATA_PATH}/rom ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom
    )

    # Copy all test ROM files to /rom sub-folder
    add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E
        copy_directory ${DATA_PATH}/testrom ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rom
    )
else()
    # Produce static library; Create unrealng::automation_python alias for the target
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    # Add include directories for emulator headers
    target_include_directories(${PROJECT_NAME}
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_DIR}
        ${ROOT_DIR}/core/src
    )

    if(PYTHON_USE_STATIC)
        # Link against static Python built from source
        target_link_libraries(${PROJECT_NAME}
            PUBLIC
            pybind11::headers
            python_static
        )
        target_include_directories(${PROJECT_NAME} PUBLIC
            ${PYTHON_STATIC_INCLUDE_DIR}
        )
        add_dependencies(${PROJECT_NAME} python_build_ep)
    else()
        target_link_libraries(${PROJECT_NAME}
            PUBLIC
            pybind11::embed
            Python3::Python
        )
    endif()
endif()

# Set build properties
set_target_properties(${PROJECT_NAME} PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@loader_path/../lib"
    INSTALL_RPATH_USE_LINK_PATH TRUE
)

add_dependencies(${PROJECT_NAME}
    pybind11::pybind11_headers
)

# <Diagnostics>
get_target_property(APP_SOURCES ${PROJECT_NAME} SOURCES)
get_target_property(APP_INCLUDES ${PROJECT_NAME} INCLUDE_DIRECTORIES)

message(STATUS "<${PROJECT_NAME} diagnostics>")
message(STATUS "  PROJECT_NAME:             ${PROJECT_NAME}")
message(STATUS "  SOURCE_DIR:               ${SOURCE_DIR}")
message(STATUS "  PYTHON_USE_STATIC:        ${PYTHON_USE_STATIC}")
if(PYTHON_USE_STATIC)
    message(STATUS "  PYTHON_STATIC_LIB:        ${PYTHON_STATIC_LIB}")
endif()
message(STATUS "  APP_SOURCES:              ${APP_SOURCES}")
message(STATUS "  APP_INCLUDES:             ${APP_INCLUDES}")
message(STATUS "  DATA_PATH:                ${DATA_PATH}")
message(STATUS "</${PROJECT_NAME} diagnostics>")
# </Diagnostics>

message("<< <${PROJECT_NAME}>")