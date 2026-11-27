cmake_minimum_required(VERSION 3.16)
project(automation_mcp LANGUAGES CXX)

# Explicitly set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

message(">> <${PROJECT_NAME}>")

# The MCP protocol layer (dispatcher, tools, router, resources): drogon-free, jsoncpp only. Its own library, so the
# server and core-tests link the same objects instead of the tests compiling all of it a second time
add_library(automation_mcp_core STATIC
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
    src/mcp-asm.h
    src/mcp-asm.cpp
    src/mcp-signature.h
    src/mcp-signature.cpp
    src/mcp-analysis.h
    src/mcp-analysis.cpp
    src/mcp-media.h
    src/mcp-media.cpp
    src/mcp-slots.cpp
    src/mcp-controllers.h
    src/mcp-controllers.cpp
    src/mcp-actions.h
    src/mcp-actions.cpp
    src/mcp-gui.h
    src/mcp-gui.cpp
    src/mcp-streamdeck.h
    src/mcp-streamdeck.cpp
    src/mcp-bus.cpp
    src/mcp-history.cpp
    src/mcp-jobs.cpp
    src/mcp-audio.h
    src/mcp-audio.cpp
    src/mcp-gsanalysis.h
    src/mcp-gsanalysis.cpp
    src/mcp-vdac4.h
    src/mcp-vdac4.cpp
    src/target-resolver.h
    src/target-resolver.cpp
)

# Consumers (automation library, tests) include the drogon-free MCP headers
target_include_directories(automation_mcp_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(automation_mcp_core PUBLIC
    jsoncpp_static
)

# The server: the drogon HTTP adapter (automation-mcp.cpp) and the loopback client (webapi-client.cpp) over the
# protocol layer. drogon/jsoncpp_static targets are created by the sibling webapi subtree, so the parent only adds
# this directory when ENABLE_WEBAPI_AUTOMATION is active. automation-mcp.h itself depends on neither
add_library(${PROJECT_NAME} STATIC
    src/automation-mcp.h
    src/automation-mcp.cpp
    src/webapi-client.h
    src/webapi-client.cpp
)

add_library(unrealng::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        automation_mcp_core
    PRIVATE
        drogon
        jsoncpp_static
)

# Bridge executable: stdio JSON-RPC pipe → MCP HTTP endpoint, zero deps
add_subdirectory(bridge)

if (TARGET unreal-mcp-bridge)
    add_dependencies(${PROJECT_NAME} unreal-mcp-bridge)
endif ()

message("<< <${PROJECT_NAME}>")

