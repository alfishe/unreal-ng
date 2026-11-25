/// @file openapi-history-routes_test.cpp
/// @brief The device history WebAPI routes (history_api.cpp): each one is registered in emulator_api.h and has a path
/// in the OpenAPI manifest, and the read documents and takes its cursor (`since`). The handlers are drogon code that
/// core-tests do not link (their logic is HistoryControl's, HistoryControl_Test), so the sources are read.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "_helpers/testpathhelper.h"

namespace
{
std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path WebApiSources()
{
    return TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src";
}
}  // namespace

TEST(OpenApiHistoryRoutes_Test, EveryRouteIsRegisteredAndTheReadTakesSince)
{
    const std::string header = ReadFile(WebApiSources() / "emulator_api.h");
    const std::string manifest = ReadFile(WebApiSources() / "openapi" / "openapi_history.inc");
    const std::string handlers = ReadFile(WebApiSources() / "api" / "history_api.cpp");
    ASSERT_FALSE(header.empty());
    ASSERT_FALSE(manifest.empty()) << "openapi_history.inc not found";
    ASSERT_FALSE(handlers.empty()) << "history_api.cpp not found";
    struct Route
    {
        const char* path;
        const char* method;
    };
    for (const Route& route : {Route{"/api/v1/emulator/{id}/history", "Get"},
                               Route{"/api/v1/emulator/{id}/history/{device}", "Get"},
                               Route{"/api/v1/emulator/{id}/history/{device}/config", "Post"},
                               Route{"/api/v1/emulator/{id}/history/{device}/clear", "Post"},
                               Route{"/api/v1/emulator/{id}/history/{device}/pause", "Post"},
                               Route{"/api/v1/emulator/{id}/history/{device}/resume", "Post"}})
    {
        EXPECT_NE(header.find(std::string("\"") + route.path + "\", drogon::" + route.method), std::string::npos)
            << route.method << " " << route.path << " is not registered";
        EXPECT_NE(manifest.find(std::string("\"") + route.path + "\""), std::string::npos)
            << route.path << " has no path in the OpenAPI manifest";
    }
    EXPECT_NE(manifest.find("read[\"parameters\"][3][\"name\"] = \"since\""), std::string::npos)
        << "the read documents its cursor";
    for (const char* field : {"next_since", "missed", "restarted"})
        EXPECT_NE(manifest.find(field), std::string::npos) << field << " is not described";
    EXPECT_NE(handlers.find("getParameter(\"since\")"), std::string::npos) << "the read takes its cursor";
    EXPECT_NE(ReadFile(WebApiSources() / "openapi_spec.cpp").find("openapi/openapi_history.inc"), std::string::npos);
}
