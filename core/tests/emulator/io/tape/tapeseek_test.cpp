#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/io/tape/tapetypes.h"

/// Seek / position / playback-state unit tests (tape-manager design §6 and
/// §8.1). The engine is driven through the public surfaces plus the CUT
/// cursor fields — the pattern documented on TapeCUT: positions are SET to
/// known values instead of being reached through the ROM LOAD routine, so
/// the tests never become hostage to ROM timing.
///
/// Field-naming note (source of truth: Tape::GetPosition / getTapeStreamBit):
/// `_currentOffsetWithinPulse` holds the edgePulseTimings VECTOR INDEX,
/// `_currentPulseIdxInBlock` the T-states consumed inside that pulse.
class TapePlaybackFixture : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    TapeCUT* _tape = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        if (!_emulator->Init())
        {
            throw std::runtime_error("Failed to initialize emulator for tape seek/position tests");
        }

        _context = _emulator->GetContext();
        _tape = new TapeCUT(_context);
    }

    void TearDown() override
    {
        if (_tape != nullptr)
        {
            delete _tape;
            _tape = nullptr;
        }

        if (_emulator != nullptr)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
            _emulator = nullptr;
        }

        _context = nullptr;  // Owned by _emulator, don't delete
    }

    /// 19-byte ROM Program header with a valid checksum
    static std::vector<uint8_t> MakeHeaderBlock()
    {
        std::vector<uint8_t> bytes;
        bytes.push_back(0x00);  // flag: header
        bytes.push_back(0x00);  // type: Program
        for (const char c : std::string("seektest  "))  // 7 + 3 spaces = 10-byte name field
        {
            bytes.push_back(static_cast<uint8_t>(c));
        }
        bytes.push_back(0x02);
        bytes.push_back(0x00);  // program length: 2 bytes
        bytes.push_back(0x00);
        bytes.push_back(0x80);  // autostart line $8000 = none
        bytes.push_back(0x02);
        bytes.push_back(0x00);  // vars start

        uint8_t checksum = 0;
        for (const uint8_t byte : bytes)
        {
            checksum ^= byte;
        }
        bytes.push_back(checksum);
        return bytes;
    }

    /// Flag-$FF data block (flag + payload + checksum)
    static std::vector<uint8_t> MakeDataBlock()
    {
        const std::vector<uint8_t> payload = { 0x0C, 0x00, 0x00, 0x01, 0xEA, 0x0D };

        std::vector<uint8_t> bytes;
        bytes.push_back(0xFF);
        uint8_t checksum = 0xFF;
        for (const uint8_t byte : payload)
        {
            bytes.push_back(byte);
            checksum ^= byte;
        }
        bytes.push_back(checksum);
        return bytes;
    }

    static std::vector<std::vector<uint8_t>> MakePairs(size_t pairCount)
    {
        std::vector<std::vector<uint8_t>> blocks;
        for (size_t i = 0; i < pairCount; i++)
        {
            blocks.push_back(MakeHeaderBlock());
            blocks.push_back(MakeDataBlock());
        }
        return blocks;
    }

    /// Writes the payloads as $10 standard blocks, mounts the file as the
    /// context's tape path and parses it through EnsureImageLoaded() — real
    /// blocks, real catalog, no ROM dependency
    void MountImage(const std::vector<std::vector<uint8_t>>& payloads, const std::string& fileName)
    {
        TzxTapeBuilder builder;
        for (const std::vector<uint8_t>& payload : payloads)
        {
            builder.AddStandardBlock(1000, payload);
        }

        const std::string path = TestPathHelper::GetTestScratchPath("tapeseek/" + fileName);
        ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));

        _context->coreState.tapeFilePath = path;
        ASSERT_TRUE(_tape->EnsureImageLoaded());
    }

    /// Bind the in-flight block (StartPlaybackAtCursor + one frame start
    /// generates the block's bitstream), then place the pulse cursor at the
    /// given edgePulseTimings index / intra-pulse T-state offset
    void StartAndAdvanceIntoFirstBlock(size_t vectorIndex, size_t tStatesIntoPulse)
    {
        _tape->StartPlaybackAtCursor();
        _tape->handleFrameStart();

        ASSERT_NE(_tape->_currentTapeBlock, nullptr);
        ASSERT_EQ(_tape->_currentTapeBlockIndex, 0u);
        ASSERT_FALSE(_tape->GetBlocks()[0].edgePulseTimings.empty());

        _tape->_currentOffsetWithinPulse = vectorIndex;  // edgePulseTimings index (swapped naming)
        _tape->_currentPulseIdxInBlock = tStatesIntoPulse;
    }
};

class TapeSeek_Test : public TapePlaybackFixture
{
};

class TapePosition_Test : public TapePlaybackFixture
{
};

/// endregion </Fixtures>

TEST_F(TapeSeek_Test, SeekFromIdleSetsCursor)
{
    MountImage(MakePairs(2), "seek-from-idle.tzx");
    ASSERT_EQ(_tape->GetBlocks().size(), 4u);
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);

    EXPECT_TRUE(_tape->SeekToBlock(2));

    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);  // seek arms, never plays

    std::optional<TapePosition> position = _tape->GetPosition();
    ASSERT_TRUE(position.has_value());
    EXPECT_EQ(position->blockIndex, 2u);
}

TEST_F(TapeSeek_Test, SeekBackwardPastConsumed)
{
    MountImage(MakePairs(2), "seek-backward.tzx");

    _tape->ConsumeBlock(0);
    _tape->ConsumeBlock(1);
    _tape->ConsumeBlock(2);
    ASSERT_EQ(_tape->GetConsumptionCursor(), 3u);

    EXPECT_TRUE(_tape->SeekToBlock(1));
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
}

TEST_F(TapeSeek_Test, SeekWhilePlayingAbandonsPartial)
{
    MountImage(MakePairs(2), "seek-while-playing.tzx");
    StartAndAdvanceIntoFirstBlock(500, 100);
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);

    // Seek to the SAME block: a consume-style stop would advance the cursor
    // past block 0 — abandon keeps it AT 0 ("restart this block", §6.2)
    EXPECT_TRUE(_tape->SeekToBlock(0));

    EXPECT_EQ(_tape->GetConsumptionCursor(), 0u);
    EXPECT_EQ(_tape->_currentTapeBlock, nullptr);
    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);  // no frozen state left
}

TEST_F(TapeSeek_Test, SeekWhilePausedDropsFreeze)
{
    MountImage(MakePairs(2), "seek-while-paused.tzx");
    StartAndAdvanceIntoFirstBlock(500, 100);

    _tape->pausePlayback();
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);

    EXPECT_TRUE(_tape->SeekToBlock(2));

    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);
    EXPECT_EQ(_tape->_currentTapeBlock, nullptr);
}

TEST_F(TapeSeek_Test, SeekOutOfRangeFails)
{
    MountImage(MakePairs(1), "seek-out-of-range.tzx");
    ASSERT_EQ(_tape->GetBlocks().size(), 2u);

    std::optional<TapePosition> before = _tape->GetPosition();
    ASSERT_TRUE(before.has_value());
    ASSERT_EQ(before->blockIndex, 0u);

    EXPECT_FALSE(_tape->SeekToBlock(2));  // == block count: one past the end

    EXPECT_EQ(_tape->GetConsumptionCursor(), 0u);
    std::optional<TapePosition> after = _tape->GetPosition();
    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->blockIndex, before->blockIndex);  // position unchanged
}

TEST_F(TapeSeek_Test, SeekWithoutImageFails)
{
    EXPECT_FALSE(_tape->SeekToBlock(0));
    EXPECT_FALSE(_tape->GetPosition().has_value());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);
}

TEST_F(TapeSeek_Test, RewindKeepsImageAndCatalog)
{
    MountImage(MakePairs(2), "rewind.tzx");
    const size_t blockCount = _tape->GetBlocks().size();
    const size_t catalogCount = _tape->GetBlockCatalog().size();
    ASSERT_GT(blockCount, 0u);
    ASSERT_GT(catalogCount, 0u);

    StartAndAdvanceIntoFirstBlock(500, 100);
    _tape->ConsumeBlock(0);

    _tape->RewindToStart();

    EXPECT_EQ(_tape->GetConsumptionCursor(), 0u);
    EXPECT_EQ(_tape->GetBlocks().size(), blockCount);         // image kept (FR-5)
    EXPECT_EQ(_tape->GetBlockCatalog().size(), catalogCount); // catalog kept
    EXPECT_FALSE(_tape->GetBlocks()[0].data.empty());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);
}

TEST_F(TapePosition_Test, StateEnumTransitions)
{
    // Single block: the natural end-of-tape stop parks the cursor at the
    // block count — the exact precondition of the Ended state
    MountImage({ MakeDataBlock() }, "state-transitions.tzx");
    ASSERT_EQ(_tape->GetBlocks().size(), 1u);
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);

    _tape->StartPlaybackAtCursor();
    _tape->handleFrameStart();
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);

    _tape->pausePlayback();
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);

    _tape->ResumePlaybackFromPause();
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Playing);

    _tape->stopPlayback();  // in-flight block counts as consumed
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
}

TEST_F(TapePosition_Test, PositionFieldsMidBlock)
{
    MountImage(MakePairs(1), "position-mid-block.tzx");
    _tape->StartPlaybackAtCursor();
    _tape->handleFrameStart();

    const TapeBlock& block = _tape->GetBlocks()[0];
    ASSERT_FALSE(block.edgePulseTimings.empty());
    ASSERT_GT(block.edgePulseTimings.size(), 20u);

    // Sample A: block start
    _tape->_currentOffsetWithinPulse = 0;
    _tape->_currentPulseIdxInBlock = 0;
    std::optional<TapePosition> sampleA = _tape->GetPosition();
    ASSERT_TRUE(sampleA.has_value());
    EXPECT_EQ(sampleA->blockIndex, 0u);
    EXPECT_EQ(sampleA->pulseIndex, 0u);
    EXPECT_EQ(sampleA->offsetWithinPulse, 0u);
    EXPECT_DOUBLE_EQ(sampleA->secondsIntoBlock, 0.0);

    // Sample B: 10 pulses in, 500 T-states into the current pulse
    _tape->_currentOffsetWithinPulse = 10;
    _tape->_currentPulseIdxInBlock = 500;
    std::optional<TapePosition> sampleB = _tape->GetPosition();
    ASSERT_TRUE(sampleB.has_value());
    EXPECT_EQ(sampleB->blockIndex, 0u);
    EXPECT_EQ(sampleB->pulseIndex, 10u);
    EXPECT_EQ(sampleB->offsetWithinPulse, 500u);

    uint64_t expectedTStates = 500;
    for (size_t i = 0; i < 10; i++)
    {
        expectedTStates += block.edgePulseTimings[i];
    }
    EXPECT_DOUBLE_EQ(sampleB->secondsIntoBlock, static_cast<double>(expectedTStates) / 3500000.0);
    EXPECT_GT(sampleB->blockTotalSeconds, 0.0);  // from the catalog descriptor
    EXPECT_DOUBLE_EQ(sampleB->blockTotalSeconds, _tape->GetBlockCatalog()[0].estimatedSeconds);

    // Sample C: further into the stream — elapsed signal time strictly grows
    _tape->_currentOffsetWithinPulse = 20;
    _tape->_currentPulseIdxInBlock = 500;
    std::optional<TapePosition> sampleC = _tape->GetPosition();
    ASSERT_TRUE(sampleC.has_value());
    EXPECT_GT(sampleC->secondsIntoBlock, sampleB->secondsIntoBlock);
}

TEST_F(TapePosition_Test, PositionNulloptWithoutImage)
{
    EXPECT_FALSE(_tape->GetPosition().has_value());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Idle);

    MountImage(MakePairs(1), "nullopt.tzx");
    EXPECT_TRUE(_tape->GetPosition().has_value());
}

/// The fast-load plan must be computed over the parser-FILLED catalog, not
/// the raw loader-supplied descriptors (design §5.5/§5.8): loader descriptors
/// are partial by contract — checksums, flags and durations are parser-
/// derived. Regression lock for the live smoke-test catch where every
/// app-level tape read "checksum_invalid" with totalSeconds == 0 while the
/// hand-filled unit fixtures stayed green.
class TapeFastLoadPlan_Test : public TapePlaybackFixture
{
};

TEST_F(TapeFastLoadPlan_Test, PlanComputedOverFilledCatalog)
{
    MountImage(MakePairs(2), "fastloadplan.tzx");

    const TapeFastLoadPlan& plan = _tape->GetFastLoadPlan();
    const std::vector<TapeBlockDescriptor>& catalog = _tape->GetBlockCatalog();

    ASSERT_EQ(catalog.size(), 4u);

    // Standard ROM header+data pairs with valid checksums: everything traps
    EXPECT_EQ(plan.verdict, FastLoadVerdictEnum::Full);
    EXPECT_EQ(plan.eligibleBlocks, 4u);
    EXPECT_EQ(plan.stickinessHorizon, 4u);
    EXPECT_EQ(plan.firstRejectIndex, SIZE_MAX);
    for (FastLoadRejectEnum reject : plan.perBlock)
    {
        EXPECT_EQ(reject, FastLoadRejectEnum::None);
    }

    // Durations come from the FILLED descriptors — the raw loader hints
    // leave estimatedSeconds at 0 and would sum a 0.0 s tape
    EXPECT_GT(plan.totalSeconds, 0.0);
    EXPECT_GT(plan.acceleratedSeconds, 0.0);

    // Coherence with the catalog the control planes display
    for (const TapeBlockDescriptor& descriptor : catalog)
    {
        EXPECT_TRUE(descriptor.checksumValid);
    }
    EXPECT_NE(plan.summary.find("full"), std::string::npos);
}
