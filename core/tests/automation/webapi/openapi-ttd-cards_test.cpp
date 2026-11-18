/// @file openapi-ttd-cards_test.cpp
/// @brief The WebAPI's time travel on a sound card's CPU (gs-debugger design §8) in the OpenAPI manifest: the `cpu`
/// option of POST /ttd/seek, /ttd/step-instruction, /ttd/reverse-step and /ttd/reverse-continue, the card move's
/// response and the cards[] readout of GET /ttd/position and /ttd/status. The handlers pass the JSON body to
/// TTDControl unchanged (OptionsFromJson), whose replies TTDControl_CardPositions_Test checks; the drogon code is not
/// linked into core-tests, so the manifest is read from its sources.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include "_helpers/testpathhelper.h"

namespace
{
std::string Manifest()
{
    const std::filesystem::path dir = TestPathHelper::FindProjectRoot() / "core" / "automation" / "webapi" / "src" / "openapi";
    std::string all;
    for (const auto& entry : std::filesystem::directory_iterator(dir))
        if (entry.path().extension() == ".inc")
        {
            std::ifstream in(entry.path(), std::ios::binary);
            all += std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }
    return all;
}
}  // namespace

TEST(OpenApiTTDCards_Test, TheCardOptionsAndReadoutsAreDescribed)
{
    const std::string manifest = Manifest();
    for (const char* request : {"TTDSeekRequest", "TTDStepInstructionRequest", "TTDReverseStepRequest",
                                "TTDReverseContinueRequest"})
    {
        const std::string cpu = std::string("schemas[\"") + request + "\"][\"properties\"][\"cpu\"]";
        EXPECT_NE(manifest.find(cpu), std::string::npos) << request << " has no cpu";
    }
    EXPECT_NE(manifest.find("schemas[\"TTDSeekRequest\"][\"properties\"][\"instruction\"]"), std::string::npos);
    EXPECT_NE(manifest.find("schemas[\"TTDStepInstructionRequest\"][\"properties\"][\"count\"]"), std::string::npos);
    EXPECT_NE(manifest.find("schemas[\"TTDReverseContinueRequest\"][\"properties\"][\"max_frames\"]"), std::string::npos);
    // A card's reverse continue takes no pcs (the handler passes a body with cpu and without pcs on)
    EXPECT_EQ(manifest.find("schemas[\"TTDReverseContinueRequest\"][\"required\"].append(\"pcs\")"), std::string::npos);
    for (const char* response : {"TTDPositionResponse", "TTDStatusResponse"})
    {
        const std::string cards = std::string("schemas[\"") + response + "\"][\"properties\"][\"cards\"][\"items\"][\"$ref\"] = \"#/components/schemas/TTDCardPosition\"";
        EXPECT_NE(manifest.find(cards), std::string::npos) << response << " has no cards[]";
    }
    for (const char* field : {"instruction", "card_position", "caught_up", "main"})
        EXPECT_NE(manifest.find(std::string("schemas[\"TTDCardPosition\"][\"properties\"][\"") + field + "\"]"),
                  std::string::npos)
            << field;
    for (const char* status : {"\"ok\"", "\"at_start\"", "\"at_end\"", "\"no_hit\"", "\"no_breakpoints\"", "\"no_count\""})
        EXPECT_NE(manifest.find(status), std::string::npos) << status;
    EXPECT_NE(manifest.find("schemas[\"TTDCardMoveResponse\"][\"properties\"][\"breakpoint\"]"), std::string::npos);
}
