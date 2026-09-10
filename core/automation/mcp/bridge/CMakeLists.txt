cmake_minimum_required(VERSION 3.16)
project(unreal-mcp-bridge LANGUAGES CXX)

# Explicitly set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

# Zero-dependency stdio→HTTP pipe daemon (std + raw sockets only).
# Deliberately NOT linked against drogon/jsoncpp so IDE sandboxes can run it
# without pulling the whole emulator with them.
add_executable(${PROJECT_NAME}
    src/main.cpp
    src/mini-http.cpp
    src/mini-http.h
)

if (WIN32)
    # Covers MSVC and MinGW (the #pragma comment(lib) only helps MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif ()

set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

message("<< <${PROJECT_NAME}>")

