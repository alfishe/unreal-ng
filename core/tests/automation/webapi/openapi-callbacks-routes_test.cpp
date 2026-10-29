/// @file openapi-callbacks-routes_test.cpp
/// @brief The script callbacks' WebAPI routes (callbacks_api.cpp): each one is registered in emulator_api.h with its
/// method and has a path in the OpenAPI manifest, and the schemas they name exist. The handlers are drogon code that
/// core-tests do not link (their logic is core callback_control's, CallbackControl_Test), so the sources are read.

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

TEST(OpenApiCallbacksRoutes_Test, EveryRouteIsRegisteredAndDocumented)
{
    const std::string header = ReadFile(WebApiSources() / "emulator_api.h");
    const std::string manifest = ReadFile(WebApiSources() / "openapi" / "openapi_callbacks.inc");
    const std::string schemas = ReadFile(WebApiSources() / "openapi" / "openapi_schemas.inc");
    ASSERT_FALSE(header.empty());
    ASSERT_FALSE(manifest.empty()) << "openapi_callbacks.inc not found";
    struct Route
    {
        const char* path;
        const char* method;
    };
    for (const Route& route : {Route{"/api/v1/emulator/{id}/callbacks", "Get"},
                               Route{"/api/v1/emulator/{id}/callbacks/{cb_id}/resume", "Post"},
                               Route{"/api/v1/emulator/{id}/callbacks/{cb_id}", "Delete"},
                               Route{"/api/v1/emulator/{id}/callbacks/{cb_id}/policy", "Put"}})
    {
        EXPECT_NE(header.find(std::string("\"") + route.path + "\", drogon::" + route.method), std::string::npos)
            << route.method << " " << route.path << " is not registered";
        EXPECT_NE(manifest.find(std::string("\"") + route.path + "\""), std::string::npos)
            << route.path << " has no path in the OpenAPI manifest";
    }
    for (const char* schema : {"ScriptCallbackList", "ScriptCallbackResult"})
        EXPECT_NE(manifest.find(std::string("#/components/schemas/") + schema + "\""), std::string::npos) << schema;
    for (const char* schema : {"ScriptCallback", "ScriptCallbackList", "ScriptCallbackResult"})
        EXPECT_NE(schemas.find(std::string("schemas[\"") + schema + "\"]"), std::string::npos) << schema << " is not defined";
    EXPECT_NE(ReadFile(WebApiSources() / "openapi_spec.cpp").find("openapi/openapi_callbacks.inc"), std::string::npos);
}
