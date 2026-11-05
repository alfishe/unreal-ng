/// @file memoryprofile_test.cpp
/// @brief The `memory` section of a software profile (docs/inprogress/2026-11-05-software-metadata-profile): places
/// in every written form, regions of the analysis' kinds, labels, label files, entries and comments; what it refuses,
/// naming the place; a profile keeps the section and writes it back.

#include <gtest/gtest.h>

#include <string>

#include "debugger/analyzers/analysis/l2model.h"
#include "profiles/memoryprofile.h"
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
