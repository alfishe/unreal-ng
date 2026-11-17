/// @file cli-statetransfer_test.cpp
/// @brief CLI `snapshot transfer` (cli-statetransfer.h): the arguments become the StateTransferRequest every surface
/// builds, the reply becomes the CLI's text or the WebAPI's JSON - on real machines, no CLI socket (model what-if W0b).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-statetransfer.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"

namespace
{
StateTransferRequest Parse(const std::vector<std::string>& args, bool& json)
{
    StateTransferRequest request;
    std::string error;
    EXPECT_TRUE(CliStateTransfer::ParseArgs(args, request, json, error)) << error;
    return request;
}
}  // namespace

TEST(CliStateTransfer_Test, ArgumentsBecomeTheRequest)
{
    bool json = false;
    StateTransferRequest existing = Parse({"emu-2", "--check", "--no-keep-frame"}, json);
    EXPECT_EQ(existing.to, "emu-2");
    EXPECT_TRUE(existing.model.empty());
    EXPECT_TRUE(existing.check);
    ASSERT_TRUE(existing.keepFramePosition.has_value());
    EXPECT_FALSE(*existing.keepFramePosition);
    EXPECT_FALSE(existing.start.has_value());
    EXPECT_FALSE(json);

    StateTransferRequest fresh = Parse({"--model", "PENTAGON", "--ram", "512", "--id", "fork", "--no-start", "--json"}, json);
    EXPECT_TRUE(fresh.to.empty());
    EXPECT_EQ(fresh.model, "PENTAGON");
    EXPECT_EQ(fresh.ramSizeKB, 512u);
    EXPECT_EQ(fresh.symbolicId, "fork");
    ASSERT_TRUE(fresh.start.has_value());
    EXPECT_FALSE(*fresh.start);
    EXPECT_FALSE(fresh.keepFramePosition.has_value()) << "omitted: the core's default";
    EXPECT_TRUE(json);

    EXPECT_EQ(Parse({"--to", "emu-3"}, json).to, "emu-3");
}

TEST(CliStateTransfer_Test, MalformedArgumentsAreRefusedWithTheReason)
{
    const struct
    {
        std::vector<std::string> args;
        const char* says;
    } cases[] = {
        {{"--ram", "lots", "--model", "48K"}, "--ram"},
        {{"--bogus"}, "unknown option '--bogus'"},
        {{"emu-2", "emu-3"}, "unexpected argument 'emu-3'"},
        {{"--model"}, "unknown option '--model'"},   // its value is missing
    };
    for (const auto& c : cases)
    {
        StateTransferRequest request;
        bool json = false;
        std::string error;
        EXPECT_FALSE(CliStateTransfer::ParseArgs(c.args, request, json, error)) << c.says;
        EXPECT_NE(error.find(c.says), std::string::npos) << error;
    }
}

TEST(CliStateTransfer_Test, TheReplyPrintsAsTextOrAsTheWebApiJson)
{
    EmulatorManager* manager = EmulatorManager::GetInstance();
    struct RemoveAll
    {
        EmulatorManager* manager;
        ~RemoveAll()
        {
            for (const auto& id : manager->GetEmulatorIds())
                manager->RemoveEmulator(id);
        }
    } removeAll{manager};
    auto source = manager->CreateEmulatorWithModelAndRAM("cli-transfer", "128k", 128, LoggerLevel::LogError);
    auto target = manager->CreateEmulatorWithModelAndRAM("cli-transfer", "PENTAGON", 128, LoggerLevel::LogError);
    ASSERT_TRUE(source && target);

    bool json = false;
    StateTransferRequest request = Parse({target->GetId(), "--check"}, json);
    request.sourceId = source->GetId();
    const StateTransferReply reply = StateTransferControl::Execute(request);
    const std::string text = CliStateTransfer::Render(reply, false);
    EXPECT_EQ(text.rfind("Transfer check " + source->GetId() + " -> " + target->GetId() + ": possible\n", 0), 0u) << text;
    EXPECT_NE(text.find("mode: cross-model; copied "), std::string::npos) << text;
    EXPECT_NE(text.find(reply.report.ToString()), std::string::npos) << "the report's own lines";

    const std::string asJson = CliStateTransfer::Render(reply, true);
    EXPECT_NE(asJson.find("\"mode\":\"cross-model\""), std::string::npos) << asJson;
    EXPECT_NE(asJson.find("\"check\":true"), std::string::npos) << asJson;

    request = Parse({"--model", "48K", "--check"}, json);
    request.sourceId = source->GetId();
    EXPECT_EQ(CliStateTransfer::Render(StateTransferControl::Execute(request), false),
              "Error: 'check' needs an existing target ('to'); a new instance is checked by creating it\n");
}
