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
                                 "Get /api/v1/emulator/{id}/state/next/sprites", "Post /api/v1/emulator/{id}/next/layer-capture",
                                 "Get /api/v1/emulator/{id}/state/next/pixel",
                                 "Get /api/v1/emulator/{id}/state/next/sd", "Post /api/v1/emulator/{id}/next/sd-journal",
                                 "Get /api/v1/emulator/{id}/state/next/divmmc", "Post /api/v1/emulator/{id}/next/divmmc-journal",
                                 "Get /api/v1/emulator/{id}/state/next/nmi", "Post /api/v1/emulator/{id}/next/nmi-journal",
                                 "Get /api/v1/emulator/{id}/state/next/audio", "Get /api/v1/emulator/{id}/state/next/input",
                                 "Get /api/v1/emulator/{id}/state/next/uart",
                                 "Get /api/v1/emulator/{id}/state/next/magic-port", "Post /api/v1/emulator/{id}/next/magic-port"})
        EXPECT_TRUE(found.count(expected)) << expected << " is not registered in emulator_api.h";
}

TEST(OpenApiNextRoutes_Test, EveryNextRouteHasAManifestPath)
{
    const std::string manifest = Manifest();
    ASSERT_FALSE(manifest.empty()) << "openapi/*.inc not found";
    const std::vector<Route> routes = NextRoutes();
    ASSERT_GE(routes.size(), 13u);
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
                            Report{"/api/v1/emulator/{id}/state/next/sprites", {"from", "count", "all"}, "NextSpritesReport"},
                            Report{"/api/v1/emulator/{id}/state/next/pixel", {"x", "y", "sx"}, "NextPixelReport"}})
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

// Per-layer capture (docs/inprogress/2026-10-07-zx-next/design-layer-capture.md): the routes outside state/next and next/, the parameters of
// capture/screen and the schemas they answer with
TEST(OpenApiNextRoutes_Test, TheLayerCaptureRoutesParametersAndSchemasAreDocumented)
{
    const std::filesystem::path root = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src";
    const std::string header = ReadFile(root / "emulator_api.h");
    EXPECT_NE(header.find("\"/api/v1/emulator/{id}/capture/next-layers\", drogon::Get"), std::string::npos);
    EXPECT_NE(header.find("\"/api/v1/emulator/{id}/capture/screen\", drogon::Get"), std::string::npos);
    const std::string manifest = Manifest();
    for (const char* path : {"/api/v1/emulator/{id}/capture/next-layers", "/api/v1/emulator/{id}/next/layer-capture"})
        EXPECT_NE(manifest.find(std::string("\"") + path + "\""), std::string::npos) << path;
    // capture/screen: the layer enum with every word, alpha, and the rgba format
    const size_t screen = manifest.find("\"/api/v1/emulator/{id}/capture/screen\"");
    ASSERT_NE(screen, std::string::npos);
    const size_t screenEnd = manifest.find("// Per-layer capture", screen);
    const std::string block = manifest.substr(screen, screenEnd == std::string::npos ? std::string::npos : screenEnd - screen);
    EXPECT_NE(block.find("{\"layer\", \"query\""), std::string::npos);
    EXPECT_NE(block.find("{\"alpha\", \"query\""), std::string::npos);
    for (const char* word : {"composed", "ula", "layer2", "tilemap", "sprites", "ids", "rgba"})
        EXPECT_NE(block.find(std::string("\"") + word + "\""), std::string::npos) << "capture/screen does not list " << word;
    EXPECT_NE(block.find("props[\"legend\"]"), std::string::npos);
    // capture/next-layers: format and alpha, a response schema that exists
    const size_t layers = manifest.find("\"/api/v1/emulator/{id}/capture/next-layers\"][\"get\"]");
    ASSERT_NE(layers, std::string::npos);
    const std::string layersBlock = manifest.substr(layers, 3500);
    EXPECT_NE(layersBlock.find("\"format\""), std::string::npos);
    EXPECT_NE(layersBlock.find("\"alpha\""), std::string::npos);
    EXPECT_NE(layersBlock.find("#/components/schemas/NextLayers"), std::string::npos);
    // the POST: its body and the schemas
    EXPECT_NE(manifest.find("#/components/schemas/NextLayerCaptureState"), std::string::npos);
    for (const char* schema : {"NextLayers", "NextLayerCaptureState", "NextLayerLegendEntry"})
        EXPECT_NE(manifest.find(std::string("fill(\"") + schema + "\""), std::string::npos) << schema << " is not defined";
    EXPECT_NE(manifest.find("{\"layer_capture\", \"object\""), std::string::npos) << "the video report schema lists layer_capture";
}
