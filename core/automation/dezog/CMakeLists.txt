cmake_minimum_required(VERSION 3.12)
project(automation-dezog LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

message(">> <${PROJECT_NAME}>")

set(SOURCES
    src/dzrpprotocol.cpp
    src/dzrpserver.cpp
)

set(HEADERS
    include/dzrptypes.h
    include/dzrpprotocol.h
    include/dzrpserver.h
)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../cli/include  # For platform-sockets.h
)

# Platform-specific libraries
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE pthread)
endif()

# Test executable
option(BUILD_DEZOG_TEST "Build DZRP standalone test server" ON)

if(BUILD_DEZOG_TEST)
    add_executable(dezog-test-server
        test/test-server.cpp
    )
    target_link_libraries(dezog-test-server PRIVATE ${PROJECT_NAME})

    if(UNIX AND NOT APPLE)
        target_link_libraries(dezog-test-server PRIVATE pthread)
    endif()
endif()

message("<< <${PROJECT_NAME}>")
