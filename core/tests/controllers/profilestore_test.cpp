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

/// C6, D7, H-3: a game profile found by its signatures. The content signature (a re-pack or a rename) and the file
/// hash pick it exactly; the TR-DOS file set picks it after the game saved to its disk (the catalog rule: every entry
/// kept, at most one file changed, a few added; the Jaccard rule for a set without entry tokens); each with how,
/// strongest first, and where the haptics come from
TEST(ProfileStore_Test, Signatures_ExactContentAndFuzzyFileSet)
{
    // Five files: the loader, three code blocks, a data file the game rewrites; an entry token (n:) and a content
    // token (c: / d:) each
    const std::string game = "schema: 1\nkind: play\nid: user/game-signed\nmatch:\n"
                             "  signatures: [\"trdset:sha256:5e1c\"]\n"
                             "  file_set: [\"n:00000000000000e1\", \"n:00000000000000e2\", \"n:00000000000000e3\", "
                             "\"n:00000000000000e4\", \"n:00000000000000e5\", \"c:00000000000000a1\", \"c:00000000000000a2\", "
                             "\"c:00000000000000a3\", \"c:00000000000000a4\", \"d:00000000000000d1\"]\n"
                             "haptics:\n  sources:\n    boom: { filter: ay-envelope, source: \"ay:0\" }\n"
                             "  rules:\n    - { on: boom, play: explosion }\n"
                             "layers:\n  play:\n    west: { key: X }\n";
    ProfileStore store;
    std::string error;
    ASSERT_TRUE(store.AddText(game, ProfileScope::User, error)) << error;
    const Profile* profile = store.Find("user/game-signed");
    ASSERT_NE(profile, nullptr);
    EXPECT_EQ(profile->fileSet.size(), 10u);
    EXPECT_TRUE(profile->IsGame());

    ProfileContext context;
    EXPECT_TRUE(store.Resolve(context).gameMatches.empty()) << "nothing loaded";

    context.signatures = {"file:sha256:ffff", "TRDSET:SHA256:5E1C"};
    ResolvedProfiles resolved = store.Resolve(context);
    ASSERT_EQ(resolved.gameMatches.size(), 1u);
    EXPECT_EQ(resolved.gameMatches[0].second, "signature trdset:sha256:5e1c") << "keys compare in any case";
    EXPECT_EQ(resolved.hapticsReason, "game profile user/game-signed (signature trdset:sha256:5e1c)");
    EXPECT_EQ(resolved.applied.back(), "user/game-signed");

    // Saved to: the data file rewritten in place matches (its entry kept, its data other); a high-score file added too
    const std::vector<std::string> entries = {"n:00000000000000e1", "n:00000000000000e2", "n:00000000000000e3",
                                              "n:00000000000000e4", "n:00000000000000e5"};
    auto with = [&entries](std::vector<std::string> tokens) {
        tokens.insert(tokens.end(), entries.begin(), entries.end());
        return tokens;
    };
    context.signatures = {"file:sha256:eeee", "trdset:sha256:0bad"};
    context.fileSet = with({"c:00000000000000a1", "c:00000000000000a2", "c:00000000000000a3", "c:00000000000000a4",
                            "d:00000000000000d2"});
    resolved = store.Resolve(context);
    ASSERT_EQ(resolved.gameMatches.size(), 1u);
    EXPECT_EQ(resolved.gameMatches[0].second, "file set: every entry kept, 1 of 5 files changed, 0 added");
    const std::vector<std::string> saved = with({"n:00000000000000e6", "c:00000000000000a1", "c:00000000000000a2",
                                                 "c:00000000000000a3", "c:00000000000000a4", "d:00000000000000d1",
                                                 "d:00000000000000f1"});
    context.fileSet = saved;
    resolved = store.Resolve(context);
    ASSERT_EQ(resolved.gameMatches.size(), 1u);
    EXPECT_EQ(resolved.gameMatches[0].second, "file set: every entry kept, 0 of 5 files changed, 1 added");
    EXPECT_EQ(Find(resolved, "", "", "west")->target.kind, TargetKind::Keys);
    context.fileSet = with({"c:00000000000000a1", "c:00000000000000a2", "c:00000000000000b3", "c:00000000000000a4",
                            "d:00000000000000d2"});
    EXPECT_TRUE(store.Resolve(context).gameMatches.empty()) << "two files changed: another program";
    context.fileSet = {"n:00000000000000e1", "n:00000000000000e2", "n:00000000000000e3", "n:00000000000000e5",
                       "c:00000000000000a1", "c:00000000000000a2", "c:00000000000000a3", "d:00000000000000d1"};
    EXPECT_TRUE(store.Resolve(context).gameMatches.empty()) << "an entry missing";

    // A file hash only (an older user profile) and a manual choice rank above the fuzzy one
    ASSERT_TRUE(store.AddText("schema: 1\nkind: play\nid: user/game-old\nmatch: { file_sha256: [\"EEEE\"] }\n"
                              "layers:\n  play:\n    west: { key: Y }\n",
                              ProfileScope::User, error))
        << error;
    context.fileSet = saved;
    resolved = store.Resolve(context);
    ASSERT_EQ(resolved.gameMatches.size(), 2u);
    EXPECT_EQ(resolved.gameMatches[0].first, "user/game-old") << "file:sha256: in the signatures names the file";
    EXPECT_EQ(resolved.gameMatches[0].second, "file sha256 eeee");
    EXPECT_EQ(resolved.gameMatches[1].first, "user/game-signed");
    context.manual = {"user/game-signed"};
    EXPECT_EQ(store.Resolve(context).gameMatches[0].second, "manual");

    // A bad token is refused with the reason
    EXPECT_FALSE(store.AddText("schema: 1\nkind: play\nid: user/bad\nmatch: { file_set: [\"x:12\"] }\n", ProfileScope::User,
                               error));
    EXPECT_NE(error.find("file_set"), std::string::npos) << error;
}

/// The shipped games: the index holds a profile's file set, the lookup finds it by the fuzzy rule without parsing
/// every profile
TEST(ProfileStore_Test, Signatures_GameIndexFindsByFileSet)
{
    namespace fs = std::filesystem;
    const fs::path root = TestPathHelper::GetUniqueTestScratchPath("games");
    fs::create_directories(root);
    std::ofstream(root / "signed.yaml")
        << "schema: 1\nkind: play\nid: builtin/game/signed\nmatch:\n"
           "  file_set: [\"c:00000000000000a1\", \"c:00000000000000a2\", \"c:00000000000000a3\", \"c:00000000000000a4\", "
           "\"d:00000000000000d1\"]\nlayers:\n  play:\n    west: { key: X }\n";
    std::ofstream(root / "other.yaml") << "schema: 1\nkind: play\nid: builtin/game/other\nmatch:\n"
                                          "  file_set: [\"c:00000000000000a1\", \"c:00000000000000b2\"]\nlayers:\n  play:\n"
                                          "    west: { key: Z }\n";
    std::vector<GameEntry> entries;
    std::vector<std::string> errors;
    ASSERT_TRUE(GameLibrary::Build(root.string(), entries, errors)) << (errors.empty() ? "" : errors.front());
    ASSERT_EQ(entries.size(), 2u);
    std::vector<GameEntry> parsed;
    std::string error;
    ASSERT_TRUE(GameLibrary::ParseIndex(GameLibrary::DumpIndex(entries), parsed, error)) << error;
    ASSERT_EQ(parsed.size(), 2u);
    EXPECT_EQ(parsed[0].fileSet, entries[0].fileSet) << "file_set survives the index";

    ProfileStore store;
    ASSERT_TRUE(store.OpenGames(root.string(), errors));
    ProfileContext context;
    context.fileSet = {"c:00000000000000a1", "c:00000000000000a2", "c:00000000000000a3", "c:00000000000000a4",
                       "d:00000000000000d1", "d:00000000000000e1"};
    EXPECT_EQ(store.LoadGamesFor(context, errors), 1u) << (errors.empty() ? "" : errors.front());
    EXPECT_NE(store.Find("builtin/game/signed"), nullptr);
    EXPECT_EQ(store.Find("builtin/game/other"), nullptr) << "a shared token alone is no match";
    EXPECT_EQ(store.Resolve(context).applied.back(), "builtin/game/signed");
    fs::remove_all(root);
}
