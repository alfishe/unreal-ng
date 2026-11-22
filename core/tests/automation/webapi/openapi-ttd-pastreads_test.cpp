/// @file openapi-ttd-pastreads_test.cpp
/// @brief The WebAPI's reads of the past (PLAN #122 S1, V2, E4) in the routes and the OpenAPI manifest: POST
/// /ttd/memory-series and /ttd/screen-at are routed, every option TTDControl takes for them is described, and memory-at
/// names the moment it read. The handlers pass the JSON body to TTDControl (ranges as JSON text, frames joined), whose
/// replies TTDControl_PastReads_Test checks; the drogon code is not linked into core-tests, so the sources are read.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "_helpers/testpathhelper.h"
#include "debugger/ttd/ttdcontrol.h"

namespace
{
std::string ReadAll(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::string Manifest()
{
    const std::filesystem::path dir = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src" / "openapi";
    std::string all;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".inc")
            all += ReadAll(entry.path());
    return all;
}
}  // namespace

TEST(OpenApiTTDPastReads_Test, RoutesAndEveryOptionAreDescribed)
{
    const std::string routes =
        ReadAll(TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src" / "emulator_api.h");
    EXPECT_NE(routes.find("\"/api/v1/emulator/{id}/ttd/memory-series\", drogon::Post"), std::string::npos);
    EXPECT_NE(routes.find("\"/api/v1/emulator/{id}/ttd/screen-at\", drogon::Post"), std::string::npos);

    const std::string manifest = Manifest();
    for (const auto& [verb, var] : {std::pair<const char*, const char*>{"memory-series", "sb"}, {"screen-at", "qb"}})
    {
        EXPECT_NE(manifest.find(std::string("/api/v1/emulator/{id}/ttd/") + verb), std::string::npos) << verb;
        for (const std::string& option : ttd::TTDControl::OptionsFor(verb))
            EXPECT_NE(manifest.find(std::string(var) + "[\"properties\"][\"" + option + "\"]"), std::string::npos)
                << verb << " option " << option << " is not described";
    }
    // E4: memory-at's answer names its moment
    for (const char* field : {"moment (frame_start", "at_tinframe (where the CPU stood", "moment_text"})
        EXPECT_NE(manifest.find(field), std::string::npos) << field;
}
