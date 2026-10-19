/// @file hostinputcontrol_test.cpp
/// @brief The host verbs every surface calls (surfaces.md §2): profiles listed with the shipped games of the index,
/// one profile as YAML, save / delete in the user directory, the manual game profile, the effective bindings with
/// their source, bind / unbind in a user profile, the haptics of the devices and their test effects, the errors, and
/// Process without an owner.

#include <gtest/gtest.h>

#include <filesystem>

#include "_helpers/testpathhelper.h"
#include "fake/fakebackend.h"
#include "hostinputcontrol.h"
#include "hostinputservice.h"

using namespace controllers;

namespace
{
/// The Qt client's HostControllers, without Qt: a store, the last context, a user directory
class FakeOwner : public IHostInputOwner
{
public:
    explicit FakeOwner(const std::string& userDir) : _userDir(userDir)
    {
        std::vector<std::string> errors;
        _store.OpenGames((TestPathHelper::FindProjectRoot() / "data" / "controllers" / "profiles" / "games").string(),
                         errors);
        _context.machine = "next";
        _context.devices = {{"bt:054c:0ce6:1", "DualSense", "dualsense", "playstation", "fake"}};
        Reapply();
    }
    ProfileStore& Profiles() override
    {
        return _store;
    }
    ProfileContext Context() const override
    {
        return _context;
    }
    const ResolvedProfiles& Resolved() const override
    {
        return _resolved;
    }
    std::string UserProfileDir() const override
    {
        return _userDir;
    }
    std::string ManualProfile() const override
    {
        return _manual;
    }
    void SetManualProfile(const std::string& id) override
    {
        _manual = id;
        Reapply();
    }
    void Reapply() override
    {
        _context.manual = _manual.empty() ? std::vector<std::string>{} : std::vector<std::string>{_manual};
        std::vector<std::string> errors;
        _store.LoadGamesFor(_context, errors);
        _resolved = _store.Resolve(_context);
        reapplied++;
    }
    HostInputService* Input() override
    {
        return input;
    }
    void SetDevices(std::vector<HostDeviceInfo> devices)
    {
        _context.devices = std::move(devices);
    }
    int reapplied = 0;
    HostInputService* input = nullptr;

private:
    ProfileStore _store;
    ProfileContext _context;
    ResolvedProfiles _resolved;
    std::string _userDir;
    std::string _manual;
};

const JsonValue* FindItem(const JsonValue& list, const std::string& field, const std::string& value)
{
    for (const JsonValue& item : list.Items())
        if (const JsonValue* member = item.Find(field); member != nullptr && member->AsString() == value)
            return &item;
    return nullptr;
}
}  // namespace

TEST(HostInputControl_Test, ProfilesApplyAndBindings)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("host-profiles");
    FakeOwner owner(root.string());
    HostInputControl control(owner);

    HostReply reply = control.Execute({"profiles", {{"set", "game"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue* vradark = FindItem(*reply.body.Find("profiles"), "id", "builtin/game/vradarks-revenge");
    ASSERT_NE(vradark, nullptr) << "the index lists the shipped games";
    EXPECT_FALSE(vradark->Find("loaded")->AsBool()) << "not parsed before a match";

    reply = control.Execute({"profile", {{"id", "builtin/game/vradarks-revenge"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_NE(reply.body.Find("yaml")->AsString().find("extends: [scheme/wasd, scheme/kempston-mouse]"),
              std::string::npos)
        << reply.body.Find("yaml")->AsString();

    reply = control.Execute({"apply", {{"id", "builtin/game/vradarks-revenge"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(owner.ManualProfile(), "builtin/game/vradarks-revenge");
    reply = control.Execute({"bindings", {{"layer", "play"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue* south = FindItem(*reply.body.Find("bindings"), "control", "south");
    ASSERT_NE(south, nullptr);
    EXPECT_EQ(south->Find("profile")->AsString(), "builtin/game/vradarks-revenge");
    EXPECT_EQ(south->Find("spec")->Find("key")->AsString(), "ENTER");
    const JsonValue* aim = FindItem(*reply.body.Find("bindings"), "control", "lt.click");
    ASSERT_NE(aim, nullptr);
    EXPECT_EQ(aim->Find("profile")->AsString(), "scheme/kempston-mouse") << "a scheme's binding names the scheme";

    // bind copies the shipped game into a user profile of the same id, which overrides it; delete brings it back
    reply = control.Execute(
        {"bind", {{"control", "south"}, {"spec", "{ key: SPACE }"}, {"profile", "builtin/game/vradarks-revenge"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(fs::is_regular_file(root / "games" / "vradarks-revenge.yaml"));
    reply = control.Execute({"bindings", {}});
    EXPECT_EQ(FindItem(*reply.body.Find("bindings"), "control", "south")->Find("spec")->Find("key")->AsString(),
              "SPACE");
    reply = control.Execute({"delete", {{"id", "builtin/game/vradarks-revenge"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_FALSE(fs::exists(root / "games" / "vradarks-revenge.yaml"));
    reply = control.Execute({"bindings", {}});
    EXPECT_EQ(FindItem(*reply.body.Find("bindings"), "control", "south")->Find("spec")->Find("key")->AsString(),
              "ENTER")
        << "the shipped game came back from the index";

    reply = control.Execute({"apply", {{"id", "none"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(owner.ManualProfile().empty());

    // A family's user profile (below a game, so with none applied); the device's bindings follow it
    reply = control.Execute({"bind", {{"control", "north"}, {"spec", R"({"key": "Q"})"}, {"family", "dualsense"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("profile")->AsString(), "user/dualsense");
    reply = control.Execute({"bindings", {{"device", "bt:054c:0ce6:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("scope")->AsString(), "family:dualsense");
    EXPECT_EQ(FindItem(*reply.body.Find("bindings"), "control", "north")->Find("profile")->AsString(),
              "user/dualsense");
    reply = control.Execute({"unbind", {{"control", "north"}, {"family", "dualsense"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;

    // The pad works as Sinclair 1: the family's profile extends the scheme, its keys come from it; none undoes it
    reply = control.Execute({"scheme", {{"id", "scheme/sinclair-1"}, {"family", "dualsense"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(owner.Profiles().Find("user/dualsense")->extends, std::vector<std::string>{"scheme/sinclair-1"});
    reply = control.Execute({"bindings", {{"device", "bt:054c:0ce6:1"}}});
    const JsonValue* left = FindItem(*reply.body.Find("bindings"), "control", "dpad.left");
    ASSERT_NE(left, nullptr);
    EXPECT_EQ(left->Find("profile")->AsString(), "scheme/sinclair-1");
    EXPECT_EQ(control.Execute({"scheme", {{"id", "builtin/play/pad-default"}, {"family", "dualsense"}}}).error,
              "bad-request")
        << "only a scheme";
    reply = control.Execute({"scheme", {{"id", "none"}, {"family", "dualsense"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(owner.Profiles().Find("user/dualsense")->extends.empty());
    EXPECT_EQ(HostInputControl::UserProfileFor("bt:054c:0ce6:1", "dualsense"), "user/device-bt_054c_0ce6_1");

    fs::remove_all(root);
}

TEST(HostInputControl_Test, ErrorsSayWhy)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("host-errors");
    FakeOwner owner(root.string());
    HostInputControl control(owner);

    HostReply reply = control.Execute({"profile", {{"id", "nope"}}});
    EXPECT_EQ(reply.error, "unknown-profile");
    EXPECT_EQ(reply.HttpStatus(), 404);
    reply = control.Execute({"delete", {{"id", "builtin/play/pad-default"}}});
    EXPECT_EQ(reply.error, "read-only");
    reply = control.Execute({"bind", {{"control", "southh"}, {"spec", "{ key: ENTER }"}}});
    EXPECT_EQ(reply.error, "invalid-binding");
    EXPECT_NE(reply.message.find("southh"), std::string::npos) << reply.message;
    reply = control.Execute({"profiles", {{"sett", "game"}}});
    EXPECT_EQ(reply.error, "bad-request");
    EXPECT_NE(reply.message.find("set, machine"), std::string::npos) << "lists the options: " << reply.message;
    reply = control.Execute({"apply", {{"id", "scheme/wasd"}}});
    EXPECT_EQ(reply.error, "bad-request") << "a scheme is not a game profile";
    reply = control.Execute({"frob", {}});
    EXPECT_EQ(reply.error, "bad-request");
    reply = control.Execute({"save", {{"text", "schema: 1\nkind: play\nid: user/mine\nlayers:\n  play:\n"
                                                "    west: { key: \"5\" }\n"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_TRUE(fs::is_regular_file(root / "play" / "mine.yaml"));

    HostInputControl::SetOwner(nullptr);
    EXPECT_EQ(HostInputControl::Process({"context", {}}).error, "unavailable");
    HostInputControl::SetOwner(&owner);
    EXPECT_TRUE(HostInputControl::Process({"context", {}}).Ok());
    HostInputControl::SetOwner(nullptr);
    fs::remove_all(root);
}

TEST(HostInputControl_Test, HapticsListsTheEffectsAndRumblePlaysThem)
{
    FakeOwner owner(TestPathHelper::GetUniqueTestScratchPath("host-haptics"));
    HostInputControl control(owner);
    const HostDeviceInfo pad{"fake:1", "Pad", "xbox", "xbox", "fake"};
    const HostDeviceInfo keyboard{"fake:kbd", "Keyboard", "keyboard", "keyboard", "fake"};
    owner.SetDevices({pad, keyboard});

    HostReply reply = control.Execute({"haptics", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue* none = FindItem(*reply.body.Find("devices"), "id", "fake:1");
    ASSERT_NE(none, nullptr);
    EXPECT_FALSE(none->Find("available")->AsBool()) << "no host output without a service";
    EXPECT_EQ(control.Execute({"rumble", {{"device", "fake:1"}}}).error, "unavailable");

    HostInputService service;
    auto owned = std::make_unique<FakeBackend>();
    FakeBackend* fake = owned.get();
    std::string reason;
    ASSERT_TRUE(service.AddBackend(std::move(owned), reason)) << reason;
    ASSERT_TRUE(fake->Connect(pad));
    ASSERT_TRUE(fake->Connect(keyboard));
    owner.input = &service;

    reply = control.Execute({"haptics", {}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    const JsonValue* motors = FindItem(*reply.body.Find("devices"), "id", "fake:1");
    ASSERT_NE(motors, nullptr);
    EXPECT_TRUE(motors->Find("available")->AsBool()) << motors->Find("reason")->AsString();
    EXPECT_EQ(motors->Find("actuators")->Dump(), R"(["low","high"])") << "a backend's default: the two motors";
    ASSERT_NE(FindItem(*motors->Find("effects"), "id", "clicks"), nullptr);
    EXPECT_EQ(FindItem(*motors->Find("effects"), "id", "left-trigger"), nullptr) << "no trigger motors";
    const JsonValue* keys = FindItem(*reply.body.Find("devices"), "id", "fake:kbd");
    ASSERT_NE(keys, nullptr);
    EXPECT_FALSE(keys->Find("available")->AsBool());
    EXPECT_EQ(keys->Find("reason")->AsString(), "Keyboards and mice have no motors");
    EXPECT_EQ(keys->Find("effects")->Items().size(), 0u);

    reply = control.Execute({"haptics", {{"device", "fake:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("devices")->Items().size(), 1u) << "device= keeps only that one";

    reply = control.Execute({"rumble", {{"device", "fake:1"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(reply.body.Find("effect")->AsString(), "identify") << "identify by default";
    EXPECT_GT(fake->Rumble("fake:1").low, 0.0f) << "identify pulses both motors";
    EXPECT_GT(fake->Rumble("fake:1").high, 0.0f);
    service.Haptics().StopAll();

    reply = control.Execute({"rumble", {{"device", "fake:1"}, {"effect", "high"}}});
    ASSERT_TRUE(reply.Ok()) << reply.message;
    EXPECT_EQ(fake->Rumble("fake:1").low, 0.0f);
    EXPECT_GT(fake->Rumble("fake:1").high, 0.0f) << "the high-frequency motor alone";

    reply = control.Execute({"rumble", {{"device", "fake:1"}, {"effect", "left-trigger"}}});
    EXPECT_EQ(reply.error, "bad-request");
    EXPECT_NE(reply.message.find("it has: identify, low, high, clicks, swell"), std::string::npos) << reply.message;
    reply = control.Execute({"rumble", {{"device", "fake:kbd"}}});
    EXPECT_EQ(reply.error, "unavailable");
    EXPECT_NE(reply.message.find("no motors"), std::string::npos) << reply.message;
    EXPECT_EQ(control.Execute({"rumble", {}}).error, "bad-request") << "rumble needs a device";
    EXPECT_EQ(control.Execute({"rumble", {{"device", "fake:9"}}}).error, "unknown-device");
    EXPECT_EQ(control.Execute({"haptics", {{"device", "fake:9"}}}).error, "unknown-device");
    EXPECT_EQ(control.Execute({"haptics", {{"effect", "low"}}}).error, "bad-request") << "effect is rumble's";
    service.Stop();
}
