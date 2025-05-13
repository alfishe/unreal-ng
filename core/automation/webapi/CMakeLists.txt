cmake_minimum_required (VERSION 3.10)

project(automation_webapi LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set paths
set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Set specific build options for Drogon framework (located in lib/drogon)
option(BUILD_CTL "Build drogon_ctl" OFF)
option(BUILD_EXAMPLES "Build examples" OFF)
option(BUILD_ORM "Build orm" OFF)
option(COZ_PROFILING "Use coz for profiling" OFF)
option(BUILD_SHARED_LIBS "Build drogon as a shared lib" OFF)
option(BUILD_DOC "Build Doxygen documentation" OFF)
option(BUILD_BROTLI "Build Brotli" OFF)
add_subdirectory(lib/drogon)

set(COMMON_SOURCES
    src/hello_world_api.cpp
    src/hello_world_api.h
    src/emulator_api.cpp
    src/emulator_api.h
    #src/emulator_websocket.cpp
    #src/emulator_websocket.h
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
else ()
    # Build as submodule
    add_library(${PROJECT_NAME} STATIC
        ${COMMON_SOURCES}
    )

    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})
endif ()


target_include_directories(${PROJECT_NAME} PRIVATE
    ${JSONCPP_INCLUDE_DIRS}     # Allow compiler to find jsoncpp headers

    ${ROOT_DIR}/core/src        # Core includes
)

# Link service executable with Drogon library
target_link_libraries(${PROJECT_NAME} PRIVATE
    drogon
)