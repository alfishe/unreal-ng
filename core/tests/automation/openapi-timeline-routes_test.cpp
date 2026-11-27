/// @file openapi-timeline-routes_test.cpp
/// @brief The twin timeline's WebAPI routes (debug_api.cpp): registered in emulator_api.h, described in the OpenAPI
/// manifest with their parameters, the route exempt from the automation gate (a read and a lease change nothing in the
/// machine). The handlers are drogon code core-tests do not link (their logic is timeline::Read / SetArmed,
/// TwinTimeline_Test), so the sources are read.

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
}  // namespace

TEST(OpenApiTimelineRoutes_Test, TheRoutesAreRegisteredAndDescribed)
{
    const std::filesystem::path root = TestPathHelper::FindProjectRoot();
    const std::filesystem::path webapi = root / "core" / "automation" / "webapi" / "src";
    const std::string header = ReadFile(webapi / "emulator_api.h");
    const std::string manifest = ReadFile(webapi / "openapi" / "openapi_debug.inc");
    const std::string handlers = ReadFile(webapi / "api" / "debug_api.cpp");
    ASSERT_FALSE(header.empty());
    ASSERT_FALSE(manifest.empty());
    ASSERT_FALSE(handlers.empty());
    EXPECT_NE(header.find("\"/api/v1/emulator/{id}/debug/timeline\", drogon::Get"), std::string::npos);
    EXPECT_NE(header.find("\"/api/v1/emulator/{id}/debug/timeline/arm\", drogon::Post"), std::string::npos);
    EXPECT_NE(manifest.find("\"/api/v1/emulator/{id}/debug/timeline\""), std::string::npos);
    EXPECT_NE(manifest.find("\"/api/v1/emulator/{id}/debug/timeline/arm\""), std::string::npos);
    for (const char* parameter : {"cpu", "span", "recent", "density", "client", "lease_ms", "arm"})
    {
        EXPECT_NE(manifest.find(std::string("\"") + parameter + "\", \"query\""), std::string::npos)
            << parameter << " is not documented";
        EXPECT_NE(handlers.find(std::string("\"") + parameter + "\""), std::string::npos)
            << parameter << " is not read";
    }
    for (const char* field : {"lanes", "now", "recent", "next", "markers", "density", "gap_t", "duration_units"})
        EXPECT_NE(manifest.find(field), std::string::npos) << field << " is not described";

    const std::string gate = ReadFile(root / "core" / "src" / "emulator" / "execution" / "automationgate.cpp");
    EXPECT_NE(gate.find("\"/debug/timeline\""), std::string::npos) << "a lease changes nothing in the machine";
}
