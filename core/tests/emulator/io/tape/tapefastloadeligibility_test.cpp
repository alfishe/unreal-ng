#include "pch.h"

#include <gtest/gtest.h>

#include "emulator/io/tape/tapecatalog.h"
#include "emulator/io/tape/tapetypes.h"
#include "loaders/tape/loader_tap.h"    // LoaderTAP (end-to-end pipeline test)
#include "loaders/tape/loader_tape.h"

#include "_helpers/testpathhelper.h"
#include "common/filehelper.h"

#include <vector>

/// region <Test helpers>

namespace
{
    // A trap-shaped byte block: kind Header/Data, ROM-standard profile,
    // flag $00/$FF, checksum ok, playable — passes rules 1-5 of design §5.8.
    TapeBlockDescriptor MakeTrapShaped(TapeBlockKindEnum kind, uint8_t flag, double seconds)
    {
        TapeBlockDescriptor descriptor;
        descriptor.kind = kind;
        descriptor.rawSize = 19;
        descriptor.rawFlag = flag;
        descriptor.timing.profile = TapeSpeedProfileEnum::StandardRom;
        descriptor.checksumValid = true;
        descriptor.playable = true;
        descriptor.estimatedSeconds = seconds;
        return descriptor;
    }

    TapeBlockDescriptor MakeStandardHeader(double seconds = 2.0)
    {
        return MakeTrapShaped(TapeBlockKindEnum::Header, 0x00, seconds);
    }

    TapeBlockDescriptor MakeStandardData(double seconds = 5.0)
    {
        return MakeTrapShaped(TapeBlockKindEnum::Data, 0xFF, seconds);
    }

    // Byte block with Custom timing — the classic turbo loader (TZX $11).
    TapeBlockDescriptor MakeTurboData(double seconds = 1.0)
    {
        TapeBlockDescriptor descriptor = MakeTrapShaped(TapeBlockKindEnum::Data, 0xFF, seconds);
        descriptor.timing.profile = TapeSpeedProfileEnum::Custom;
        return descriptor;
    }

    // Structural entry (TZX $20 pause / $21 group / loop / call...).
    TapeBlockDescriptor MakeControl(double seconds = 1.0)
    {
        TapeBlockDescriptor descriptor;
        descriptor.kind = TapeBlockKindEnum::Control;
        descriptor.playable = true;
        descriptor.estimatedSeconds = seconds;
        return descriptor;
    }

    TapeImage MakeImage(const std::vector<TapeBlockDescriptor>& descriptors, bool linearized = true)
    {
        TapeImage image;
        image.blocks.resize(descriptors.size());  // blocks are irrelevant to Analyze() — descriptors drive it
        image.descriptors = descriptors;
        image.controlFlowLinearized = linearized;
        return image;
    }
}

/// endregion </Test helpers>

/// region <TapeFastLoadEligibility tests>

class TapeFastLoadEligibility_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
    }

    void TearDown() override
    {
    }
};

TEST_F(TapeFastLoadEligibility_Test, FullVerdictOnAllStandardBlocks)
{
    TapeImage image = MakeImage({MakeStandardHeader(2.0), MakeStandardData(5.0)});

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Full);
    EXPECT_EQ(plan.stickinessHorizon, 2u);
    EXPECT_EQ(plan.eligibleBlocks, 2u);
    EXPECT_EQ(plan.firstRejectIndex, SIZE_MAX);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::None);
    ASSERT_EQ(plan.perBlock.size(), 2u);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::None);
    EXPECT_EQ(plan.perBlock[1], FastLoadRejectEnum::None);
    EXPECT_DOUBLE_EQ(plan.totalSeconds, 7.0);
    // The trap removes encoding time only — the ROM 1-second post-block pauses
    // still elapse: (2.0 - 1.0) + (5.0 - 1.0) = 5.0
    EXPECT_DOUBLE_EQ(plan.acceleratedSeconds, 5.0);
}

TEST_F(TapeFastLoadEligibility_Test, TurboFirstBlockSticksWholeTape)
{
    // Design §5.8 flagship example: 26 blocks, block 0 turbo, blocks 1-25
    // vanilla. eligibleBlocks = 25, but stickiness makes the horizon 0.
    std::vector<TapeBlockDescriptor> descriptors;
    descriptors.push_back(MakeTurboData());
    for (int i = 0; i < 25; i++)
    {
        descriptors.push_back(MakeStandardData(1.0));
    }

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage(descriptors));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.stickinessHorizon, 0u);
    EXPECT_EQ(plan.eligibleBlocks, 25u) << "eligibleBlocks exists to explain the difference — reporting 26 here would be a lie";
    EXPECT_EQ(plan.firstRejectIndex, 0u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::NonStandardTiming);
    EXPECT_DOUBLE_EQ(plan.acceleratedSeconds, 0.0) << "nothing fast-loads — the trap removes no time";
    EXPECT_NE(plan.summary.find("none"), std::string::npos);
}

TEST_F(TapeFastLoadEligibility_Test, PartialAtThirdBlock)
{
    TapeImage image = MakeImage({MakeStandardHeader(2.0), MakeStandardData(5.0), MakeTurboData(1.0), MakeStandardData(1.0)});

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Partial);
    EXPECT_EQ(plan.stickinessHorizon, 2u);
    EXPECT_EQ(plan.eligibleBlocks, 3u);  // blocks 0, 1 and 3 are trap-shaped — 3 is beyond the horizon
    EXPECT_EQ(plan.firstRejectIndex, 2u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::NonStandardTiming);
    EXPECT_DOUBLE_EQ(plan.acceleratedSeconds, 5.0);  // prefix only, pauses excluded
    EXPECT_DOUBLE_EQ(plan.totalSeconds, 9.0);
    EXPECT_NE(plan.summary.find("partial"), std::string::npos);
}

TEST_F(TapeFastLoadEligibility_Test, PulseStreamKindsRejected)
{
    TapeBlockDescriptor tone = MakeTrapShaped(TapeBlockKindEnum::Tone, 0x00, 0.5);
    TapeBlockDescriptor pulses = MakeTrapShaped(TapeBlockKindEnum::PulseStream, 0x00, 0.5);
    TapeBlockDescriptor data = MakeStandardData(1.0);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({tone, pulses, data}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::PulseStream);
    EXPECT_EQ(plan.perBlock[1], FastLoadRejectEnum::PulseStream);
    EXPECT_EQ(plan.perBlock[2], FastLoadRejectEnum::None);
    EXPECT_EQ(plan.eligibleBlocks, 1u);
}

TEST_F(TapeFastLoadEligibility_Test, CustomKindMeansNonStandardFlag)
{
    // Custom kind IS a non-$00/$FF flag byte — the ROM loader would never accept it
    TapeBlockDescriptor custom = MakeTrapShaped(TapeBlockKindEnum::Custom, 0x03, 1.0);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({custom}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::NonStandardFlag);
}

TEST_F(TapeFastLoadEligibility_Test, NonStandardFlagRejectedForDataKind)
{
    TapeBlockDescriptor oddFlag = MakeTrapShaped(TapeBlockKindEnum::Data, 0x55, 1.0);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({oddFlag}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::NonStandardFlag);
}

TEST_F(TapeFastLoadEligibility_Test, ChecksumInvalidRejected)
{
    // insult.tap class of failure: ROM-valid shape, corrupted payload —
    // the trap declines, the signal path reproduces "R Tape loading error"
    TapeBlockDescriptor corrupt = MakeTrapShaped(TapeBlockKindEnum::Data, 0xFF, 1.0);
    corrupt.checksumValid = false;

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({MakeStandardHeader(1.0), corrupt}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Partial);
    EXPECT_EQ(plan.stickinessHorizon, 1u);
    EXPECT_EQ(plan.firstRejectIndex, 1u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::ChecksumInvalid);
}

TEST_F(TapeFastLoadEligibility_Test, UnplayableRejected)
{
    TapeBlockDescriptor unplayable = MakeTrapShaped(TapeBlockKindEnum::Data, 0xFF, 1.0);
    unplayable.playable = false;

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({unplayable}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::Unplayable);
    EXPECT_DOUBLE_EQ(plan.totalSeconds, 0.0) << "unplayable blocks do not count towards real-speed duration";
}

TEST_F(TapeFastLoadEligibility_Test, LeadingControlDoesNotKillHorizon)
{
    // Design §5.8 / tzx §8a.2: a leading TZX $20 pause pseudo-block must not
    // drop the horizon to zero on an otherwise-vanilla tape — Control entries
    // are skipped, not rejected.
    TapeImage image = MakeImage({MakeControl(1.5), MakeStandardHeader(2.0), MakeControl(1.0), MakeStandardData(5.0)});

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Full);
    EXPECT_EQ(plan.stickinessHorizon, 4u);
    EXPECT_EQ(plan.eligibleBlocks, 2u) << "Control entries are excluded from eligibleBlocks";
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::ControlBlock);
    EXPECT_EQ(plan.perBlock[2], FastLoadRejectEnum::ControlBlock);
    // Control durations count towards the total (real wall-clock) but their
    // pauses are not "removed" by the trap
    EXPECT_DOUBLE_EQ(plan.totalSeconds, 9.5);
    EXPECT_DOUBLE_EQ(plan.acceleratedSeconds, 5.0);
}

TEST_F(TapeFastLoadEligibility_Test, UnlinearizedImageIsNoneInert)
{
    // Rule 6, image-wide: a loop/jump expansion that hit the bound makes the
    // play order untrustworthy — verdict None, every byte block inert
    TapeImage image = MakeImage({MakeStandardHeader(2.0), MakeStandardData(5.0)}, false);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.stickinessHorizon, 0u);
    EXPECT_EQ(plan.firstRejectIndex, 0u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::ControlFlowInert);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::ControlFlowInert);
    EXPECT_EQ(plan.perBlock[1], FastLoadRejectEnum::ControlFlowInert);
    // eligibleBlocks still reports the intrinsic shape — it explains, never promises
    EXPECT_EQ(plan.eligibleBlocks, 2u);
}

TEST_F(TapeFastLoadEligibility_Test, EmptyImageAndControlOnlyImageAreEmpty)
{
    TapeFastLoadPlan emptyPlan = TapeFastLoadEligibility::Analyze(MakeImage({}));
    EXPECT_EQ(emptyPlan.verdict, FastLoadVerdictEnum::Empty);
    EXPECT_EQ(emptyPlan.stickinessHorizon, 0u);
    EXPECT_TRUE(emptyPlan.perBlock.empty());

    // Pause-only image (e.g. a TZX of nothing but $20 blocks): no loadable payload
    TapeFastLoadPlan controlOnlyPlan = TapeFastLoadEligibility::Analyze(MakeImage({MakeControl(), MakeControl()}));
    EXPECT_EQ(controlOnlyPlan.verdict, FastLoadVerdictEnum::Empty);
}

TEST_F(TapeFastLoadEligibility_Test, ExplicitPauseIsNotInherited)
{
    // Custom-profile block with an explicit 500 ms pause: PauseSeconds must
    // use the explicit value, not the ROM-inherited 1000 ms
    TapeBlockDescriptor header = MakeTrapShaped(TapeBlockKindEnum::Header, 0x00, 1.5);
    header.timing.pauseMs = 500;

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({header}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Full);
    EXPECT_DOUBLE_EQ(plan.acceleratedSeconds, 1.0);  // 1.5 - 0.5
}

TEST_F(TapeFastLoadEligibility_Test, RealTapPipelineYieldsFullVerdict)
{
    // End-to-end through the real pipeline: LoaderTAP → TapeCatalogParser →
    // Analyze. A clean two-block TAP must come out Full with both per-block
    // verdicts None — the property §8.2 (UnifiedStateAcrossFormats) builds on.
    std::vector<uint8_t> tap;
    auto appendBlock = [&tap](const std::vector<uint8_t>& payload)
    {
        tap.push_back(static_cast<uint8_t>(payload.size() & 0xFF));
        tap.push_back(static_cast<uint8_t>(payload.size() >> 8));
        tap.insert(tap.end(), payload.begin(), payload.end());
    };

    // Header: flag $00, type $00 (PROGRAM), name, length 2, params, checksum
    std::vector<uint8_t> headerPayload(19, 0x00);
    headerPayload[1] = 0x00;
    headerPayload[2] = 'P'; headerPayload[3] = 'I'; headerPayload[4] = 'P'; headerPayload[5] = 'E';
    headerPayload[12] = 0x02;  // declared length
    uint8_t checksum = 0;
    for (uint8_t byte : headerPayload) checksum ^= byte;
    headerPayload[18] = checksum;

    // Data: flag $FF, 2 bytes, checksum
    std::vector<uint8_t> dataPayload = {0xFF, 0x3E, 0x00, 0x00};
    checksum = 0;
    for (uint8_t byte : dataPayload) checksum ^= byte;
    dataPayload[3] = checksum;

    appendBlock(headerPayload);
    appendBlock(dataPayload);

    LoaderTAP loader;
    TapeImage image = loader.Load(tap, "pipeline.tap");
    ASSERT_TRUE(image.IsUsable());
    image.descriptors = TapeCatalogParser::Build(image);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Full);
    EXPECT_EQ(plan.stickinessHorizon, 2u);
    EXPECT_EQ(plan.eligibleBlocks, 2u);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::None);
    EXPECT_EQ(plan.perBlock[1], FastLoadRejectEnum::None);
    EXPECT_GT(plan.totalSeconds, 0.0);
    EXPECT_GT(plan.acceleratedSeconds, 0.0);
}

TEST_F(TapeFastLoadEligibility_Test, HeaderlessDataRejected)
{
    // r12: a headerless data block's consumer is a custom loader that never
    // calls the hooked LD-BYTES — the trap cannot serve it, whatever its
    // encoding shape (the reserved §5.8 row, wired after the DIZZY_X report)
    TapeBlockDescriptor headerless = MakeStandardData(1.0);
    headerless.headerless = true;

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(MakeImage({headerless}));

    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::None);
    EXPECT_EQ(plan.stickinessHorizon, 0u);
    EXPECT_EQ(plan.eligibleBlocks, 0u);
    EXPECT_EQ(plan.firstRejectIndex, 0u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::Headerless);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::Headerless);
    EXPECT_NE(plan.summary.find("headerless"), std::string::npos);
}

TEST_F(TapeFastLoadEligibility_Test, RealTapHeaderlessPayloadsArePartial)
{
    // r12 regression on the tape that exposed the gap: DIZZY_X block 0/1 is
    // the ROM-loaded Program pair (trap-shaped); blocks 2-4 are headerless
    // custom-loader payloads that always fall back to the real-speed signal
    // path — the plan must predict Partial with the horizon at block 2.
    std::string filePath = TestPathHelper::GetTestDataPath("loaders/tap/DIZZY_X_ALEX_S__MAX_IWAMOTO.tap");
    ASSERT_TRUE(FileHelper::FileExists(filePath)) << "Test file not found: " << filePath;

    size_t fileSize = FileHelper::GetFileSize(filePath);
    std::vector<uint8_t> buffer(fileSize);
    FileHelper::ReadFileToBuffer(filePath, buffer.data(), fileSize);

    LoaderTAP loader;
    TapeImage image = loader.Load(buffer, "dizzy-x.tap");
    ASSERT_TRUE(image.IsUsable());
    image.descriptors = TapeCatalogParser::Build(image);

    TapeFastLoadPlan plan = TapeFastLoadEligibility::Analyze(image);

    ASSERT_EQ(plan.perBlock.size(), 5u);
    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Partial);
    EXPECT_EQ(plan.stickinessHorizon, 2u);
    EXPECT_EQ(plan.eligibleBlocks, 2u);
    EXPECT_EQ(plan.firstRejectIndex, 2u);
    EXPECT_EQ(plan.firstRejectReason, FastLoadRejectEnum::Headerless);
    EXPECT_EQ(plan.perBlock[0], FastLoadRejectEnum::None);
    EXPECT_EQ(plan.perBlock[1], FastLoadRejectEnum::None);
    EXPECT_EQ(plan.perBlock[2], FastLoadRejectEnum::Headerless);
    EXPECT_EQ(plan.perBlock[3], FastLoadRejectEnum::Headerless);
    EXPECT_EQ(plan.perBlock[4], FastLoadRejectEnum::Headerless);
}

/// endregion </TapeFastLoadEligibility tests>
