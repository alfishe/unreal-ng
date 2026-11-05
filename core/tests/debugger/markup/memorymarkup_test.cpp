/// @file memorymarkup_test.cpp
/// @brief markup::MemoryMarkup (docs/inprogress/2026-11-05-software-metadata-profile §3): the layers' priority, page
/// places following the paging, the anchors the disassembler takes, the user's marks and their removal, the guard

#include <gtest/gtest.h>

#include <vector>

#include "3rdparty/digestpp/digestpp.hpp"
#include "_helpers/emulatortesthelper.h"
#include "debugger/debugmanager.h"
#include "debugger/markup/memorymarkup.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

using analysis::RegionKind;
using markup::Layer;
using markup::Place;
using markup::Space;

class MemoryMarkup_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    Memory* _memory = nullptr;
    markup::MemoryMarkup* _markup = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _memory = _emulator->GetContext()->pMemory;
        _markup = _emulator->GetContext()->pDebugManager->GetMemoryMarkup();
        ASSERT_NE(_markup, nullptr);
        _memory->SetRAMPageToBank3(0);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static markup::Claim C(Place place, RegionKind kind, const std::string& title = "")
    {
        markup::Claim claim;
        claim.place = place;
        claim.kind = kind;
        claim.title = title;
        return claim;
    }

    static markup::LayerContent Layer1(const std::string& source, std::vector<markup::Claim> claims)
    {
        markup::LayerContent content;
        content.source = source;
        content.claims = std::move(claims);
        return content;
    }
};

TEST_F(MemoryMarkup_Test, HigherLayerWinsWhereClaimsOverlap)
{
    _markup->SetLayer(Layer::Analysis, Layer1("analysis", {C(Place{Space::Cpu, 0, 0x8000, 0x80FF}, RegionKind::Code)}));
    _markup->SetLayer(Layer::Profile,
                      Layer1("profile:game", {C(Place{Space::Cpu, 0, 0x8040, 0x805F}, RegionKind::Sprite, "hero")}));

    const std::vector<markup::Span> spans = _markup->Ranges(0x8000, 0x80FF);
    ASSERT_EQ(spans.size(), 3u);
    EXPECT_EQ(spans[0].lo, 0x8000);
    EXPECT_EQ(spans[0].hi, 0x803F);
    EXPECT_EQ(spans[0].kind, RegionKind::Code);
    EXPECT_TRUE(spans[0].claimStart);
    EXPECT_EQ(spans[1].lo, 0x8040);
    EXPECT_EQ(spans[1].hi, 0x805F);
    EXPECT_EQ(spans[1].layer, Layer::Profile);
    EXPECT_EQ(spans[1].source, "profile:game");
    EXPECT_EQ(spans[1].title, "hero");
    EXPECT_EQ(spans[2].lo, 0x8060);
    EXPECT_EQ(spans[2].kind, RegionKind::Code);
    EXPECT_FALSE(spans[2].claimStart) << "the code after the sprite is the rest of a claim, no instruction start";

    EXPECT_FALSE(_markup->At(0x7FFF).has_value()) << "nothing claims #7FFF";
    EXPECT_EQ(_markup->At(0x8050)->kind, RegionKind::Sprite);
}

TEST_F(MemoryMarkup_Test, PagePlaceShowsOnlyWhereItsPageIsMapped)
{
    _markup->SetLayer(Layer::Profile, Layer1("profile:game", {C(Place{Space::Ram, 3, 0x0100, 0x01FF}, RegionKind::Font)}));
    EXPECT_TRUE(_markup->Ranges(0xC000, 0xFFFF).empty()) << "RAM 0 at #C000";

    _memory->SetRAMPageToBank3(3);
    const std::vector<markup::Span> spans = _markup->Ranges(0xC000, 0xFFFF);
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].lo, 0xC100);
    EXPECT_EQ(spans[0].hi, 0xC1FF);
    EXPECT_EQ(spans[0].kind, RegionKind::Font);

    _memory->SetRAMPageToBank3(0);
    EXPECT_FALSE(_markup->At(0xC100).has_value());
}

TEST_F(MemoryMarkup_Test, AnchorsAreEntriesAndCodeStarts)
{
    markup::LayerContent content = Layer1("profile:game", {C(Place{Space::Cpu, 0, 0x6000, 0x6FFF}, RegionKind::Code),
                                                           C(Place{Space::Cpu, 0, 0x7000, 0x70FF}, RegionKind::Data)});
    content.entries = {Place{Space::Cpu, 0, 0x6A00, 0x6A00}};
    content.comments = {markup::Note{Place{Space::Cpu, 0, 0x6A10, 0x6A10}, "waits for the frame"}};
    _markup->SetLayer(Layer::Profile, content);

    EXPECT_EQ(_markup->Anchors(0x5000, 0x7FFF), (std::vector<uint16_t>{0x6000, 0x6A00}));
    EXPECT_TRUE(_markup->IsInstructionStart(0x6A00));
    EXPECT_FALSE(_markup->IsInstructionStart(0x6A01));
    EXPECT_FALSE(_markup->IsInstructionStart(0x7000)) << "data starts no instruction";
    EXPECT_TRUE(_markup->Anchors(0x6001, 0x69FF).empty()) << "a query inside a claim is no claim start";

    const std::vector<markup::CommentAt> comments = _markup->Comments(0x6000, 0x6FFF);
    ASSERT_EQ(comments.size(), 1u);
    EXPECT_EQ(comments[0].address, 0x6A10);
    EXPECT_EQ(comments[0].text, "waits for the frame");
}

TEST_F(MemoryMarkup_Test, MarkFollowsThePageAndUnmarkCuts)
{
    _memory->SetRAMPageToBank3(4);
    // #BF00-#C0FF crosses into the window of RAM 4: two claims, the second no instruction start
    ASSERT_TRUE(_markup->Mark(0xBF00, 0xC0FF, RegionKind::Code, "routine"));
    EXPECT_EQ(_markup->Anchors(0x8000, 0xFFFF), (std::vector<uint16_t>{0xBF00}));
    EXPECT_EQ(_markup->Ranges(0xBF00, 0xC0FF).size(), 2u) << "two pages, two claims";

    _memory->SetRAMPageToBank3(1);
    EXPECT_FALSE(_markup->At(0xC000).has_value()) << "the mark went with RAM 4";
    EXPECT_EQ(_markup->At(0xBF80)->kind, RegionKind::Code);

    _memory->SetRAMPageToBank3(4);
    const uint64_t revision = _markup->Revision();
    _markup->Unmark(0xC000, 0xC00F);
    EXPECT_GT(_markup->Revision(), revision);
    EXPECT_FALSE(_markup->At(0xC008).has_value());
    ASSERT_TRUE(_markup->At(0xC010).has_value()) << "the rest of the claim stays";
    EXPECT_FALSE(_markup->IsInstructionStart(0xC010)) << "a cut start is no instruction start";

    _markup->SetLayer(Layer::Analysis, Layer1("analysis", {C(Place{Space::Cpu, 0, 0xC000, 0xC00F}, RegionKind::Text)}));
    EXPECT_EQ(_markup->At(0xC008)->kind, RegionKind::Text) << "the lower layer shows again";
    EXPECT_FALSE(_markup->Mark(0x9000, 0x8FFF, RegionKind::Data)) << "a reversed range";
}

TEST_F(MemoryMarkup_Test, GuardHoldsOnlyWhileTheBytesMatch)
{
    for (uint16_t a = 0x9000; a < 0x9010; a++)
        _memory->DirectWriteToZ80Memory(a, static_cast<uint8_t>(a));
    std::vector<uint8_t> bytes;
    for (uint16_t a = 0x9000; a < 0x9010; a++)
        bytes.push_back(static_cast<uint8_t>(a));
    markup::Claim claim = C(Place{Space::Ram, 2, 0x1000, 0x100F}, RegionKind::LevelMap, "level 1");
    claim.sha256 = digestpp::sha256().absorb(bytes.data(), bytes.size()).hexdigest();
    _markup->SetLayer(Layer::Profile, Layer1("profile:game", {claim}));
    ASSERT_TRUE(_markup->At(0x9008).has_value()) << "RAM 2 at #8000, the bytes match";

    _memory->DirectWriteToZ80Memory(0x9008, 0xFF);
    EXPECT_FALSE(_markup->At(0x9008).has_value()) << "another level: the claim is ignored";
}

TEST_F(MemoryMarkup_Test, LayerNamesRoundTrip)
{
    for (int i = 0; i < markup::kLayerCount; i++)
    {
        Layer parsed = Layer::Bundle;
        ASSERT_TRUE(markup::ParseLayer(markup::LayerName(static_cast<Layer>(i)), parsed));
        EXPECT_EQ(static_cast<int>(parsed), i);
    }
    Layer parsed = Layer::Bundle;
    EXPECT_FALSE(markup::ParseLayer("none", parsed));
}
