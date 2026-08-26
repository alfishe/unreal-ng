cmake_minimum_required(VERSION 3.12)
project(automation_gdb LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

set(SOURCES
    src/automation-gdb.cpp
    src/gdbserver.cpp
    src/gdbpacket.cpp
    src/gdbdispatcher.cpp
    src/gdbtarget_z80.cpp
)

set(HEADERS
    include/automation-gdb.h
    include/gdbserver.h
    include/gdbpacket.h
    include/gdbdispatcher.h
    include/gdbtarget_z80.h
)

add_library(${PROJECT_NAME} STATIC ${SOURCES} ${HEADERS})
add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(${PROJECT_NAME}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        $<TARGET_PROPERTY:unrealng::core,INCLUDE_DIRECTORIES>
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        unrealng::core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ENABLE_GDB_AUTOMATION
)

message("<< <${PROJECT_NAME}>")
