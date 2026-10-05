/// @file cli-slots_test.cpp
/// @brief CLI `slots` (cli-slots.h): the arguments become the SlotControlRequest every surface builds, the replies
/// become the CLI's text - on a real machine, no CLI socket (ZX-bus slots tdd.md §2.5, the CLI's part of the parity
/// script; the replies themselves are SlotControl_Test's).

#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <vector>

#include "../../automation/cli/src/commands/cli-slots.h"
#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/soundcardscope.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/slots/slotconfig.h"
#include "emulator/slots/slotmanager.h"

namespace
{

SlotControlRequest Parse(const std::vector<std::string>& args, bool& json, std::string& error)
{
    SlotControlRequest request;
    EXPECT_TRUE(CliSlots::ParseArgs(args, request, json, error)) << error;
    return request;
}

} // namespace

TEST(CliSlots_Test, ArgumentsBecomeTheRequest)
{
    bool json = false;
    std::string error;

    SlotControlRequest list = Parse({}, json, error);
    EXPECT_EQ(list.verb, "list");
    EXPECT_FALSE(json);

    SlotControlRequest plug = Parse({"plug", "zxbus.next", "multisound", "dip=ym,saa", "gsRam=2m", "--replace",
                                     "--dry-run", "--media", "save", "--json"},
                                    json, error);
    EXPECT_EQ(plug.verb, "plug");
    EXPECT_EQ(plug.slot, "zxbus.next");
    EXPECT_EQ(plug.card, "multisound");
    EXPECT_EQ(plug.options, "dip=ym,saa gsRam=2m");
    EXPECT_TRUE(plug.replaceIfIncompatible);
    EXPECT_TRUE(plug.dryRun);
    EXPECT_EQ(plug.media, "save");
    EXPECT_TRUE(json);

    EXPECT_EQ(Parse({"plug", "auto", "neogs"}, json, error).slot, "") << "auto: the planner's choice";

    SlotControlRequest set = Parse({"set", "zxbus.1", "ctrlMask=classic"}, json, error);
    EXPECT_EQ(set.verb, "set");
    EXPECT_EQ(set.slot, "zxbus.1");
    EXPECT_EQ(set.options, "ctrlMask=classic");

    SlotControlRequest gs = Parse({"gs", "neogs", "--replace"}, json, error);
    EXPECT_EQ(gs.verb, "gs");
    EXPECT_EQ(gs.card, "neogs");
    EXPECT_TRUE(gs.replaceIfIncompatible);

    EXPECT_EQ(Parse({"matrix", "cards"}, json, error).table, "cards");

    SlotControlRequest bad;
    EXPECT_FALSE(CliSlots::ParseArgs({"plug", "zxbus.1"}, bad, json, error)) << "a plug needs a card";
    EXPECT_FALSE(CliSlots::ParseArgs({"remove"}, bad, json, error));
    EXPECT_FALSE(CliSlots::ParseArgs({"remove", "zxbus.1", "extra"}, bad, json, error));
    EXPECT_FALSE(CliSlots::ParseArgs({"list", "--loud"}, bad, json, error));
    EXPECT_NE(error.find("--loud"), std::string::npos) << error;
}

/// The text of `slots`, `slots catalog` and a refused plug on a real machine
TEST(CliSlots_Test, RepliesAsText)
{
    SoundCardScope everySound;
    std::string error;
    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "", "PENTAGON", LoggerLevel::LogError, &error, [](CONFIG& config) {
            SlotConfig slots;
            ParseSlotsSection({{"ay-socket", "tsfm"}, {"zxbus.1", "gs"}}, slots);
            SlotManager::UseSlots(slots, config);
        });
    ASSERT_NE(emulator, nullptr) << error;
    const std::string id = emulator->GetId();

    bool json = false;
    SlotControlRequest request;
    ASSERT_TRUE(CliSlots::ParseArgs({}, request, json, error));
    request.emulatorId = id;
    std::string text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_NE(text.find("ay-socket = tsfm  fit real, active"), std::string::npos) << text;
    EXPECT_NE(text.find("zxbus.1 = gs [ram=128k rom=1.05]  fit real, active"), std::string::npos) << text;
    EXPECT_NE(text.find("built-in devices:"), std::string::npos) << text;

    ASSERT_TRUE(CliSlots::ParseArgs({"catalog"}, request, json, error));
    request.emulatorId = id;
    text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_NE(text.find("multisound - ZX-MultiSound rev.A2: needs-replace in zxbus.2, fit real"), std::string::npos) << text;
    EXPECT_NE(text.find("dip=ym|saa|gs|sd (set, default ym,saa,gs,sd)"), std::string::npos) << text;

    ASSERT_TRUE(CliSlots::ParseArgs({"plug", "zxbus.next", "neogs"}, request, json, error));
    request.emulatorId = id;
    request.startWhenRunning = false;
    text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_EQ(text.rfind("refused: needs replaceIfIncompatible", 0), 0u) << text;
    EXPECT_NE(text.find("removes zxbus.1 = gs [ram=128k rom=1.05]"), std::string::npos) << text;

    ASSERT_TRUE(CliSlots::ParseArgs({"plug", "zxbus.next", "neogs", "--json"}, request, json, error));
    request.emulatorId = id;
    text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_EQ(text.rfind("{\"ok\":false,\"status\":\"refused\"", 0), 0u) << text;

    emulator.reset();
    EmulatorManager::GetInstance()->RemoveEmulator(id);
    MessageCenter::DisposeDefaultMessageCenter();
}

/// `network set` (owner decision Q11): the settings and the slot flags become the SlotControl network request; a card
/// change prints the plan and the restart, the other settings the in-place line
TEST(CliSlots_Test, NetworkSetIsTheNetworkVerb)
{
    SoundCardScope everySound;
    bool json = false;
    std::string error;
    SlotControlRequest request;
    ASSERT_TRUE(CliSlots::ParseNetworkSet({"card=zxnetusb,zxwifi", "host_access=off", "--replace", "--dry-run", "--media",
                                           "discard"},
                                          request, json, error))
        << error;
    EXPECT_EQ(request.verb, "network");
    EXPECT_EQ(request.settings, (std::vector<std::pair<std::string, std::string>>{{"card", "zxnetusb,zxwifi"},
                                                                                  {"host_access", "off"}}));
    EXPECT_TRUE(request.replaceIfIncompatible);
    EXPECT_TRUE(request.dryRun);
    EXPECT_EQ(request.media, "discard");
    EXPECT_FALSE(CliSlots::ParseNetworkSet({"card"}, request, json, error));
    EXPECT_NE(error.find("key=value"), std::string::npos) << error;
    EXPECT_FALSE(CliSlots::ParseNetworkSet({}, request, json, error));
    EXPECT_FALSE(CliSlots::ParseNetworkSet({"--loud"}, request, json, error));

    std::shared_ptr<Emulator> emulator = EmulatorManager::GetInstance()->CreateEmulatorWithModel(
        "", "PENTAGON", LoggerLevel::LogError, &error, [](CONFIG& config) {
            SlotConfig slots;
            ParseSlotsSection({{"ay-socket", "ay"}}, slots);
            SlotManager::UseSlots(slots, config);
        });
    ASSERT_NE(emulator, nullptr) << error;
    const std::string id = emulator->GetId();

    ASSERT_TRUE(CliSlots::ParseNetworkSet({"card=zxnetusb", "--dry-run"}, request, json, error));
    request.emulatorId = id;
    std::string text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_EQ(text.rfind("dry-run", 0), 0u) << text;
    EXPECT_NE(text.find("zxnetusb"), std::string::npos) << text;

    ASSERT_TRUE(CliSlots::ParseNetworkSet({"hosts=a.test=10.0.2.7"}, request, json, error));
    request.emulatorId = id;
    text = CliSlots::Render(request, SlotControl::Execute(request), json);
    EXPECT_EQ(text.rfind("accepted: applied at the next frame boundary", 0), 0u) << text;

    emulator.reset();
    EmulatorManager::GetInstance()->RemoveEmulator(id);
    MessageCenter::DisposeDefaultMessageCenter();
}
