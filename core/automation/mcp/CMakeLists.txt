cmake_minimum_required(VERSION 3.16)
project(automation_mcp LANGUAGES CXX)

# Explicitly set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

# Static library: MCP protocol layer (dispatcher, tools, router, resources —
# drogon-free by design) + the drogon HTTP adapter (automation-mcp.cpp) and
# loopback client (webapi-client.cpp). drogon/jsoncpp_static targets are
# created by the sibling webapi subtree, so the parent only adds this
# directory when ENABLE_WEBAPI_AUTOMATION is active.
add_library(${PROJECT_NAME} STATIC
    src/automation-mcp.h
    src/automation-mcp.cpp
    src/mcp-dispatcher.h
    src/mcp-dispatcher.cpp
    src/mcp-protocol.h
    src/mcp-resources.h
    src/mcp-resources.cpp
    src/mcp-router.h
    src/mcp-router.cpp
    src/mcp-tools.h
    src/mcp-tools.cpp
    src/mcp-tool-utils.h
    src/mcp-symbols.h
    src/mcp-symbols.cpp
    src/mcp-analysis.h
    src/mcp-analysis.cpp
    src/mcp-media.h
    src/mcp-media.cpp
    src/target-resolver.h
    src/target-resolver.cpp
    src/webapi-client.h
    src/webapi-client.cpp
)

add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

# Consumers (automation library, tests) include the drogon-free MCP headers;
# automation-mcp.h itself depends on neither drogon nor jsoncpp
target_include_directories(${PROJECT_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        drogon
        jsoncpp_static
)

# Bridge executable: stdio JSON-RPC pipe → MCP HTTP endpoint, zero deps
add_subdirectory(bridge)

message("<< <${PROJECT_NAME}>")
