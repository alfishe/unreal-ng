/// @file openapi-next-routes_test.cpp
/// @brief Every ZX Next route of the WebAPI (`state/next/...` and `next/...` in emulator_api.h) has a path in the OpenAPI manifest
/// (core/automation/webapi/src/openapi/*.inc), with its query parameters and a response schema that exists. The handlers are drogon
/// code that core-tests do not link, so the routes are read from the sources: a route added without a manifest entry fails here.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <set>
#include <sstream>
#include <string>

#include "_helpers/testpathhelper.h"

namespace
{
std::string ReadFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

struct Route
{
    std::string path;
    std::string method;  // "Get" | "Post"
};

std::vector<Route> NextRoutes()
{
    const std::filesystem::path root = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src";
    const std::string header = ReadFile(root / "emulator_api.h");
    std::vector<Route> routes;
    const std::regex pattern(R"re(ADD_METHOD_TO\(EmulatorAPI::\w+,\s*"(/api/v1/emulator/\{id\}/(?:state/next|next)(?:/[\w-]+)*)",\s*drogon::(\w+))re");
    for (std::sregex_iterator it(header.begin(), header.end(), pattern), end; it != end; ++it)
        routes.push_back({(*it)[1], (*it)[2]});
    return routes;
}

std::string Manifest()
{
    const std::filesystem::path dir = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src" / "openapi";
    std::string all;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".inc")
            all += ReadFile(entry.path());
    return all;
}
}  // namespace

TEST(OpenApiNextRoutes_Test, TheRoutesAreFoundInTheHeader)
{
    std::set<std::string> found;
    for (const Route& r : NextRoutes())
        found.insert(r.method + " " + r.path);
    for (const char* expected : {"Get /api/v1/emulator/{id}/state/next/regs", "Get /api/v1/emulator/{id}/state/next/reg-journal",
                                 "Post /api/v1/emulator/{id}/next/reg-journal", "Get /api/v1/emulator/{id}/state/next/dma",
                                 "Get /api/v1/emulator/{id}/state/next/video", "Get /api/v1/emulator/{id}/state/next/palette",
                                 "Get /api/v1/emulator/{id}/state/next/ports", "Get /api/v1/emulator/{id}/state/next/nextreg",
                                 "Post /api/v1/emulator/{id}/next/nextreg", "Get /api/v1/emulator/{id}/state/next/copper",
                                 "Get /api/v1/emulator/{id}/state/next/sprites"})
        EXPECT_TRUE(found.count(expected)) << expected << " is not registered in emulator_api.h";
}

TEST(OpenApiNextRoutes_Test, EveryNextRouteHasAManifestPath)
{
    const std::string manifest = Manifest();
    ASSERT_FALSE(manifest.empty()) << "openapi/*.inc not found";
    const std::vector<Route> routes = NextRoutes();
    ASSERT_GE(routes.size(), 11u);
    for (const Route& r : routes)
        EXPECT_NE(manifest.find("\"" + r.path + "\""), std::string::npos) << r.method << " " << r.path << " has no path in the OpenAPI manifest";
}

TEST(OpenApiNextRoutes_Test, TheReportRoutesDeclareTheirQueryParametersAndAnExistingSchema)
{
    const std::string manifest = Manifest();
    const std::string schemas = manifest;
    struct Report
    {
        const char* path;
        std::vector<const char*> query;
        const char* schema;
    };
    for (const Report& r : {Report{"/api/v1/emulator/{id}/state/next/dma", {}, "NextDmaReport"},
                            Report{"/api/v1/emulator/{id}/state/next/video", {}, "NextVideoReport"},
                            Report{"/api/v1/emulator/{id}/state/next/palette", {"palette", "range"}, "NextPaletteReport"},
                            Report{"/api/v1/emulator/{id}/state/next/ports", {"port", "access"}, "NextPortsReport"},
                            Report{"/api/v1/emulator/{id}/state/next/nextreg", {"reg", "changed"}, "NextRegReadReport"},
                            Report{"/api/v1/emulator/{id}/state/next/copper", {"from", "count", "raw"}, "NextCopperReport"},
                            Report{"/api/v1/emulator/{id}/state/next/sprites", {"from", "count", "all"}, "NextSpritesReport"}})
    {
        const size_t at = manifest.find(std::string("{\"") + r.path + "\"");
        ASSERT_NE(at, std::string::npos) << r.path;
        const size_t next = manifest.find("{\"/api/v1/", at + 1);  // the entry runs to the next route of the table
        const std::string block = manifest.substr(at, next == std::string::npos ? std::string::npos : next - at);
        for (const char* q : r.query)
            EXPECT_NE(block.find(std::string("{\"") + q + "\""), std::string::npos) << r.path << " lacks query parameter " << q;
        EXPECT_NE(block.find(r.schema), std::string::npos) << r.path << " does not name " << r.schema;
        EXPECT_NE(schemas.find(std::string("fill(\"") + r.schema + "\""), std::string::npos) << r.schema << " is not defined";
    }
    EXPECT_NE(manifest.find("fill(\"NextRegWriteResult\""), std::string::npos);
    EXPECT_NE(manifest.find("\"/api/v1/emulator/{id}/next/nextreg\""), std::string::npos);
}
