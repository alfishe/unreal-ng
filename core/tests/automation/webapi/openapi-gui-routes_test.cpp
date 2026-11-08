/// @file openapi-gui-routes_test.cpp
/// @brief The GUI control plane's WebAPI routes (gui_api.cpp, gui-control-plane design §4.10): each one is registered
/// in emulator_api.h with its method and has a path in the OpenAPI manifest. The handlers are drogon code core-tests
/// do not link (their logic is GuiPlane's, GuiMethods_Test), so the sources are read.

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

TEST(OpenApiGuiRoutes_Test, EveryRouteIsRegisteredAndDocumented)
{
    const std::string header = ReadFile(WebApiSources() / "emulator_api.h");
    const std::string manifest = ReadFile(WebApiSources() / "openapi" / "openapi_gui.inc");
    ASSERT_FALSE(header.empty());
    ASSERT_FALSE(manifest.empty()) << "openapi_gui.inc not found";
    struct Route
    {
        const char* path;
        const char* method;
    };
    for (const Route& route : {Route{"/api/v1/gui", "Get"}, Route{"/api/v1/gui/catalogue", "Get"},
                               Route{"/api/v1/gui/state", "Get"}, Route{"/api/v1/gui/{method}", "Post"}})
    {
        EXPECT_NE(header.find(std::string("\"") + route.path + "\", drogon::" + route.method), std::string::npos)
            << route.method << " " << route.path << " is not registered";
        EXPECT_NE(manifest.find(std::string("\"") + route.path + "\""), std::string::npos)
            << route.path << " has no path in the OpenAPI manifest";
    }
    EXPECT_NE(ReadFile(WebApiSources() / "openapi_spec.cpp").find("openapi/openapi_gui.inc"), std::string::npos);
}
