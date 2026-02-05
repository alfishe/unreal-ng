cmake_minimum_required(VERSION 3.12)
project(automation LANGUAGES CXX)

# C++ standard is inherited from root CMakeLists.txt (C++20)

message(">> <${PROJECT_NAME}>")

# Set paths
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (DATA_PATH ${ROOT_DIR}/data)

option(ENABLE_AUTOMATION "Allow automation" ON)
# Define options for each automation module
option(ENABLE_LUA_AUTOMATION "Enable automation for Lua" ON)
option(ENABLE_PYTHON_AUTOMATION "Enable automation for Python" OFF)
option(ENABLE_WEBAPI_AUTOMATION "Enable automation for WebAPI" ON)
option(ENABLE_CLI_AUTOMATION "Enable automation for CLI" ON)

# Respect parent's ENABLE_AUTOMATION option
if (NOT ENABLE_AUTOMATION)
    set(ENABLE_LUA_AUTOMATION OFF)
    set(ENABLE_PYTHON_AUTOMATION OFF)
    set(ENABLE_WEBAPI_AUTOMATION OFF)
    set(ENABLE_CLI_AUTOMATION OFF)
endif ()

# Add subdirectories based on automation options
if (ENABLE_LUA_AUTOMATION)
    add_subdirectory(lua)
    set(AUTOMATION_LUA_TARGET automation_lua)
endif ()

if (ENABLE_PYTHON_AUTOMATION)
    add_subdirectory(python)
    set(AUTOMATION_PYTHON_TARGET automation_python)
endif ()

if (ENABLE_WEBAPI_AUTOMATION)
    add_subdirectory(webapi)
    set(AUTOMATION_WEBAPI_TARGET automation_webapi)
endif ()

if (ENABLE_CLI_AUTOMATION)
    add_subdirectory(cli)
    set(AUTOMATION_CLI_TARGET automation-cli)
endif ()

set (SOURCES
    automation.cpp
    automation.h
    batch-command-processor.cpp
    batch-command-processor.h
)


if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(STATUS "unrealng::core library will be built and linked from ${PROJECT_NAME}")

    add_subdirectory(${ROOT_DIR}/core/src core)

    # Put binary and configuration files to /bin subfolder
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
    
    add_executable(${PROJECT_NAME}
        ${SOURCES}
        automation-main.cpp
    )
    
    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )

    if (APPLE)
        target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
        set(common_libraries iconv)
    endif (APPLE)

    if (UNIX AND NOT APPLE)
        set(common_libraries -static-libstdc++)
    endif (UNIX AND NOT APPLE)

    # Conditional pthread linking - only link if webapi is disabled. drogon/trentor adds Threads::Threads linkage
    if (NOT ENABLE_WEBAPI_AUTOMATION)
        list(APPEND common_libraries Threads::Threads)
    endif()

    # Link all automation libraries conditionally (based on their settings)
    target_link_libraries(${PROJECT_NAME}
        PRIVATE
            $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:automation_lua>          # Conditional linking
            $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:automation_python>    # Conditional linking
            $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:automation_webapi>    # Conditional linking
            $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:automation-cli>          # Conditional linking

        PUBLIC
            unrealng::core
            ${common_libraries}
    )

    # Copy default config file to binary folder after successful compilation
    SET (CONFIG_FILE ${DATA_PATH}/configs/pentagon512k/unreal.ini)
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
else ()
    # Produce static library;  Create unrealng::automation alias for the target
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )

    # Link all automation libraries conditionally (based on their settings)
    # NOTE: Automation sublibraries are linked PUBLIC, not PRIVATE, because they contain
    # template instantiations (e.g., sol2 bindings in automation_lua) that need to be
    # visible to the final executable. With PRIVATE linkage, the linker may not pull in
    # these symbols when linking static libraries transitively.
    target_link_libraries(${PROJECT_NAME}
        PUBLIC
            $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:${AUTOMATION_CLI_TARGET}>
            $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${AUTOMATION_LUA_TARGET}>
            $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:${AUTOMATION_PYTHON_TARGET}>
            $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:${AUTOMATION_WEBAPI_TARGET}>
            unrealng::core
    )
endif ()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${ROOT_DIR}/core/automation
    $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${CMAKE_CURRENT_SOURCE_DIR}/lua/src>
    $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:${CMAKE_CURRENT_SOURCE_DIR}/lua/lib/sol2/sol2-3.5.0/include>
    $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:${CMAKE_CURRENT_SOURCE_DIR}/python/src>
    $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:${CMAKE_CURRENT_SOURCE_DIR}/webapi/src>
    $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:${CMAKE_CURRENT_SOURCE_DIR}/cli/include>
)

# Add conditional compiler definitions for automation modules
# Must exactly match the definitions in unreal-qt/CMakeLists.txt!!!
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:ENABLE_LUA_AUTOMATION>
    $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:ENABLE_PYTHON_AUTOMATION>
    $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:ENABLE_WEBAPI_AUTOMATION>
    $<$<BOOL:${ENABLE_CLI_AUTOMATION}>:ENABLE_CLI_AUTOMATION>
)

# <Diagnostics>
message(STATUS "<${PROJECT_NAME} diagnostics>")
message(STATUS "Options:")
message(STATUS "  ENABLE_AUTOMATION: ${ENABLE_AUTOMATION}")
message(STATUS "    ENABLE_LUA_AUTOMATION: ${ENABLE_LUA_AUTOMATION}")
message(STATUS "    ENABLE_PYTHON_AUTOMATION: ${ENABLE_PYTHON_AUTOMATION}")
message(STATUS "    ENABLE_CLI_AUTOMATION: ${ENABLE_CLI_AUTOMATION}")
message(STATUS "    ENABLE_WEBAPI_AUTOMATION: ${ENABLE_WEBAPI_AUTOMATION}")
message(STATUS "Settings:")
message(STATUS "  PROJECT_NAME:        ${PROJECT_NAME}")
message(STATUS "  SRC_DIR:             ${SRC_DIR}")
message(STATUS "  LIB_DIR:             ${LIB_DIR}")

message(STATUS "  INCLUDE_DIRECTORIES:")
get_property(include_dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach (dir ${include_dirs})
    message(STATUS "                       ${dir}")
endforeach ()

message(STATUS "</<${PROJECT_NAME} diagnostics>")
# </Diagnostics>

message("<< <${PROJECT_NAME}>")