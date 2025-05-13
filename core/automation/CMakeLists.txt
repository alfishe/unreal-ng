cmake_minimum_required(VERSION 3.12)
project(automation LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

message(">> <${PROJECT_NAME}>")

# Set paths
get_filename_component(ROOT_DIR ../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
set (DATA_PATH ${ROOT_DIR}/data)

# Define options for each automation module
option(ENABLE_LUA_AUTOMATION "Enable automation for Lua" ON)
option(ENABLE_PYTHON_AUTOMATION "Enable automation for Python" ON)
option(ENABLE_WEBAPI_AUTOMATION "Enable automation for WebAPI" ON)

# Respect parent's ENABLE_AUTOMATION option
if (NOT ENABLE_AUTOMATION)
    set(ENABLE_LUA_AUTOMATION OFF)
    set(ENABLE_PYTHON_AUTOMATION OFF)
    set(ENABLE_WEBAPI_AUTOMATION OFF)
endif()

# Add subdirectories based on automation options
if (ENABLE_LUA_AUTOMATION)
    add_subdirectory(lua)
endif()

if (ENABLE_PYTHON_AUTOMATION)
    add_subdirectory(python)
endif()

if (ENABLE_WEBAPI_AUTOMATION)
    add_subdirectory(webapi)
endif()

set(SOURCES
    automation.cpp
    automation.h
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

    # Link all dependencies
    if (APPLE)
        target_link_directories(${PROJECT_NAME} PRIVATE /usr/lib)
        set(common_libraries pthread iconv)
    endif (APPLE)

    if (UNIX AND NOT APPLE)
        set(common_libraries -static-libstdc++ pthread)
    endif (UNIX AND NOT APPLE)

    target_link_libraries(${PROJECT_NAME}
        automation_lua
        automation_python
        automation_webapi

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
else()
    # Produce static library;  Create unrealng::automation_lua alias for the target
    add_library(${PROJECT_NAME} STATIC ${SOURCES})
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )

    # Link libraries conditionally (depending on correspondent options)
    target_link_libraries(${PROJECT_NAME} PUBLIC
        $<$<BOOL:${ENABLE_LUA_AUTOMATION}>:automation_lua>
        $<$<BOOL:${ENABLE_PYTHON_AUTOMATION}>:automation_python>
        $<$<BOOL:${ENABLE_WEBAPI_AUTOMATION}>:automation_webapi>
    )
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/
    ${ROOT_DIR}/core/automation

    $<TARGET_PROPERTY:unrealng::automation_lua,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:unrealng::automation_python,INCLUDE_DIRECTORIES>
    $<TARGET_PROPERTY:unrealng::automation_webapi,INCLUDE_DIRECTORIES>
)

message("<< <${PROJECT_NAME}>")