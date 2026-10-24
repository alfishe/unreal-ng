/// @file profilestore_test.cpp
/// @brief Controller profiles (mapping-and-profiles.md §6-§8): the JSON and YAML readers, schema 1 validation with the
/// reason, the built-in defaults and every shipped game file, the games index, schemes (`extends`), the field-by-field
/// merge global → family → device → game, the directories and a save.

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>

#include "_helpers/testpathhelper.h"
#include "profiles/json.h"
#include "profiles/profilestore.h"
#include "profiles/yaml.h"
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

std::string GamesDir()
{
    return (TestPathHelper::FindProjectRoot() / "data" / "controllers" / "profiles" / "games").string();
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

TEST(ProfileStore_Test, Yaml_ScalarsKeysAndPlace)
{
    JsonValue value;
    std::string error;
    ASSERT_TRUE(ParseYaml(R"(# a comment
a: [1, 2.5, 0x10, true, ~, "7", '']
b: plain text   # a trailing comment
"dpad.*": { on: 2, off: 3 }
)",
                          value, error))
        << error;
    ASSERT_TRUE(value.IsObject());
    EXPECT_EQ(value.GetMembers()[0].first, "a") << "members keep their order";
    const auto& items = value.Find("a")->Items();
    ASSERT_EQ(items.size(), 7u);
    EXPECT_EQ(items[1].AsNumber(), 2.5);
    EXPECT_EQ(items[2].AsNumber(), 16);
    EXPECT_TRUE(items[3].IsBool());
    EXPECT_TRUE(items[4].IsNull());
    EXPECT_TRUE(items[5].IsString()) << "a quoted digit stays a string";
    EXPECT_TRUE(items[6].IsString());
    EXPECT_EQ(value.Find("b")->AsString(), "plain text");
    EXPECT_EQ(value.Find("dpad.*")->Find("on")->AsNumber(), 2) << "YAML 1.2: `on` is a key, not a bool";

    JsonValue again;
    ASSERT_TRUE(ParseYaml(DumpYaml(value), again, error)) << error << "\n" << DumpYaml(value);
    EXPECT_EQ(again.Dump(), value.Dump()) << DumpYaml(value);

    EXPECT_FALSE(ParseYaml("a: 1\nb: 2\na: 3\n", value, error));
    EXPECT_NE(error.find("duplicate key 'a'"), std::string::npos) << error;
    EXPECT_FALSE(ParseYaml("a: [1, 2\nb: 3\n", value, error));
    EXPECT_NE(error.find("line "), std::string::npos) << error;

    const std::string bundle = "# header\n---\nid: a\n---\n# nothing\n--- \nid: b\n";
    const auto documents = SplitYamlDocuments(bundle);
    ASSERT_EQ(documents.size(), 2u);
    ASSERT_TRUE(ParseYaml(bundle.substr(documents[1].first, documents[1].second), value, error)) << error;
    EXPECT_EQ(value.Find("id")->AsString(), "b");
}

TEST(ProfileStore_Test, BuiltinsParseAndInvalidProfilesSayWhy)
{
    for (const BuiltinProfileText& builtin : BuiltinProfileTexts())
    {
        Profile profile;
        std::string error;
        EXPECT_TRUE(ParseProfile(builtin.text, profile, error)) << builtin.name << ": " << error;
        const char* directory = profile.kind == ProfileKind::Play ? "play/" : "debug/";
        EXPECT_EQ(std::string(builtin.name).rfind(profile.scheme ? "schemes/" : directory, 0), 0u)
            << builtin.name << ": in its kind's directory";
    }
    // §7.4: every shipped game file and bundle entry, and the committed index is what the files say
    std::vector<GameEntry> entries;
    std::vector<std::string> errors;
    ASSERT_TRUE(GameLibrary::Build(GamesDir(), entries, errors));
    for (const std::string& e : errors)
        ADD_FAILURE() << e;
    EXPECT_FALSE(entries.empty());
    std::ifstream in(std::filesystem::path(GamesDir()) / "index.yaml", std::ios::binary);
    const std::string indexText((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<GameEntry> indexed;
    std::string why;
    ASSERT_TRUE(GameLibrary::ParseIndex(indexText, indexed, why)) << why;
    EXPECT_EQ(GameLibrary::DumpIndex(indexed), GameLibrary::DumpIndex(entries))
        << "games/index.yaml is stale: run tools/controllers/profile-index.py";
    {
        ProfileStore store;
        ASSERT_TRUE(store.OpenGames(GamesDir(), errors));
        for (const GameEntry& entry : entries)
        {
            ProfileContext context;
            context.manual = {entry.id};
            errors.clear();
            EXPECT_EQ(store.LoadGamesFor(context, errors), 1u) << entry.id;
            const ResolvedProfiles resolved = store.Resolve(context);
            for (const std::string& w : resolved.warnings)
                ADD_FAILURE() << entry.id << ": " << w;
        }
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
    EXPECT_FALSE(ParseProfile("schema: 1\nkind: scheme\nid: s\nmatch: { machines: [next] }\n", profile, error));
    EXPECT_NE(error.find("a scheme has no 'match'"), std::string::npos) << error;
    EXPECT_FALSE(ParseProfile("schema: 1\nkind: play\nid: x\nlayers:\n  play:\n    southh: { key: \"1\" }\n",
                              profile, error));
    EXPECT_NE(error.find("layers.play.southh"), std::string::npos) << "YAML says where too: " << error;

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

    // A NEX started from NextZXOS: no file the emulator loaded, the game profile chosen by hand; it lives on disk
    // behind the index and extends scheme/wasd and scheme/kempston-mouse
    context.machine = "next";
    context.manual = {"builtin/game/vradarks-revenge"};
    std::vector<std::string> errors;
    ASSERT_TRUE(store.OpenGames(GamesDir(), errors));
    EXPECT_EQ(store.Find("builtin/game/vradarks-revenge"), nullptr) << "not parsed before a match";
    EXPECT_EQ(store.LoadGamesFor(context, errors), 1u) << (errors.empty() ? "" : errors.front());
    const ResolvedProfiles game = store.Resolve(context);
    EXPECT_TRUE(game.warnings.empty()) << game.warnings.front();
    EXPECT_EQ(game.applied.back(), "builtin/game/vradarks-revenge");
    ASSERT_NE(Find(game, "", "", "dpad.up"), nullptr);
    EXPECT_EQ(Find(game, "", "", "dpad.up")->target.kind, TargetKind::Keys) << "W";
    EXPECT_EQ(Find(game, "", "", "south")->target.kind, TargetKind::Keys) << "Enter fires";
    EXPECT_EQ(Find(game, "", "", "rt.click")->target.kind, TargetKind::Mouse);
    EXPECT_EQ(Find(game, "", "", "rstick.x")->target.kind, TargetKind::Mouse) << "the right stick aims";
    EXPECT_EQ(Find(game, "", "", "lt.click")->target.kind, TargetKind::Mouse) << "from scheme/kempston-mouse";
    const auto at = [&game](const std::string& id) {
        return std::find(game.applied.begin(), game.applied.end(), id) - game.applied.begin();
    };
    EXPECT_LT(at("scheme/wasd"), at("builtin/game/vradarks-revenge")) << "a scheme merges below its game";
    EXPECT_LT(at("scheme/kempston-mouse"), at("builtin/game/vradarks-revenge"));

    // The program's hash finds it too, with no manual choice
    ProfileStore byHash;
    ASSERT_TRUE(byHash.OpenGames(GamesDir(), errors));
    context.manual.clear();
    context.fileSha256 = {"32AD0B5FA7CBB749841ECF26BDD6FCE7563D8FFBCC736C15689B1FA4ABCBCD9E"};
    EXPECT_EQ(byHash.LoadGamesFor(context, errors), 1u) << "keys compare in any case";
    EXPECT_EQ(byHash.Resolve(context).applied.back(), "builtin/game/vradarks-revenge");
}

TEST(ProfileStore_Test, Schemes_ExtendsMergesBelowAndCyclesWarn)
{
    ProfileStore store;
    std::string error;
    ASSERT_TRUE(store.AddText("schema: 1\nkind: scheme\nid: scheme/a\nextends: scheme/b\nlayers:\n  play:\n"
                              "    north: { key: Q }\n    west: { key: W }\n",
                              ProfileScope::User, error))
        << error;
    ASSERT_TRUE(store.AddText("schema: 1\nkind: scheme\nid: scheme/b\nextends: [scheme/a]\nlayers:\n  play:\n"
                              "    east: { key: E }\n",
                              ProfileScope::User, error))
        << error;
    ASSERT_TRUE(store.AddText("schema: 1\nkind: play\nid: user/game\nextends: [scheme/a, scheme/missing]\n"
                              "match: { file_sha256: [\"ab\"] }\nlayers:\n  play:\n    west: { key: X }\n",
                              ProfileScope::User, error))
        << error;
    ProfileContext context;
    EXPECT_EQ(Find(store.Resolve(context), "", "", "east")->target.kind, TargetKind::Retro)
        << "a scheme is not selected by itself: the pad default's fire2";
    context.fileSha256 = {"ab"};
    const ResolvedProfiles resolved = store.Resolve(context);
    ASSERT_NE(Find(resolved, "", "", "north"), nullptr);
    EXPECT_EQ(Find(resolved, "", "", "north")->target.kind, TargetKind::Keys);
    EXPECT_EQ(Find(resolved, "", "", "east")->target.kind, TargetKind::Keys) << "a scheme's scheme merges too";
    ASSERT_EQ(resolved.warnings.size(), 2u);
    EXPECT_NE(resolved.warnings[0].find("a cycle"), std::string::npos) << resolved.warnings[0];
    EXPECT_NE(resolved.warnings[1].find("scheme/missing"), std::string::npos) << resolved.warnings[1];
    const auto& applied = resolved.applied;
    EXPECT_EQ(applied.back(), "user/game");
    EXPECT_EQ(applied[applied.size() - 2], "scheme/a");
}

/// The keyboard's debug profile is the debugger's shortcuts (debug-and-ttd.md §4.3), the Dial's its time travel remote
/// (§4.4): selected without the debug layer; the window keyboard, a debugger remote, takes the bound combos only
TEST(ProfileStore_Test, KeyboardShortcuts_WithoutTheDebugLayer)
{
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.debugLayer = false;
    context.devices = {{"kbd:system", "Keyboard", "keyboard", "keyboard", "qt"},
                       {"usb:045e:0b00:1", "Elite", "xbox-elite-2", "xbox", "fake"},
                       {"bt:045e:091b:1", "Surface Dial", "surface-dial", "generic", "fake"}};
    const ResolvedProfiles resolved = store.Resolve(context);
    EXPECT_TRUE(resolved.warnings.empty()) << resolved.warnings.front();
    const Binding* stepOver = Find(resolved, "family:keyboard", "debug", "key.f10");
    ASSERT_NE(stepOver, nullptr) << "the shortcuts need no debug layer";
    EXPECT_EQ(stepOver->target.name, "run.step_over");
    EXPECT_NE(Find(resolved, "family:keyboard", "", "key.kp8"), nullptr) << "the numpad joystick stays";
    EXPECT_EQ(Find(resolved, "family:xbox-elite-2", "debug", "rb"), nullptr) << "the pads' debug profiles wait";
    const Binding* knob = Find(resolved, "family:surface-dial", "@ttd", "knob");
    ASSERT_NE(knob, nullptr) << "a control surface is a remote by nature";
    EXPECT_EQ(knob->target.name, "ttd.seek_by");
    EXPECT_NE(Find(resolved, "family:surface-dial", "debug", "push"), nullptr);

    InputRouter router;
    std::vector<std::string> actions;
    router.SetActionSink([&actions](const ActionEvent& event) {
        if (event.pressed)
            actions.push_back(event.action);
    });
    router.OnDeviceAdded(context.devices[0]);
    router.SetRole("kbd:system", DeviceRole::DebuggerRemote);
    store.Apply(router, context);
    EXPECT_TRUE(router.Consumes("kbd:system", "key.ctrl.f10"));
    EXPECT_TRUE(router.Consumes("kbd:system", "key.f5"));
    EXPECT_FALSE(router.Consumes("kbd:system", "key.kp8")) << "a debugger remote has no play layer";
    EXPECT_FALSE(router.Consumes("kbd:system", "key.a"));
    EXPECT_FALSE(router.Consumes("kbd:system", "key.alt.f10"));
    router.OnControl({"kbd:system", "key.ctrl.f10", 1, 1});
    router.OnControl({"kbd:system", "key.ctrl.f10", 0, 2});
    router.OnControl({"kbd:system", "key.shift.f11", 1, 3});
    EXPECT_EQ(actions, (std::vector<std::string>{"run.frame_step", "run.step_out"}));
}

/// C7: `repeat: {delay, rate}` is acted on (the shipped debug pad's step keys), and a malformed one says why
TEST(ProfileStore_Test, Repeat_ParsedAndChecked)
{
    ProfileStore store;
    ProfileContext context;
    context.machine = "pentagon";
    context.debugLayer = true;
    context.devices = {{"usb:045e:0b00:1", "Elite", "xbox-elite-2", "xbox", "fake"}};
    const ResolvedProfiles resolved = store.Resolve(context);
    const Binding* step = nullptr;
    for (const auto& [scope, bindings] : resolved.scopes)
        for (const Binding& binding : bindings)
            if (binding.layer == "debug" && binding.sources == std::vector<std::string>{"dpad.right"})
                step = &binding;
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->options.repeatDelay, 15u);
    EXPECT_EQ(step->options.repeatRate, 4u);

    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(Play("x", "", R"("south": {"key": "SPACE", "repeat": {"delay": 20, "rate": 3}})"),
                             profile, error))
        << error;
    EXPECT_FALSE(ParseProfile(Play("x", "", R"("south": {"key": "SPACE", "repeat": {"delay": 20}})"), profile, error));
    EXPECT_NE(error.find("{delay, rate}"), std::string::npos) << error;
    EXPECT_FALSE(ParseProfile(Play("x", "", R"("south": {"key": "SPACE", "repeat": {"delay": 0, "rate": 3}})"),
                              profile, error));
    EXPECT_FALSE(ParseProfile(Play("x", "", R"("south": {"retro": "kempston:fire", "turbo": "classic", "repeat": {"delay": 5, "rate": 3}})"),
                              profile, error));
    EXPECT_NE(error.find("not with turbo"), std::string::npos) << error;
    ASSERT_TRUE(ParseProfile(Play("x", "", R"("south": {"key": "SPACE", "repeat": "none"})"), profile, error))
        << error;
}

TEST(ProfileStore_Test, Directories_WrongKindRefusedAndSaveRoundTrips)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("profiles");
    fs::create_directories(root / "debug");
    std::ofstream(root / "debug" / "wrong.json") << Play("user/wrong", "", R"("south": {"retro": "auto:fire"})");
    // A JSON file of an older version, replaced when the profile is saved again
    fs::create_directories(root / "play");
    std::ofstream(root / "play" / "sabre-wulf.json")
        << Play("user/sabre-wulf", R"(, "match": {"file_sha256": ["3b2a"]})", R"("south": {"retro": "auto:fire"})");

    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(Play("user/sabre-wulf", R"(, "label": "Sabre Wulf", "match": {"file_sha256": ["3b2a"]},
        "connectors": {"kempston": {"device": "kempston-joystick"}}, "options": {"deadzone": 0.2})",
                                  R"("south": {"retro": "kempston:fire", "turbo": "classic"})"),
                             profile, error))
        << error;
    ASSERT_TRUE(ProfileStore::Save(profile, root.string(), error)) << error;
    EXPECT_TRUE(fs::is_regular_file(root / "games" / "sabre-wulf.yaml")) << "a game profile goes to games/";
    EXPECT_FALSE(fs::exists(root / "play" / "sabre-wulf.json")) << "the older JSON goes";

    ProfileStore store;
    std::vector<std::string> errors;
    EXPECT_EQ(store.LoadDirectory(root.string(), ProfileScope::User, errors), 1u);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_NE(errors[0].find("belongs in play/, not debug/"), std::string::npos) << errors[0];
    const Profile* loaded = store.Find("user/sabre-wulf");
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(ProfileStore::Dump(*loaded), ProfileStore::Dump(profile));
    EXPECT_EQ(loaded->connectors.at("kempston").device, "kempston-joystick");
    fs::remove_all(root);
}

TEST(ProfileStore_Test, Aliases_APromotedDeviceKeepsItsProfilesAcrossRestarts)
{
    // architecture.md §5: a `gc:` device promoted to its IOHID id; the profiles naming the old id follow it
    ProfileStore store;
    std::string error;
    ASSERT_TRUE(store.AddText(Play("user/my-pad", R"(, "applies_to": {"devices": ["gc:dualsense-wireless-controller#1"]})",
                                   R"("north": {"key": "ENTER"})"),
                              ProfileScope::User, error))
        << error;
    const HostDeviceId gc = "gc:dualsense-wireless-controller#1";
    const HostDeviceId bt = "bt:054c:0ce6:5a5a";
    ProfileContext context;
    context.devices = {{bt, "DualSense Wireless Controller", "dualsense", "playstation", "fake"}};
    EXPECT_EQ(Find(store.Resolve(context), bt, "", "north"), nullptr) << "no alias yet";

    store.AddAlias(gc, bt);
    EXPECT_EQ(store.AliasesOf(bt), std::vector<HostDeviceId>{gc});
    const Binding* north = Find(store.Resolve(context), bt, "", "north");
    ASSERT_NE(north, nullptr) << "the device scope under the new id";
    EXPECT_EQ(north->target.kind, TargetKind::Keys);

    // Promoted once more: the oldest id follows to the newest
    store.AddAlias(bt, "usb:054c:0ce6:7");
    EXPECT_EQ(store.Aliases().at(gc), "usb:054c:0ce6:7");
    EXPECT_EQ(store.AliasesOf("usb:054c:0ce6:7").size(), 2u);

    // Kept across a restart
    const std::filesystem::path root = TestPathHelper::GetUniqueTestScratchPath("aliases");
    ASSERT_TRUE(store.SaveAliases(root.string(), error)) << error;
    ProfileStore restarted;
    ASSERT_TRUE(restarted.LoadAliases(root.string(), error)) << error;
    EXPECT_EQ(restarted.Aliases(), store.Aliases());
    ProfileStore fresh;
    EXPECT_TRUE(fresh.LoadAliases((root / "none").string(), error)) << "no file: no aliases, no error";
    EXPECT_TRUE(fresh.Aliases().empty());

    std::map<HostDeviceId, HostDeviceId> parsed;
    EXPECT_FALSE(ProfileStore::ParseAliases("aliases:\n  - from: x\n", parsed, error));
    std::filesystem::remove_all(root);
}
