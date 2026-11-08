/// @file memoryprofile_test.cpp
/// @brief The `memory` section of a software profile (docs/inprogress/2026-11-05-software-metadata-profile): places
/// in every written form, regions of the analysis' kinds, labels, label files, entries and comments; what it refuses,
/// naming the place; a profile keeps the section and writes it back; laid over a machine as the markup's profile
/// layer and the label set profile:<id>.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "debugger/analyzers/analysis/l2model.h"
#include "debugger/debugmanager.h"
#include "debugger/markup/memorymarkup.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "profiles/memoryprofile.h"
#include "profiles/memoryprofilemarkup.h"
#include "profiles/profile.h"
#include "profiles/profilestore.h"
#include "profiles/json.h"

using namespace controllers;

namespace
{
const char* const kProfile = R"(schema: 1
kind: play
id: game/memory-test
match: {file_sha256: ["00"]}
layers: {play: {}}
memory:
  regions:
    - {at: 0x6000-0x7FFF, kind: code, title: game engine}
    - {at: "page:1:0xC000-0xDAFF", kind: sprite, geometry: {w: 3, h: 32}}
    - {at: "#B000", size: 0x400, kind: data, confidence: 600,
       sha256: 0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef}
    - {at: "rom:0:0x0D6B", size: 16, kind: text}
  labels:
    - {name: main_loop, at: 0x6A00}
    - {name: draw_sprite, at: "page:1:$E000", comment: IX = sprite}
  symbols: [memory-test.usym.json]
  entries: [{name: start, at: 24000}]
  comments:
    - {at: 0x6A10, text: waits for the frame interrupt}
)";

/// A `memory` section written as JSON through ParseMemoryProfile: the reason, empty when it parses
std::string Refusal(const std::string& json)
{
    JsonValue section;
    std::string error;
    if (!ParseJson(json, section, error))
        return "bad test JSON: " + error;
    MemoryProfile memory;
    return ParseMemoryProfile(section, memory, error) ? std::string() : error;
}
}  // namespace

TEST(MemoryProfile_Test, AProfileKeepsItsMemorySection)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(kProfile, profile, error)) << error;
    ASSERT_FALSE(profile.memory.IsNull());

    MemoryProfile memory;
    ASSERT_TRUE(ParseMemoryProfile(profile.memory, memory, error)) << error;
    ASSERT_EQ(memory.regions.size(), 4u);
    EXPECT_EQ(memory.regions[0].place.space, MemorySpace::Cpu);
    EXPECT_EQ(memory.regions[0].place.lo, 0x6000);
    EXPECT_EQ(memory.regions[0].place.hi, 0x7FFF);
    EXPECT_EQ(memory.regions[0].kind, "code");
    EXPECT_EQ(memory.regions[0].title, "game engine");
    EXPECT_EQ(memory.regions[0].confidence, 1000) << "a profile's claim is certain unless it says";

    EXPECT_EQ(memory.regions[1].place.space, MemorySpace::Ram);
    EXPECT_EQ(memory.regions[1].place.page, 1);
    EXPECT_EQ(memory.regions[1].place.lo, 0x0000) << "page places keep offsets inside the page";
    EXPECT_EQ(memory.regions[1].place.hi, 0x1AFF);
    ASSERT_TRUE(memory.regions[1].geometry.IsObject());

    EXPECT_EQ(memory.regions[2].place.lo, 0xB000);
    EXPECT_EQ(memory.regions[2].place.hi, 0xB3FF) << "at + size";
    EXPECT_EQ(memory.regions[2].confidence, 600);
    EXPECT_EQ(memory.regions[2].sha256.size(), 64u);

    EXPECT_EQ(memory.regions[3].place.space, MemorySpace::Rom);
    EXPECT_EQ(memory.regions[3].place.lo, 0x0D6B);
    EXPECT_EQ(memory.regions[3].place.hi, 0x0D7A);

    ASSERT_EQ(memory.labels.size(), 2u);
    EXPECT_EQ(memory.labels[0].name, "main_loop");
    EXPECT_EQ(memory.labels[0].place.lo, 0x6A00);
    EXPECT_EQ(memory.labels[1].place.space, MemorySpace::Ram);
    EXPECT_EQ(memory.labels[1].place.lo, 0x2000);
    EXPECT_EQ(memory.labels[1].comment, "IX = sprite");
    EXPECT_EQ(memory.symbols, std::vector<std::string>{"memory-test.usym.json"});
    ASSERT_EQ(memory.entries.size(), 1u);
    EXPECT_EQ(memory.entries[0].place.lo, 24000);
    ASSERT_EQ(memory.comments.size(), 1u);
    EXPECT_EQ(memory.comments[0].text, "waits for the frame interrupt");

    // Written back as it came: a saved profile parses to the same section
    Profile again;
    ASSERT_TRUE(ParseProfile(ProfileStore::Dump(profile), again, error)) << error;
    EXPECT_EQ(again.memory.Dump(), profile.memory.Dump());
}

TEST(MemoryProfile_Test, RefusalsNameThePlace)
{
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0x8000", "kind": "graphics"}]})").find("memory.regions[0]: unknown kind"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"kind": "code"}]})").find("'at' is required"), std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0x8000-0x7000", "kind": "code"}]})").find("ends before it starts"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "page:1:0x3F00-0x4100", "kind": "data"}]})").find("inside its 16K page"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0xFF00", "size": 512, "kind": "data"}]})").find("past the end"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0x8000", "kind": "data", "sha256": "ABC"}]})").find("sha256"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0x8000", "kind": "data", "confidence": 1001}]})").find("0-1000"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": "0x10000", "kind": "data"}]})").find("above #FFFF"), std::string::npos);
    EXPECT_NE(Refusal(R"({"labels": [{"name": "a", "at": "0x8000-0x8001"}]})").find("memory.labels[0]: '0x8000-0x8001'"),
              std::string::npos)
        << "a label is one address";
    EXPECT_NE(Refusal(R"({"labels": [{"name": "two words", "at": 1}]})").find("has a space"), std::string::npos);
    EXPECT_NE(Refusal(R"({"labels": [{"at": 1}]})").find("'name' is required"), std::string::npos);
    EXPECT_NE(Refusal(R"({"symbols": ["../../etc/labels.sym"]})").find("not a path next to the profile"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"symbols": ["/tmp/a.sym"]})").find("not a path next to the profile"), std::string::npos);
    EXPECT_NE(Refusal(R"({"comments": [{"at": "zz", "text": "x"}]})").find("is not an address"), std::string::npos);
    EXPECT_NE(Refusal(R"({"regions": [{"at": 1, "kind": "code", "colour": "red"}]})").find("unknown field 'colour'"),
              std::string::npos);
    EXPECT_NE(Refusal(R"({"maps": []})").find("memory: unknown field 'maps'"), std::string::npos);
    EXPECT_EQ(Refusal(R"({})"), "") << "an empty section is fine";

    // Only a play profile describes a program
    Profile profile;
    std::string error;
    EXPECT_FALSE(ParseProfile(R"({"schema": 1, "kind": "debug", "id": "d", "layers": {"debug": {}}, "memory": {}})",
                              profile, error));
    EXPECT_NE(error.find("only a play profile"), std::string::npos) << error;
}

TEST(MemoryProfile_Test, TheKindsAreTheAnalysisKinds)
{
    // One vocabulary: every analysis region kind is a profile kind and back
    std::vector<std::string> kinds;
    for (int kind = 0; kind <= static_cast<int>(analysis::RegionKind::Untouched); kind++)
        kinds.push_back(analysis::RegionKindName(static_cast<analysis::RegionKind>(kind)));
    EXPECT_EQ(kinds, MemoryRegionKinds());
}

/// MemoryProfileToJson writes what ParseMemoryProfile reads: the section of a save parses to the same lists
TEST(MemoryProfile_Test, WrittenSectionReadsBack)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(kProfile, profile, error)) << error;
    MemoryProfile memory;
    ASSERT_TRUE(ParseMemoryProfile(profile.memory, memory, error)) << error;

    MemoryProfile again;
    ASSERT_TRUE(ParseMemoryProfile(MemoryProfileToJson(memory), again, error)) << error;
    ASSERT_EQ(again.regions.size(), memory.regions.size());
    for (size_t i = 0; i < memory.regions.size(); i++)
    {
        EXPECT_EQ(FormatMemoryPlace(again.regions[i].place), FormatMemoryPlace(memory.regions[i].place)) << i;
        EXPECT_EQ(again.regions[i].kind, memory.regions[i].kind) << i;
        EXPECT_EQ(again.regions[i].title, memory.regions[i].title) << i;
        EXPECT_EQ(again.regions[i].confidence, memory.regions[i].confidence) << i;
        EXPECT_EQ(again.regions[i].sha256, memory.regions[i].sha256) << i;
        EXPECT_EQ(again.regions[i].geometry.Dump(), memory.regions[i].geometry.Dump()) << i;
    }
    EXPECT_EQ(FormatMemoryPlace(memory.regions[1].place), "page:1:0x0000-0x1AFF");
    EXPECT_EQ(FormatMemoryPlace(memory.regions[3].place), "rom:0:0x0D6B-0x0D7A");
    ASSERT_EQ(again.labels.size(), 2u);
    EXPECT_EQ(again.labels[1].name, "draw_sprite");
    EXPECT_EQ(FormatMemoryPlace(again.labels[1].place), "page:1:0x2000");
    EXPECT_EQ(again.labels[1].comment, "IX = sprite");
    EXPECT_EQ(again.symbols, memory.symbols);
    ASSERT_EQ(again.entries.size(), 1u);
    EXPECT_EQ(again.entries[0].name, "start");
    ASSERT_EQ(again.comments.size(), 1u);
    EXPECT_EQ(again.comments[0].text, memory.comments[0].text);
}

/// A save's merge: the analysis claims under the profile's regions, the user's on top (the later wins), nothing
/// repeated, a user comment replacing the one at its address, cache pages skipped
TEST(MemoryProfile_Test, SaveMergesTheLayersInTheirOrder)
{
    MemoryProfile memory;
    MemoryRegionSpec engine;
    engine.place = MemoryPlace{MemorySpace::Cpu, 0, 0x6000, 0x7FFF};
    engine.kind = "code";
    memory.regions.push_back(engine);
    MemoryCommentSpec old;
    old.place = MemoryPlace{MemorySpace::Cpu, 0, 0x6A10, 0x6A10};
    old.text = "old";
    memory.comments.push_back(old);

    auto claim = [](markup::Place place, analysis::RegionKind kind, const std::string& title = "") {
        markup::Claim c;
        c.place = place;
        c.kind = kind;
        c.title = title;
        return c;
    };
    markup::LayerContent user;
    user.claims = {claim({markup::Space::Ram, 3, 0x0100, 0x01FF}, analysis::RegionKind::Sprite, "hero"),
                   claim({markup::Space::Cpu, 0, 0x6000, 0x7FFF}, analysis::RegionKind::Code),
                   claim({markup::Space::Cache, 0, 0x0000, 0x00FF}, analysis::RegionKind::Data)};
    user.comments = {markup::Note{{markup::Space::Cpu, 0, 0x6A10, 0x6A10}, "waits for the frame"},
                     markup::Note{{markup::Space::Rom, 1, 0x0C0A, 0x0C0A}, "the ROM's print"}};
    std::vector<markup::Claim> analysisClaims = {claim({markup::Space::Cpu, 0, 0x8000, 0x80FF}, analysis::RegionKind::Data),
                                                 claim({markup::Space::Cpu, 0, 0x6000, 0x7FFF}, analysis::RegionKind::Code)};
    analysisClaims[0].confidence = 800;

    const MemoryProfileMerge merge = MergeIntoMemoryProfile(user, analysisClaims, memory);
    EXPECT_EQ(merge.regions, 2u) << "the analysis data and the user sprite; the engine is there as it is";
    EXPECT_EQ(merge.comments, 2u);
    EXPECT_EQ(merge.skipped, 1u) << "the cache page";
    ASSERT_EQ(memory.regions.size(), 3u);
    EXPECT_EQ(FormatMemoryPlace(memory.regions[0].place), "0x8000-0x80FF") << "the analysis claim lowest";
    EXPECT_EQ(memory.regions[0].confidence, 800);
    EXPECT_EQ(memory.regions[1].kind, "code") << "then the profile's own";
    EXPECT_EQ(FormatMemoryPlace(memory.regions[2].place), "page:3:0x0100-0x01FF") << "the user's on top";
    EXPECT_EQ(memory.regions[2].title, "hero");
    ASSERT_EQ(memory.comments.size(), 2u);
    EXPECT_EQ(memory.comments[0].text, "waits for the frame") << "replaced at its address";
    EXPECT_EQ(FormatMemoryPlace(memory.comments[1].place), "rom:1:0x0C0A");

    // The user's claim over a lower equal one still goes on top: a later region over it would win otherwise
    MemoryRegionSpec over;
    over.place = MemoryPlace{MemorySpace::Cpu, 0, 0x6000, 0x60FF};
    over.kind = "data";
    memory.regions.push_back(over);
    markup::LayerContent again;
    again.claims = {claim({markup::Space::Cpu, 0, 0x6000, 0x7FFF}, analysis::RegionKind::Code)};
    EXPECT_EQ(MergeIntoMemoryProfile(again, {}, memory).regions, 1u);
    EXPECT_EQ(memory.regions.back().kind, "code");

    std::string error;
    MemoryProfile parsed;
    EXPECT_TRUE(ParseMemoryProfile(MemoryProfileToJson(memory), parsed, error)) << error;
}

/// The section over a machine: regions, entries and comments in the profile layer, labels and entries in the set
/// profile:<id> between the bundles and the files; a missing label file is a warning; the clear takes both back
TEST(MemoryProfile_Test, LaidOverAMachineAndTakenBack)
{
    Profile profile;
    std::string error;
    ASSERT_TRUE(ParseProfile(kProfile, profile, error)) << error;
    MemoryProfile memory;
    ASSERT_TRUE(ParseMemoryProfile(profile.memory, memory, error)) << error;

    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    DebugManager* debug = emulator->GetContext()->pDebugManager;
    markup::MemoryMarkup& markup = *debug->GetMemoryMarkup();
    LabelManager* labels = debug->GetLabelManager();

    std::vector<std::string> warnings;
    ApplyMemoryProfile(memory, profile.id, "Memory test", TestPathHelper::GetUniqueTestScratchPath("profiles"), markup,
                       labels, warnings);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("memory-test.usym.json"), std::string::npos) << warnings[0];

    const std::optional<markup::Span> engine = markup.At(0x6100);
    ASSERT_TRUE(engine.has_value());
    EXPECT_EQ(engine->kind, analysis::RegionKind::Code);
    EXPECT_EQ(engine->layer, markup::Layer::Profile);
    EXPECT_EQ(engine->source, "profile:game/memory-test");
    EXPECT_TRUE(markup.IsInstructionStart(24000)) << "the entry";
    ASSERT_EQ(markup.Comments(0x6A10, 0x6A10).size(), 1u);

    emulator->GetContext()->pMemory->SetRAMPageToBank3(1);
    ASSERT_TRUE(markup.At(0xC100).has_value()) << "page:1 shows while RAM 1 is at #C000";
    EXPECT_EQ(markup.At(0xC100)->kind, analysis::RegionKind::Sprite);

    ASSERT_NE(labels->GetLabelByName("main_loop"), nullptr);
    EXPECT_EQ(labels->GetLabelByName("main_loop")->address, 0x6A00);
    EXPECT_NE(labels->GetLabelByName("start"), nullptr);
    bool found = false;
    for (const auto& set : labels->GetSymbolSets())
        if (set.id == "profile:game/memory-test")
        {
            found = true;
            EXPECT_EQ(set.priority, kMemoryProfileLabelPriority);
        }
    EXPECT_TRUE(found);

    ClearMemoryProfile(profile.id, markup, labels);
    EXPECT_FALSE(markup.At(0x6100).has_value());
    EXPECT_EQ(labels->GetLabelByName("main_loop"), nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
