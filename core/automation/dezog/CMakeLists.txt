cmake_minimum_required(VERSION 3.12)
project(automation_dezog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

get_filename_component(ROOT_DIR ../../../ ABSOLUTE BASE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Transport-agnostic protocol + server layer (no emulator dependency)
set(PROTOCOL_SOURCES
    src/dzrpprotocol.cpp
    src/dzrpserver.cpp
)

# Emulator-bound layer (requires unrealng::core)
set(ADAPTER_SOURCES
    src/dezogdebugadapter.cpp
    src/automation-dezog.cpp
)

set(HEADERS
    include/dzrptypes.h
    include/dzrpprotocol.h
    include/dzrpserver.h
    include/dezogdebugadapter.h
    include/automation-dezog.h
)

# Standalone configure (cd core/automation/dezog && cmake ..) has no core target:
# build only the protocol layer + mock test server in that case.
if (TARGET unrealng::core)
    set(DEZOG_HAS_CORE ON)
else ()
    set(DEZOG_HAS_CORE OFF)
    message(STATUS "${PROJECT_NAME}: unrealng::core not available - building protocol layer only")
endif ()

if (DEZOG_HAS_CORE)
    add_library(${PROJECT_NAME} STATIC ${PROTOCOL_SOURCES} ${ADAPTER_SOURCES} ${HEADERS})
else ()
    add_library(${PROJECT_NAME} STATIC ${PROTOCOL_SOURCES} ${HEADERS})
endif ()

add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${ROOT_DIR}/core/automation/cli/include   # platform-sockets.h (header-only)
)

if (DEZOG_HAS_CORE)
    target_include_directories(${PROJECT_NAME} PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )
    target_link_libraries(${PROJECT_NAME} PUBLIC unrealng::core)
endif ()

# Platform-specific libraries
if (WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif ()

if (UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif ()

# Standalone mock test server (no emulator dependency, used by the Python verifier)
option(BUILD_DEZOG_TEST_SERVER "Build DZRP standalone mock test server" ON)

if (BUILD_DEZOG_TEST_SERVER)
    add_executable(dezog-test-server
        test/test-server.cpp
        ${PROTOCOL_SOURCES}
    )
    target_include_directories(dezog-test-server PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${ROOT_DIR}/core/automation/cli/include
    )

    if (WIN32)
        target_link_libraries(dezog-test-server PRIVATE ws2_32)
    endif ()
    if (UNIX AND NOT APPLE)
        target_link_libraries(dezog-test-server PRIVATE pthread)
    endif ()
endif ()

# Headless real-emulator host (production module + one live emulator instance).
# Used by tools/verification/dezog/verify_dzrp_emulator.py --launch.
option(BUILD_DEZOG_EMULATOR_HOST "Build headless DeZog emulator host" ON)

if (BUILD_DEZOG_EMULATOR_HOST AND DEZOG_HAS_CORE)
    add_executable(dezog-emulator-host
        test/emulator-host.cpp
    )
    target_include_directories(dezog-emulator-host PRIVATE
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
    )
    target_link_libraries(dezog-emulator-host PRIVATE ${PROJECT_NAME})

    if (UNIX AND NOT APPLE)
        target_link_libraries(dezog-emulator-host PRIVATE pthread)
    endif ()
endif ()

message("<< <${PROJECT_NAME}>")
