/// @file profilestore_test.cpp
/// @brief Controller profiles (mapping-and-profiles.md §6-§8): the JSON reader, schema 1 validation with the reason,
/// the built-in defaults, the field-by-field merge global → family → device → game, the directories and a save.

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "_helpers/testpathhelper.h"
#include "profiles/json.h"
#include "profiles/profilestore.h"
#include "router/inputrouter.h"

using namespace controllers;

namespace
{
const Binding* Find(const ResolvedProfiles& resolved, const std::string& scope, const std::string& layer,
                    const std::string& source)
{
    for (const auto& [name, bindings] : resolved.scopes)
        if (name == scope)
            for (const Binding& binding : bindings)
                if (binding.layer == layer && binding.sources.size() == 1 && binding.sources[0] == source)
                    return &binding;
    return nullptr;
}

std::string Play(const std::string& id, const std::string& extra, const std::string& bindings)
{
    return R"({"schema": 1, "kind": "play", "id": ")" + id + "\"" + extra + R"(, "layers": {"play": {)" + bindings +
           "}}}";
}
}  // namespace

TEST(ProfileStore_Test, Json_ParsesAndReportsThePlace)
{
    JsonValue value;
    std::string error;
    ASSERT_TRUE(ParseJson(R"({"a": [1, 2.5, true, null], "b": "é😀", "c": {}})", value, error))
        << error;
    EXPECT_EQ(value.GetMembers()[0].first, "a") << "members keep their order";
    EXPECT_EQ(value.Find("a")->Items()[1].AsNumber(), 2.5);
    EXPECT_EQ(value.Find("b")->AsString(), "\xC3\xA9\xF0\x9F\x98\x80");
    JsonValue again;
    ASSERT_TRUE(ParseJson(value.Dump(2), again, error)) << error;
    EXPECT_EQ(again.Dump(), value.Dump());

    EXPECT_FALSE(ParseJson("{\n  \"a\": 1,\n  \"a\": 2\n}", value, error));
    EXPECT_NE(error.find("line 3"), std::string::npos) << error;
    EXPECT_FALSE(ParseJson("[1,]", value, error));
    EXPECT_FALSE(ParseJson("{} x", value, error));
}

TEST(ProfileStore_Test, BuiltinsParseAndInvalidProfilesSayWhy)
{
    for (const char* text : BuiltinProfileTexts())
    {
        Profile profile;
        std::string error;
        EXPECT_TRUE(ParseProfile(text, profile, error)) << error << "\n" << text;
    }
    Profile profile;
    std::string error;
    EXPECT_FALSE(ParseProfile(Play("x", "", R"("south": {"retro": "kempston:fire", "turob": "classic"})"), profile,
                              error));
    EXPECT_NE(error.find("turob"), std::string::npos) << error;
    EXPECT_NE(error.find("turbo"), std::string::npos) << "lists the candidates: " << error;
    EXPECT_FALSE(ParseProfile(Play("x", "", R"("southh": {"retro": "kempston:fire"})"), profile, error));
    EXPECT_NE(error.find("layers.play.southh"), std::string::npos) << error;
    EXPECT_FALSE(ParseProfile(R"({"schema": 1, "kind": "debug", "id": "x", "match": {"file_sha256": ["ab"]}})",
                              profile, error));
    EXPECT_NE(error.find("no game level"), std::string::npos) << error;
    EXPECT_FALSE(ParseProfile(R"({"schema": 2, "kind": "play", "id": "x"})", profile, error));

    ASSERT_TRUE(ParseProfile(Play("x", "", R"("view+menu": {"action": "osd.quick_menu"})"), profile, error)) << error;
}

TEST(ProfileStore_Test, Merge_FamilyDeviceAndGameFieldByField)
{
    ProfileStore store;
    std::string error;
    ASSERT_TRUE(store.AddText(Play("user/ps-north", R"(, "applies_to": {"families": ["playstation"]})",
                                   R"("north": {"key": "ENTER"})"),
                              ProfileScope::User, error))
        << error;
    ASSERT_TRUE(store.AddText(Play("user/xbox-west", R"(, "applies_to": {"families": ["xbox"]})",
                                   R"("west": {"retro": "auto:fire2"}, "south": {"label": "Jump"})"),
                              ProfileScope::User, error))
        << error;
    ASSERT_TRUE(store.AddText(Play("user/game", R"(, "match": {"file_sha256": ["ab12"]})",
                                   R"("west": {"key": "SPACE"})"),
                              ProfileScope::User, error))
        << error;

    ProfileContext context;
    context.machine = "pentagon";
    context.devices = {{"usb:045e:0b00:1", "Elite", "xbox-elite-2", "xbox", "fake"},
                       {"kbd:system", "Keyboard", "keyboard", "keyboard", "fake"},
                       {"usb:054c:0ce6:1", "DualSense", "dualsense", "playstation", "fake"}};
    ResolvedProfiles resolved = store.Resolve(context);
    EXPECT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();

    const Binding* west = Find(resolved, "family:xbox-elite-2", "", "west");
    ASSERT_NE(west, nullptr) << "the xbox profile applies to the Elite family";
    EXPECT_EQ(west->target.retro.mask, kFire2);
    const Binding* south = Find(resolved, "family:xbox-elite-2", "", "south");
    ASSERT_NE(south, nullptr);
    EXPECT_EQ(south->target.retro.mask, kFire) << "a field-only change keeps the target below";
    EXPECT_EQ(south->options.label, "Jump");
    EXPECT_NE(Find(resolved, "family:xbox-elite-2", "", "paddle.p4"), nullptr) << "the Elite's layer key";
    EXPECT_EQ(Find(resolved, "", "", "paddle.p4"), nullptr);
    EXPECT_NE(Find(resolved, "family:keyboard", "", "key.kp8"), nullptr) << "the numpad joystick";
    EXPECT_EQ(Find(resolved, "", "", "west")->target.retro.mask, kFire3) << "other pads keep the default";
    ASSERT_NE(Find(resolved, "family:dualsense", "", "north"), nullptr) << "a profile may name the layout";
    EXPECT_EQ(Find(resolved, "family:dualsense", "", "north")->target.kind, TargetKind::Keys);

    context.fileSha256 = {"AB12"};
    resolved = store.Resolve(context);
    west = Find(resolved, "family:xbox-elite-2", "", "west");
    ASSERT_NE(west, nullptr);
    EXPECT_EQ(west->target.kind, TargetKind::Keys) << "the game ranks above the family, and its target replaces";
    EXPECT_EQ(Find(resolved, "", "", "west")->target.kind, TargetKind::Keys);
    EXPECT_EQ(resolved.applied.back(), "user/game");

    InputRouter router;
    store.Apply(router, context);
    store.Apply(router, {});
}

TEST(ProfileStore_Test, Next_MegaDrivePadByDefault)
{
    ProfileStore store;
    ProfileContext context;
    context.machine = "next";
    context.devices = {{"bt:045e:0b22:1", "Elite", "xbox-elite-2", "xbox", "fake"}};
    const ResolvedProfiles resolved = store.Resolve(context);
    const std::pair<const char*, uint16_t> expected[] = {{"south", kFire},  {"east", kFire2}, {"west", kFire3},
                                                         {"menu", kFire4},  {"north", 0x0100}, {"rb", 0x0200},
                                                         {"lb", 0x0400},    {"view", 0x0800}};
    for (const auto& [control, mask] : expected)
    {
        const Binding* binding = Find(resolved, "", "", control);
        ASSERT_NE(binding, nullptr) << control;
        EXPECT_EQ(binding->target.kind, TargetKind::Retro) << control;
        EXPECT_EQ(binding->target.retro.mask, mask) << control;
    }

    context.machine = "pentagon";
    EXPECT_EQ(Find(store.Resolve(context), "", "", "north")->target.kind, TargetKind::Keys) << "SPACE elsewhere";

    // A NEX started from NextZXOS: no file the emulator loaded, the game profile chosen by hand
    context.machine = "next";
    context.manual = {"builtin/game/vradarks-revenge"};
    const ResolvedProfiles game = store.Resolve(context);
    EXPECT_TRUE(game.warnings.empty()) << game.warnings.front();
    EXPECT_EQ(game.applied.back(), "builtin/game/vradarks-revenge");
    ASSERT_NE(Find(game, "", "", "dpad.up"), nullptr);
    EXPECT_EQ(Find(game, "", "", "dpad.up")->target.kind, TargetKind::Keys) << "W";
    EXPECT_EQ(Find(game, "", "", "south")->target.kind, TargetKind::Keys) << "Enter fires";
    EXPECT_EQ(Find(game, "", "", "rt.click")->target.kind, TargetKind::Mouse);
    EXPECT_EQ(Find(game, "", "", "rstick.x")->target.kind, TargetKind::Mouse) << "the right stick aims";
}

TEST(ProfileStore_Test, Directories_WrongKindRefusedAndSaveRoundTrips)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("profiles");
    fs::create_directories(root / "debug");
    std::ofstream(root / "debug" / "wrong.json") << Play("user/wrong", "", R"("south": {"retro": "auto:fire"})");

    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(Play("user/sabre-wulf", R"(, "label": "Sabre Wulf", "match": {"file_sha256": ["3b2a"]},
        "connectors": {"kempston": {"device": "kempston-joystick"}}, "options": {"deadzone": 0.2})",
                                  R"("south": {"retro": "kempston:fire", "turbo": "classic"})"),
                             profile, error))
        << error;
    ASSERT_TRUE(ProfileStore::Save(profile, root.string(), error)) << error;

    ProfileStore store;
    std::vector<std::string> errors;
    EXPECT_EQ(store.LoadDirectory(root.string(), ProfileScope::User, errors), 1u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("a play profile in debug/"), std::string::npos) << errors[0];
    const Profile* loaded = store.Find("user/sabre-wulf");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(ProfileStore::Dump(*loaded), ProfileStore::Dump(profile));
    EXPECT_EQ(loaded->connectors.at("kempston").device, "kempston-joystick");
    fs::remove_all(root);
}
