/// @file controllercontrol_test.cpp
/// @brief ControllerControl (docs/inprogress/2026-10-17-controller-manager/surfaces.md §2-§4): the connector verbs,
/// option checks, `plug auto`, targets and the reply envelope every surface returns.

#include "emulator/io/controllers/controllercontrol.h"

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "common/inifile.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/controllers/controllerconfig.h"
#include "emulator/notifications.h"
#include "emulator/platform.h"
#include "gtest/gtest.h"
#include "stdafx.h"

using namespace controllers;

class ControllerControl_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    ControllerReply Run(const std::string& verb, const std::string& selector = "", const std::string& device = "",
                        std::map<std::string, std::string> options = {})
    {
        return ControllerControl(_context).Execute({verb, selector, device, std::move(options)});
    }
};

TEST_F(ControllerControl_Test, List_TheMachinesConnectors)
{
    const ControllerReply reply = Run("list");
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    const StateNode value = reply.ToValue();
    StateNode connectors = value.members.back().second;
    ASSERT_EQ(value.members.back().first, "connectors");
    ASSERT_EQ(connectors.size(), 3u);
    EXPECT_EQ(connectors.items[0]["id"].s, "kempston");
    EXPECT_EQ(connectors.items[0]["device"].s, "kempston-joystick");
    EXPECT_TRUE(connectors.items[0]["fixed"].b);
    EXPECT_EQ(connectors.items[1]["kind"].s, "keyboard-joystick");
    EXPECT_EQ(connectors.items[1]["device"].kind, StateNode::Kind::Null) << "an empty socket";
    EXPECT_EQ(reply.HttpStatus(), 200);
}

TEST_F(ControllerControl_Test, Requests_FailTheSameWayOnEverySurface)
{
    ControllerReply reply = Run("jiggle");
    EXPECT_EQ(reply.result.error, ControllerError::BadRequest);
    EXPECT_NE(reply.result.message.find("verbs: list"), std::string::npos) << reply.result.message;

    reply = Run("plug", "keyjoy.1", "sinclair-joystick", {{"colour", "red"}});
    EXPECT_EQ(reply.result.error, ControllerError::BadRequest);
    EXPECT_NE(reply.result.message.find("options: port"), std::string::npos) << reply.result.message;

    reply = Run("plug", "keyjoy.1", "sinclair-joystick", {{"port", "3"}});
    EXPECT_EQ(reply.result.error, ControllerError::BadRequest) << "port is 1 or 2";
    reply = Run("plug", "keyjoy.1", "cursor-joystick", {{"port", "2"}});
    EXPECT_EQ(reply.result.error, ControllerError::BadRequest) << "a Cursor joystick has no port option";
    reply = Run("plug", "keyjoy.1", "light-pen");
    EXPECT_EQ(reply.result.error, ControllerError::BadRequest);

    reply = Run("get", "joystick-9");
    EXPECT_EQ(reply.result.error, ControllerError::UnknownConnector);
    EXPECT_EQ(reply.HttpStatus(), 404);
    EXPECT_NE(reply.result.message.find("kempston, keyjoy.1, keyjoy.2"), std::string::npos) << reply.result.message;

    reply = Run("unplug", "kempston");
    EXPECT_EQ(reply.result.error, ControllerError::Fixed);
    EXPECT_EQ(reply.HttpStatus(), 409);
    const StateNode value = reply.ToValue();
    EXPECT_FALSE(value.members[0].second.b) << "ok";
    EXPECT_EQ(value.members[1].first, "error");
    EXPECT_EQ(value.members[1].second.s, "fixed");
}

TEST_F(ControllerControl_Test, PlugAuto_FreeSocketsFirst)
{
    ControllerReply reply = Run("plug", "auto", "sinclair-joystick", {{"port", "2"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.connector, "keyjoy.1");
    EXPECT_FALSE(reply.pending) << "not recording: at once";
    EXPECT_EQ(reply.body["info"]["options"]["port"].i, 2);

    reply = Run("plug", "auto", "keys-joystick", {{"keys", "up:q,down:a,left:o,right:p,fire:m"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(reply.connector, "keyjoy.2");
    EXPECT_EQ(reply.body["info"]["options"]["keys"].s, "right:p,left:o,down:a,up:q,fire:m");

    reply = Run("targets", "", "cursor-joystick");
    ASSERT_TRUE(reply.result.Ok());
    ASSERT_EQ(reply.body["targets"].size(), 2u);
    EXPECT_EQ(reply.body["targets"].items[0]["occupiedBy"].s, "sinclair-joystick") << "both taken: replace the first";
    EXPECT_EQ(reply.body["default"].i, 0);

    reply = Run("options", "keyjoy.1", "", {{"port", "1"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    EXPECT_EQ(_context->pControllerManager->At(1).options.sinclairPort, 1);
}

/// Every plug and unplug posts NC_CONTROLLER_CONNECTOR_CHANGED naming the emulator, the connector, the device and the
/// revision the reply shows; a refused one posts nothing
TEST_F(ControllerControl_Test, PlugAndUnplug_PostConnectorChanged)
{
    struct Seen
    {
        std::string connector;
        std::string device;
        uint64_t revision;
    };
    std::mutex mutex;
    std::vector<Seen> seen;
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();
    const unreal::UUID emulatorId = _context->emulatorId;
    struct Observing
    {
        MessageCenter& mc;
        uint64_t id;
        ~Observing() { mc.RemoveObserverById(NC_CONTROLLER_CONNECTOR_CHANGED, id); }
    } observing{mc, mc.AddObserver(NC_CONTROLLER_CONNECTOR_CHANGED, [&mutex, &seen, emulatorId](int, Message* message) {
                    auto* payload = message ? dynamic_cast<ControllerConnectorPayload*>(message->obj) : nullptr;
                    if (!payload || payload->emulatorId != emulatorId)
                        return;
                    std::lock_guard<std::mutex> lock(mutex);
                    seen.push_back({payload->connector, payload->device, payload->revision});
                })};
    auto count = [&mutex, &seen] {
        std::lock_guard<std::mutex> lock(mutex);
        return seen.size();
    };

    const ControllerReply plugged = Run("plug", "keyjoy.2", "cursor-joystick");
    ASSERT_TRUE(plugged.result.Ok()) << plugged.result.message;
    EXPECT_FALSE(Run("unplug", "kempston").result.Ok());
    const ControllerReply unplugged = Run("unplug", "keyjoy.2");
    ASSERT_TRUE(unplugged.result.Ok()) << unplugged.result.message;

    ASSERT_TRUE(TestWait::For([&count] { return count() >= 2; }));
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(seen.size(), 2u) << "the refused unplug posts nothing";
    EXPECT_EQ(seen[0].connector, "keyjoy.2");
    EXPECT_EQ(seen[0].device, "cursor-joystick");
    EXPECT_EQ(seen[0].revision, plugged.revision);
    EXPECT_EQ(seen[1].device, "none");
    EXPECT_EQ(seen[1].revision, unplugged.revision);
}

TEST_F(ControllerControl_Test, Targets_RefusalIsAnAnswer)
{
    ControllerReply reply = Run("targets", "", "kempston-joystick");
    ASSERT_TRUE(reply.result.Ok()) << "a refusal is not an error";
    ASSERT_EQ(reply.body["targets"].size(), 1u);
    EXPECT_EQ(reply.body["default"].kind, StateNode::Kind::Null);
    EXPECT_NE(reply.body["refusal"].s.find("part of the board"), std::string::npos);

    reply = Run("targets", "", "md6-pad");
    EXPECT_EQ(reply.body["targets"].size(), 0u);
    EXPECT_NE(reply.body["refusal"].s.find("no connector"), std::string::npos);

    reply = Run("plug", "auto", "md6-pad");
    EXPECT_EQ(reply.result.error, ControllerError::Incompatible);
}

TEST_F(ControllerControl_Test, Config_ParsesAndApplies)
{
    IniFile ini;
    ini.LoadData(
        "[INPUT]\nJoystick=SINCLAIR2\n"
        "[CONTROLLERS]\n"
        "keyjoy.2 = keys-joystick, keys=up:q,down:a,left:o,right:p,fire:m\n"
        "kempston = cursor-joystick\n"
        "keyjoy.9 = sinclair-joystick\n"
        "keyjoy.2 = keys-joystick, keys=up:w,down:s,left:a,right:d,fire:space\n");
    std::vector<std::string> report;
    const std::vector<ControllerSetEntry> entries = ControllerConfig::FromIni(ini, &report);
    EXPECT_TRUE(report.empty());
    ASSERT_EQ(entries.size(), 4u) << "the second keyjoy.2 wins, the legacy key adds keyjoy.1";
    auto entry = [&entries](const char* connector) {
        for (const ControllerSetEntry& e : entries)
            if (e.connector == connector)
                return e;
        ADD_FAILURE() << connector;
        return ControllerSetEntry{};
    };
    EXPECT_EQ(entry("keyjoy.2").options["keys"], "up:w,down:s,left:a,right:d,fire:space")
        << "the last one, its commas kept";
    EXPECT_EQ(entries.back().connector, "keyjoy.1") << "the legacy default comes last";
    EXPECT_EQ(entry("keyjoy.1").device, "sinclair-joystick");
    EXPECT_EQ(entry("keyjoy.1").options["port"], "2");

    const std::vector<std::string> problems = ControllerConfig::Apply(_context, entries);
    ASSERT_EQ(problems.size(), 2u);
    EXPECT_NE(problems[0].find("[CONTROLLERS] kempston: fixed"), std::string::npos) << problems[0];
    EXPECT_NE(problems[1].find("[CONTROLLERS] keyjoy.9: unknown-connector"), std::string::npos) << problems[1];
    const ControllerManager& manager = *_context->pControllerManager;
    EXPECT_EQ(manager.At(1).device, RetroDeviceKind::SinclairJoystick);
    EXPECT_EQ(manager.At(1).options.sinclairPort, 2);
    EXPECT_EQ(manager.At(2).device, RetroDeviceKind::KeysJoystick);
    EXPECT_EQ(ControllerManager::KeysSpec(manager.At(2).options.keys), "right:d,left:a,down:s,up:w,fire:space");

    ini.LoadData("[INPUT]\nJoystick=QAOP\n[CONTROLLERS]\nkeyjoy.1 = none\n");
    const std::vector<ControllerSetEntry> none = ControllerConfig::FromIni(ini);
    ASSERT_EQ(none.size(), 1u) << "[CONTROLLERS] names keyjoy.1: the legacy key is not read";
    EXPECT_TRUE(ControllerConfig::Apply(_context, none).empty());
    EXPECT_EQ(manager.At(1).device, RetroDeviceKind::None);
}

TEST_F(ControllerControl_Test, Input_PressReleaseSetOnAConnector)
{
    ControllerReply reply = Run("input", "kempston", "", {{"press", "up+fire"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    StateNode held = reply.ToValue()["held"];
    ASSERT_EQ(held.size(), 2u);
    EXPECT_EQ(held.items[0].s, "up");
    EXPECT_EQ(held.items[1].s, "fire");

    reply = Run("input", "kempston", "", {{"set", "left,b5"}});
    ASSERT_TRUE(reply.result.Ok()) << reply.result.message;
    held = reply.ToValue()["held"];
    ASSERT_EQ(held.size(), 2u) << "set replaces; b5 is fire2";
    EXPECT_EQ(held.items[1].s, "fire2");
    reply = Run("input", "kempston", "", {{"set", "none"}});
    EXPECT_EQ(reply.ToValue()["held"].size(), 0u);

    EXPECT_EQ(Run("input", "keyjoy.1", "", {{"press", "up"}}).result.error, ControllerError::BadRequest) << "empty socket";
    reply = Run("input", "kempston", "", {{"press", "jump"}});
    EXPECT_NE(reply.result.message.find("unknown control 'jump'"), std::string::npos) << reply.result.message;
    EXPECT_EQ(Run("input", "kempston").result.error, ControllerError::BadRequest) << "nothing to do";
}
