cmake_minimum_required(VERSION 3.12)
project(automation_zesarux LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

get_filename_component(ROOT_DIR ../../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Text-protocol server + condition evaluator (dzrp::IDebugInterface only)
set(SERVER_SOURCES
    src/zrcpserver.cpp
    src/zesaruxcondition.cpp
)

# Emulator-bound lifecycle (needs the DeZog adapter for a second instance)
set(LIFECYCLE_SOURCES
    src/automation-zesarux.cpp
)

set(HEADERS
    include/zrcptypes.h
    include/zrcpserver.h
    include/zesaruxcondition.h
    include/automation-zesarux.h
)

# The module reuses the DeZog adapter (dzrp::IDebugInterface implementation);
# without automation_dezog there is nothing to serve.
if (TARGET automation_dezog)
    set(ZESARUX_HAS_DEZOG ON)
else ()
    set(ZESARUX_HAS_DEZOG OFF)
    message(STATUS "${PROJECT_NAME}: automation_dezog not available - module disabled")
endif ()

if (ZESARUX_HAS_DEZOG)
    add_library(${PROJECT_NAME} STATIC ${SERVER_SOURCES} ${LIFECYCLE_SOURCES} ${HEADERS})
    add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../dezog/include   # dzrpserver.h (dzrp::IDebugInterface)
        ${ROOT_DIR}/core/automation/cli/include        # platform-sockets.h (header-only)
    )

    # The lifecycle source compiles DezogDebugAdapter (emulator headers)
    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )

    target_link_libraries(${PROJECT_NAME} PUBLIC automation_dezog)

    if (WIN32)
        target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    endif ()

    if (UNIX AND NOT APPLE)
        target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
    endif ()
endif ()

message("<< <${PROJECT_NAME}>")
