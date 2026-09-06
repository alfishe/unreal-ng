#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/tzxtapebuilder.h"
#include "emulator/io/tape/tapecatalog.h"
#include "emulator/io/tape/tapetypes.h"
#include "loaders/tape/loader_tzx.h"

/// Black-box LoaderTZX::Load tests (design §5.6): every test builds a TZX
/// image with TzxTapeBuilder and asserts on the public TapeImage contract —
/// blocks, descriptors, status and warnings. No EmulatorContext needed: Load
/// is context-free by design (§5.7).
class LoaderTZXLoad_Test : public ::testing::Test
{
protected:
    static TapeImage LoadBytes(const std::vector<uint8_t>& bytes)
    {
        LoaderTZX loader;
        return loader.Load(bytes, "test.tzx");
    }

    static bool WarningContains(const TapeImage& image, const std::string& needle)
    {
        for (const std::string& warning : image.parseWarnings)
        {
            if (warning.find(needle) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    /// 19-byte ROM header block: [flag $00][type Program][name:10][len:2][param1:2][param2:2][checksum]
    static std::vector<uint8_t> MakeHeaderBlock(const std::string& name, uint16_t dataLength)
    {
        std::vector<uint8_t> block;
        block.push_back(0x00);
        block.push_back(0x00);
        for (size_t i = 0; i < 10; i++)
        {
            block.push_back(i < name.size() ? static_cast<uint8_t>(name[i]) : ' ');
        }
        block.push_back(static_cast<uint8_t>(dataLength & 0xFF));
        block.push_back(static_cast<uint8_t>(dataLength >> 8));
        block.push_back(0x00);
        block.push_back(0x80);  // autostart $8000 = none
        block.push_back(0x00);
        block.push_back(0x00);

        uint8_t checksum = 0;
        for (uint8_t byte : block)
        {
            checksum ^= byte;
        }
        block.push_back(checksum);
        return block;
    }

    /// $FF-flag data block with a valid XOR checksum
    static std::vector<uint8_t> MakeDataBlock(const std::vector<uint8_t>& payload)
    {
        std::vector<uint8_t> block;
        block.push_back(0xFF);
        uint8_t checksum = 0xFF;
        for (uint8_t byte : payload)
        {
            block.push_back(byte);
            checksum ^= byte;
        }
        block.push_back(checksum);
        return block;
    }
};

/// region <Header and version>

TEST_F(LoaderTZXLoad_Test, RejectsNonTzxSignature)
{
    std::vector<uint8_t> bytes = TzxTapeBuilder().AddStandardBlock(1000, { 0xFF, 0xFF }).Bytes();
    bytes[0] = 'X';

    TapeImage image = LoadBytes(bytes);
    EXPECT_EQ(image.status, TapeLoadStatus::Malformed);
    EXPECT_FALSE(image.IsUsable());
    EXPECT_NE(image.errorText.find("signature"), std::string::npos);
}

TEST_F(LoaderTZXLoad_Test, RejectsBufferShorterThanHeader)
{
    TapeImage image = LoadBytes({ 'Z', 'X' });
    EXPECT_EQ(image.status, TapeLoadStatus::Malformed);
    EXPECT_NE(image.errorText.find("too short"), std::string::npos);
}

TEST_F(LoaderTZXLoad_Test, RejectsMajorVersionTwo)
{
    std::vector<uint8_t> bytes = TzxTapeBuilder().SetVersion(2, 0x00)
            .AddStandardBlock(1000, { 0xFF, 0xFF }).Bytes();

    TapeImage image = LoadBytes(bytes);
    EXPECT_EQ(image.status, TapeLoadStatus::Malformed);
    EXPECT_NE(image.errorText.find("major version"), std::string::npos);
}

TEST_F(LoaderTZXLoad_Test, NewerMinorWarnsButLoads)
{
    std::vector<uint8_t> bytes = TzxTapeBuilder().SetVersion(1, 0x16)  // 1.22
            .AddStandardBlock(1000, { 0xFF, 0xFF }).Bytes();

    TapeImage image = LoadBytes(bytes);
    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(image.IsUsable());
    EXPECT_TRUE(WarningContains(image, "newer than 1.21"));
}

TEST_F(LoaderTZXLoad_Test, OlderMinorWarnsAboutDeprecatedLayouts)
{
    std::vector<uint8_t> bytes = TzxTapeBuilder().SetVersion(1, 0x0D)  // 1.13
            .AddStandardBlock(1000, { 0xFF, 0xFF }).Bytes();

    TapeImage image = LoadBytes(bytes);
    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(WarningContains(image, "predates 1.20"));
}

/// endregion </Header and version>

/// region <Playable blocks>

TEST_F(LoaderTZXLoad_Test, StandardBlockStaysRomEncoded)
{
    const std::vector<uint8_t> header = MakeHeaderBlock("tzxtest", 8);
    TapeImage image = LoadBytes(TzxTapeBuilder().AddStandardBlock(1000, header).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data, header);
    EXPECT_EQ(image.blocks[0].type, TAP_BLOCK_FLAG_HEADER);
    EXPECT_TRUE(image.blocks[0].timing == std::nullopt) << "$10 keeps representation 1 (ROM standard)";
    EXPECT_EQ(image.descriptors[0].timing.pauseMs, 1000u) << "pause survives as a catalog hint";
    EXPECT_TRUE(image.parseWarnings.empty());
}

TEST_F(LoaderTZXLoad_Test, TurboBlockCarriesFullCustomProfile)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddTurboBlock(2000, 100, 200, 500, 1000, 3000, 3, 100,
                                                               { 0xAB, 0xCD }).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 1u);
    ASSERT_TRUE(image.blocks[0].timing.has_value());
    EXPECT_EQ(image.blocks[0].timing->profile, TapeSpeedProfileEnum::Custom);
    EXPECT_EQ(image.blocks[0].timing->pilotHalfPeriod, 2000u);
    EXPECT_EQ(image.blocks[0].timing->sync1, 100u);
    EXPECT_EQ(image.blocks[0].timing->sync2, 200u);
    EXPECT_EQ(image.blocks[0].timing->zeroHalfPeriod, 500u);
    EXPECT_EQ(image.blocks[0].timing->oneHalfPeriod, 1000u);
    EXPECT_EQ(image.blocks[0].timing->pilotPulses, 3000u);
    EXPECT_EQ(image.blocks[0].timing->bitsInLastByte, 3u);
    EXPECT_EQ(image.blocks[0].timing->pauseMs, 100u);
    EXPECT_EQ(image.blocks[0].data, (std::vector<uint8_t>{ 0xAB, 0xCD }));
    EXPECT_EQ(image.descriptors[0].timing.pilotHalfPeriod, 2000u);
}

TEST_F(LoaderTZXLoad_Test, TurboZeroUsedBitsDropsPaddingByte)
{
    // tzx_normalise_used_bits: bits==0 + non-empty payload => 8 bits, drop the padding byte
    TapeImage image = LoadBytes(TzxTapeBuilder().AddTurboBlock(2000, 100, 200, 500, 1000, 16, 0, 0,
                                                               { 0xAA, 0xBB }).Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data, (std::vector<uint8_t>{ 0xAA }));
    ASSERT_TRUE(image.blocks[0].timing.has_value());
    EXPECT_EQ(image.blocks[0].timing->bitsInLastByte, 8u);
}

TEST_F(LoaderTZXLoad_Test, PureToneEmitsPulseTrain)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPureTone(2168, 5).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].edgePulseTimings, (std::vector<uint32_t>{ 2168, 2168, 2168, 2168, 2168 }));
    EXPECT_TRUE(image.blocks[0].data.empty());
    EXPECT_EQ(image.blocks[0].totalBitstreamLength, 5u * 2168u);
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Tone);
    EXPECT_EQ(image.descriptors[0].timing.profile, TapeSpeedProfileEnum::PulseStream);
    EXPECT_TRUE(image.descriptors[0].playable);
}

TEST_F(LoaderTZXLoad_Test, PulseSequencePreservesPeriods)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPulseSequence({ 667, 735, 667 }).Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].edgePulseTimings, (std::vector<uint32_t>{ 667, 735, 667 }));
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::PulseStream);
}

TEST_F(LoaderTZXLoad_Test, PureDataHasNoFlagFraming)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPureData(400, 800, 6, 0, { 0x5A }).Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data, (std::vector<uint8_t>{ 0x5A }));
    ASSERT_TRUE(image.blocks[0].timing.has_value());
    EXPECT_EQ(image.blocks[0].timing->profile, TapeSpeedProfileEnum::Custom);
    EXPECT_EQ(image.blocks[0].timing->zeroHalfPeriod, 400u);
    EXPECT_EQ(image.blocks[0].timing->oneHalfPeriod, 800u);
    EXPECT_EQ(image.blocks[0].timing->bitsInLastByte, 6u);
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Custom);
    EXPECT_FALSE(image.descriptors[0].flagBytePresent);
}

TEST_F(LoaderTZXLoad_Test, DirectRecordingCollapsesRunsIntoPulses)
{
    // 0xA0 with 5 used bits = 1,0,1,0,0 -> runs of 1,1,1,2 samples at 100 T-states
    TapeImage image = LoadBytes(TzxTapeBuilder().AddDirectRecording(100, 50, 5, { 0xA0 }).Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].edgePulseTimings,
              (std::vector<uint32_t>{ 100, 100, 100, 200, 50u * 3500u })) << "runs + 50ms pause hold-edge";
    EXPECT_EQ(image.blocks[0].totalBitstreamLength, 500u + 175000u);
    EXPECT_TRUE(image.blocks[0].data.empty());
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::PulseStream);
    EXPECT_EQ(image.descriptors[0].rawSize, 1u) << "sample bytes kept for the catalog";
}

TEST_F(LoaderTZXLoad_Test, CswRleDecodesAndScalesToZxClock)
{
    // Sample counts {2, 0 (=8192), 3} at 35000 Hz -> {200, 819200, 300} T-states
    TapeImage image = LoadBytes(TzxTapeBuilder().AddCswRle(0, 35000, { 2, 0, 3 }).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].edgePulseTimings, (std::vector<uint32_t>{ 200, 819200, 300 }));
    EXPECT_TRUE(image.descriptors[0].playable);
}

TEST_F(LoaderTZXLoad_Test, CswCompressedStaysCatalogOnly)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddCswRecording(0, 44100, 2, 0, { 0x01, 0x02, 0x03 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(image.IsUsable());
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_TRUE(image.blocks[0].data.empty());
    EXPECT_TRUE(image.blocks[0].edgePulseTimings.empty());
    EXPECT_FALSE(image.descriptors[0].playable);
    EXPECT_EQ(image.descriptors[0].rawSize, 3u);
    EXPECT_TRUE(WarningContains(image, "Compressed $18"));
}

TEST_F(LoaderTZXLoad_Test, GeneralizedDataStaysCatalogOnly)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddGeneralizedData({ 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13 })
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_FALSE(image.descriptors[0].playable);
    EXPECT_EQ(image.descriptors[0].rawSize, 14u);
    EXPECT_TRUE(WarningContains(image, "$19"));
}

/// endregion </Playable blocks>

/// region <Pauses and stop markers>

TEST_F(LoaderTZXLoad_Test, LongPauseBecomesControlSilence)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPause(1000).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].edgePulseTimings, (std::vector<uint32_t>{ 1000u * 3500u }));
    EXPECT_EQ(image.blocks[0].totalBitstreamLength, 1000u * 3500u);
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Control);
    EXPECT_TRUE(image.descriptors[0].playable);
    EXPECT_EQ(image.descriptors[0].timing.pauseMs, 1000u);
}

TEST_F(LoaderTZXLoad_Test, ShortPauseMergesIntoPreviousTone)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPureTone(2168, 2).AddPause(5).Bytes());

    ASSERT_EQ(image.blocks.size(), 1u) << "no separate block for a short pause";
    EXPECT_EQ(image.blocks[0].edgePulseTimings,
              (std::vector<uint32_t>{ 2168, 2168, 5u * 3500u }));
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Tone);
    EXPECT_EQ(image.descriptors[0].timing.pauseMs, 5u);
}

TEST_F(LoaderTZXLoad_Test, ZeroPauseIsInertStopMarker)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().AddPause(0).AddStandardBlock(1000, { 0xFF, 0xFF }).Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    ASSERT_EQ(image.blocks.size(), 2u);
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Control);
    EXPECT_FALSE(image.descriptors[0].playable);
    EXPECT_TRUE(image.descriptors[1].playable);
    EXPECT_TRUE(WarningContains(image, "stop-the-tape"));
}

/// endregion </Pauses and stop markers>

/// region <Emission context>

TEST_F(LoaderTZXLoad_Test, GroupLabelAttachesToNextPlayableBlock)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddGroupStart("Level 1")
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 2u);
    EXPECT_EQ(image.descriptors[0].groupLabel, "Level 1");
    EXPECT_TRUE(image.descriptors[1].groupLabel.empty()) << "one-shot: the label does not repeat";
}

TEST_F(LoaderTZXLoad_Test, GroupEndClearsPendingLabel)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddGroupStart("Dropped")
                                        .AddGroupEnd()
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_TRUE(image.descriptors[0].groupLabel.empty());
}

TEST_F(LoaderTZXLoad_Test, SignalLevelMarksNextBlockInverted)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddSetSignalLevel(1)
                                        .AddTurboBlock(2000, 100, 200, 500, 1000, 16, 8, 0, { 0xAA })
                                        .AddTurboBlock(2000, 100, 200, 500, 1000, 16, 8, 0, { 0xBB })
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 2u) << "$2B itself emits no block";
    ASSERT_TRUE(image.blocks[0].timing.has_value());
    EXPECT_TRUE(image.blocks[0].timing->invertedLevel);
    ASSERT_TRUE(image.blocks[1].timing.has_value());
    EXPECT_FALSE(image.blocks[1].timing->invertedLevel) << "one-shot polarity";
    EXPECT_TRUE(image.descriptors[0].timing.invertedLevel);
    EXPECT_FALSE(image.descriptors[1].timing.invertedLevel);
}

TEST_F(LoaderTZXLoad_Test, StopIf48KIsInertControl)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStopIf48K()
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 2u);
    EXPECT_EQ(image.descriptors[0].kind, TapeBlockKindEnum::Control);
    EXPECT_FALSE(image.descriptors[0].playable);
}

/// endregion </Emission context>

/// region <Control flow>

TEST_F(LoaderTZXLoad_Test, JumpSkipsOverBlocks)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStandardBlock(0, { 0x11 })
                                        .AddJump(2)          // from block 1 -> block 3
                                        .AddStandardBlock(0, { 0x22 })  // skipped
                                        .AddStandardBlock(0, { 0x33 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 2u);
    EXPECT_EQ(image.blocks[0].data[0], 0x11);
    EXPECT_EQ(image.blocks[1].data[0], 0x33);
}

TEST_F(LoaderTZXLoad_Test, LoopBodyExecutesCountTimes)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddLoopStart(3)
                                        .AddStandardBlock(0, { 0xAA })
                                        .AddLoopEnd()
                                        .AddStandardBlock(0, { 0xBB })
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 4u) << "count is TOTAL executions";
    EXPECT_EQ(image.blocks[0].data[0], 0xAA);
    EXPECT_EQ(image.blocks[1].data[0], 0xAA);
    EXPECT_EQ(image.blocks[2].data[0], 0xAA);
    EXPECT_EQ(image.blocks[3].data[0], 0xBB);
}

TEST_F(LoaderTZXLoad_Test, LoopCountZeroMeansOneExecution)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddLoopStart(0)
                                        .AddStandardBlock(0, { 0xAA })
                                        .AddLoopEnd()
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 1u);
}

TEST_F(LoaderTZXLoad_Test, CallSequenceRunsEachCalleeInOrder)
{
    // $26 at block 1 with offsets {+1, +3}: call block 2, on its $27 call
    // block 4, on that $27 the sequence is exhausted and play continues with
    // the block AFTER the last Return (block 6) — the $27 spec rule.
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStandardBlock(0, { 0xA1 })
                                        .AddCallSequence({ 1, 3 })
                                        .AddStandardBlock(0, { 0xA2 })
                                        .AddReturn()
                                        .AddStandardBlock(0, { 0xA3 })
                                        .AddReturn()
                                        .AddStandardBlock(0, { 0xA4 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 4u);
    EXPECT_EQ(image.blocks[0].data[0], 0xA1);
    EXPECT_EQ(image.blocks[1].data[0], 0xA2);
    EXPECT_EQ(image.blocks[2].data[0], 0xA3);
    EXPECT_EQ(image.blocks[3].data[0], 0xA4) << "exhausted sequence resumes after the Return";
}

TEST_F(LoaderTZXLoad_Test, SelectTakesFirstBranch)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddSelect({ { 2, "SkipOne" }, { 1, "Other" } })
                                        .AddStandardBlock(0, { 0xB1 })  // not taken
                                        .AddStandardBlock(0, { 0xB2 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data[0], 0xB2);
    EXPECT_TRUE(WarningContains(image, "SkipOne"));
}

TEST_F(LoaderTZXLoad_Test, ReturnWithoutCallWarnsButContinues)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddReturn()
                                        .AddStandardBlock(0, { 0xC1 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_TRUE(WarningContains(image, "without a matching $26"));
}

/// endregion </Control flow>

/// region <Metadata>

TEST_F(LoaderTZXLoad_Test, ArchiveInfoTitleWinsOverTextDescription)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddText("Fallback")
                                        .AddArchiveInfo({ { 0, "Real Title" }, { 2, "Someone" } })
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    EXPECT_EQ(image.title, "Real Title");
}

TEST_F(LoaderTZXLoad_Test, TextDescriptionBecomesTitleWhenNoArchiveInfo)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddText("Fallback")
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    EXPECT_EQ(image.title, "Fallback");
}

TEST_F(LoaderTZXLoad_Test, HardwareNoteNamesRecords)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddHardwareInfo({ { 0, 2, 0 }, { 1, 6, 1 } })
                                        .AddStandardBlock(1000, { 0xFF, 0xFF })
                                        .Bytes());

    EXPECT_EQ(image.hardwareNote,
              "computer: ZX Spectrum 48k, Plus (compatible with); "
              "ext. storage: TR-DOS (BetaDisk) (uses)");
    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
}

/// endregion </Metadata>

/// region <Degradation>

TEST_F(LoaderTZXLoad_Test, GlueBlockIsTransparent)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStandardBlock(0, { 0xD1 })
                                        .AddGlue()
                                        .AddStandardBlock(0, { 0xD2 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Ok);
    ASSERT_EQ(image.blocks.size(), 2u);
    EXPECT_TRUE(image.parseWarnings.empty());
}

TEST_F(LoaderTZXLoad_Test, UnknownBlockIdStopsTheScan)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStandardBlock(0, { 0xD1 })
                                        .Put8(0x7E)
                                        .AddStandardBlock(0, { 0xD2 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(image.IsUsable());
    ASSERT_EQ(image.blocks.size(), 1u) << "the prefix before the unknown ID stays loadable";
    EXPECT_TRUE(WarningContains(image, "Unknown block ID 7E"));
}

TEST_F(LoaderTZXLoad_Test, TruncatedPayloadKeepsPartialData)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .Put8(0x10).Put16(1000).Put16(4).PutBytes({ 0xAA, 0xBB })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    EXPECT_TRUE(image.IsUsable());
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data, (std::vector<uint8_t>{ 0xAA, 0xBB }));
    EXPECT_TRUE(WarningContains(image, "Truncated $10"));
}

TEST_F(LoaderTZXLoad_Test, DeprecatedBlockSkippedWithWarning)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddDeprecatedBlock(0x40, { 0x01, 0x02, 0x03 })
                                        .AddStandardBlock(0, { 0xE1 })
                                        .Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Warnings);
    ASSERT_EQ(image.blocks.size(), 1u);
    EXPECT_EQ(image.blocks[0].data[0], 0xE1);
    EXPECT_TRUE(WarningContains(image, "Deprecated block $40"));
}

TEST_F(LoaderTZXLoad_Test, EmptyTapeReportsUnsupported)
{
    TapeImage image = LoadBytes(TzxTapeBuilder().Bytes());

    EXPECT_EQ(image.status, TapeLoadStatus::Unsupported);
    EXPECT_FALSE(image.IsUsable());
    EXPECT_NE(image.errorText.find("No playable blocks"), std::string::npos);
}

/// endregion </Degradation>

/// region <Catalog integration>

TEST_F(LoaderTZXLoad_Test, CatalogClassifiesHeaderAndDataPair)
{
    TapeImage image = LoadBytes(TzxTapeBuilder()
                                        .AddStandardBlock(1000, MakeHeaderBlock("tzxtest", 8))
                                        .AddStandardBlock(1000, MakeDataBlock({ 1, 2, 3, 4, 5, 6, 7, 8 }))
                                        .Bytes());

    ASSERT_EQ(image.blocks.size(), 2u);

    const std::vector<TapeBlockDescriptor> catalog = TapeCatalogParser::Build(image);
    ASSERT_EQ(catalog.size(), 2u);

    EXPECT_EQ(catalog[0].kind, TapeBlockKindEnum::Header);
    EXPECT_TRUE(catalog[0].headerValid);
    EXPECT_EQ(catalog[0].name, "tzxtest");
    EXPECT_EQ(catalog[0].declaredLength, 8u);
    EXPECT_EQ(catalog[0].param1, 0x8000u);
    EXPECT_TRUE(catalog[0].checksumValid);
    EXPECT_EQ(catalog[0].pairedDataIndex, 1u);

    EXPECT_EQ(catalog[1].kind, TapeBlockKindEnum::Data);
    EXPECT_EQ(catalog[1].pairedHeaderIndex, 0u);
    EXPECT_FALSE(catalog[1].headerless);
    EXPECT_TRUE(catalog[1].checksumValid);
}

/// endregion </Catalog integration>
