cmake_minimum_required(VERSION 3.16)
project(swsignature LANGUAGES CXX)

# Media signatures (docs/inprogress/2026-10-08-steamdeck-client/input-and-profiles.md §4, controller manager D16): tape
# payloads, TR-DOS file sets, the fuzzy file-set rule, snapshot memory pages. The parsers are the core's, so the library
# links core (PUBLIC); core does not link it. Consumers: the automation surfaces, unreal-qt (the controller manager's
# signature hook), core-tests, POC 034. Added from core/src/CMakeLists.txt, so it inherits the core's warning flags.
# match.h is header-only and free of the core: core/controllers includes it for the profile match.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

include(${ROOT_DIR}/cmake/func.cmake)

option(ENABLE_SWSIGNATURE "Enable the media signature library" ON)

if(NOT ENABLE_SWSIGNATURE)
    message(STATUS "Signatures disabled - skipping ${PROJECT_NAME} library")
    return()
endif()

file(GLOB_RECURSE CPP_FILES ${SOURCE_DIR}/*.cpp)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/build/" FALSE OUTVAR CPP_FILES)
EXCLUDE_FILES_FROM_DIR_IN_LIST("${CPP_FILES}" "/CMakeFiles/" FALSE OUTVAR CPP_FILES)

add_library(${PROJECT_NAME} STATIC ${CPP_FILES})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC ${SOURCE_DIR}/)
target_compile_definitions(${PROJECT_NAME} PUBLIC ENABLE_SWSIGNATURE)
target_link_libraries(${PROJECT_NAME} PUBLIC core)

if(CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _DEBUG)
endif()

message("<< <${PROJECT_NAME}>")
