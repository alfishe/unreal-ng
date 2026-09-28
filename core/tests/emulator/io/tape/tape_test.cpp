#include "tape_test.h"

#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "common/dumphelper.h"
#include "common/stringhelper.h"
#include "emulator/memory/memory.h"
#include "emulator/spectrumconstants.h"

/// region <SetUp / TearDown>

void Tape_Test::SetUp()
{
    _emulator = new Emulator(LoggerLevel::LogError);
    if (!_emulator->Init())
    {
        throw std::runtime_error("Failed to initialize emulator for Tape_Test");
    }

    _context = _emulator->GetContext();
    _tape = new TapeCUT(_context);
}

void Tape_Test::TearDown()
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

/// endregion </Setup / TearDown>

TEST_F(Tape_Test, generateBitstream)
{
    TapeCUT tape(_context);

    std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0xFF};
    std::vector<uint32_t> referenceResult = {
        // Pilot
        2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168, 2168,

        // Synchronization
        667, 735,

        // [0] - 0x00
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855,

        // [1] - 0x01
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 1710, 1710,

        // [2] - 0x02
        855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 855, 1710, 1710, 855, 855,

        // [3] - 0xFF
        1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710, 1710,

        // Pause
        3500000};
    // Pilot: 10 pulses x 2168 = 21680; sync: 667 + 735 = 1402;
    // data: 2 edges per bit (0x00,0x01,0x02,0xFF) = 71820; pause: 3500000.
    // Total equals the sum of the edgePulseTimings reference below.
    constexpr size_t referenceDuration = 3500000 + 94902;

    TapeBlock tapeBlock;
    tapeBlock.type = TapeBlockFlagEnum::TAP_BLOCK_FLAG_HEADER;
    tapeBlock.data = data;

    size_t result = tape.generateBitstream(tapeBlock, 2168, 667, 735, 855, 1710, 10, 1000);

    EXPECT_EQ(result, referenceDuration);
    EXPECT_EQ(tapeBlock.totalBitstreamLength, referenceDuration);
    EXPECT_EQ(tapeBlock.edgePulseTimings, referenceResult);

    // region <Debug print>

    /*
    std::stringstream ss;

    ss << "Vector len: " << tapeBlock.edgePulseTimings.size() << std::endl;
    std::for_each(tapeBlock.edgePulseTimings.begin(), tapeBlock.edgePulseTimings.end(), [&ss](uint32_t value)
    {
        ss << value << ", ";
    });

    ss << std::endl;
    std::cout << ss.str();
    */

    // endregion </Debug print>
}

/// A1 (tape-manager design §5.2): period parameters are u32 — a TZX $11
/// turbo half-period above 65535 T-states must survive into edgePulseTimings
/// untruncated (edgePulseTimings is vector<uint32_t>; the old u16 signature
/// silently narrowed it).
TEST_F(Tape_Test, generateBitstreamAcceptsU32Periods)
{
    TapeCUT tape(_context);

    constexpr uint32_t BIG_PILOT_HALF = 70000;
    constexpr uint32_t BIG_ONE_HALF = 66000;

    TapeBlock tapeBlock;
    tapeBlock.type = TapeBlockFlagEnum::TAP_BLOCK_FLAG_DATA;
    tapeBlock.data = { 0xFF };  // one byte of ones: 8 x 2 edges

    size_t result = tape.generateBitstream(tapeBlock, BIG_PILOT_HALF, 667, 735, 855,
                                           BIG_ONE_HALF, /*pilotLength_pulses=*/2, /*pause_ms=*/0);

    // 2 pilot edges at 70000 + sync 667/735 + 16 edges at 66000
    constexpr size_t expected = 2 * 70000 + 667 + 735 + 16 * 66000;
    EXPECT_EQ(result, expected);
    EXPECT_EQ(tapeBlock.totalBitstreamLength, expected);

    // No truncation anywhere: first pilot edge and first data edge carry the
    // full u32 values (a u16 path would emit 70000-65536=4464 / 66000-65536=464)
    ASSERT_GE(tapeBlock.edgePulseTimings.size(), 5u);
    EXPECT_EQ(tapeBlock.edgePulseTimings[0], 70000u);
    EXPECT_EQ(tapeBlock.edgePulseTimings[4], 66000u);
}

TEST_F(Tape_Test, getPilotSample)
{
    // Pilot tone uses 2168 T-states half-period per ZX Spectrum tape specification
    // (not 855 which is for zero-bit data encoding)
    constexpr uint16_t PILOT_HALF_PERIOD = 2168;
    constexpr uint16_t PILOT_PERIOD = PILOT_HALF_PERIOD * 2;

    // Test a reasonable number of pilot tone periods
    constexpr size_t maxValue = PILOT_HALF_PERIOD * 100;

    for (size_t tState = 0; tState < maxValue; tState++)
    {
        uint8_t value = _tape->getPilotSample(tState);

        bool referenceValue = (tState % PILOT_PERIOD) < PILOT_HALF_PERIOD;

        if (value != referenceValue)
        {
            FAIL() << StringHelper::Format("Failed at tState: %d. Expected %d, found %d", tState, referenceValue,
                                           value);
        }

        /*
        std::string message = StringHelper::Format("tState: %07d, value: 0x%02X", tState, value);
        std::cout << message << std::endl;
        */
    }
}

/// Nonstandard-loader investigation B1 (docs/inprogress/2026-08-30-fast-tape-loading):
/// ERR_NR ($5C3A) is ordinary RAM. Custom loaders write it as scratch (DIZZY_X_EMELYANOV
/// stores $00 there before calling LD-BYTES with its own flag bytes), so a change of the
/// byte says nothing about the tape. Playback must neither stop nor move past the block.
TEST_F(Tape_Test, ErrNrWriteDuringPlaybackKeepsTapeRolling)
{
    const std::vector<uint8_t> block = { 0xFF, 0x01, 0x02, 0x03, 0xFF ^ 0x01 ^ 0x02 ^ 0x03 };
    TzxTapeBuilder builder;
    builder.AddStandardBlock(1000, block).AddStandardBlock(1000, block);
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("tape-errnr.tzx");
    ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
    _context->coreState.tapeFilePath = path;
    ASSERT_TRUE(_tape->EnsureImageLoaded());

    Memory* memory = _context->pMemory;
    memory->DirectWriteToZ80Memory(SystemVariables48k::ERR_NR, 0xFF);
    _tape->StartPlaybackAtCursor();
    _tape->handleFrameStart();
    ASSERT_TRUE(_tape->IsPlaying());
    ASSERT_EQ(_tape->_currentTapeBlockIndex, 0u);
    _tape->_currentOffsetWithinPulse = 10;  // inside block 0's pilot

    memory->DirectWriteToZ80Memory(SystemVariables48k::ERR_NR, 0x00);
    _tape->handleFrameEnd();

    EXPECT_TRUE(_tape->IsPlaying()) << "An ERR_NR write must not stop the tape";
    EXPECT_EQ(_tape->_currentTapeBlockIndex, 0u) << "The in-flight block must not be skipped";
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 10u) << "The position must not move";
}

/// region <Loader-follow (docs/inprogress/2026-08-30-fast-tape-loading/loader-follow-design.md)>

namespace
{
// Code the reads come from; the classifier looks at the bytes after the IN
constexpr uint16_t KEY_WAIT_AT = 0x8000;   // IN A,(#FE); OR #E0; INC A; JR Z
constexpr uint16_t EAR_LOOP_AT = 0x9000;   // IN A,(#FE); AND #40; JR
constexpr uint16_t OTHER_LOOP_AT = 0xA000; // IN A,(#FE); AND D; JR
constexpr uint16_t ROM_KEY_SCAN_PC = 0x02A1;

void PlaceLoaderFollowCode(Memory* memory)
{
    const uint8_t keyWait[] = { 0xDB, 0xFE, 0xF6, 0xE0, 0x3C, 0x28, 0xF9 };
    const uint8_t earLoop[] = { 0xDB, 0xFE, 0xE6, 0x40, 0x18, 0xFA };
    const uint8_t otherLoop[] = { 0xDB, 0xFE, 0xA2, 0x18, 0xFB };
    for (size_t i = 0; i < sizeof(keyWait); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(KEY_WAIT_AT + i), keyWait[i]);
    for (size_t i = 0; i < sizeof(earLoop); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(EAR_LOOP_AT + i), earLoop[i]);
    for (size_t i = 0; i < sizeof(otherLoop); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(OTHER_LOOP_AT + i), otherLoop[i]);
}
} // anonymous namespace

class TapeLoaderFollow_Test : public Tape_Test
{
protected:
    Z80* _cpu = nullptr;

    void SetUp() override
    {
        Tape_Test::SetUp();
        _cpu = _context->pCore->GetZ80();
        PlaceLoaderFollowCode(_context->pMemory);

        const std::vector<uint8_t> block = { 0xFF, 0x01, 0x02, 0x03, 0xFF ^ 0x01 ^ 0x02 ^ 0x03 };
        TzxTapeBuilder builder;
        builder.AddStandardBlock(1000, block).AddStandardBlock(1000, block);
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("tape-follow.tzx");
        ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
        _context->coreState.tapeFilePath = path;
        ASSERT_TRUE(_tape->EnsureImageLoaded());
    }

    void StartAt(size_t edgeIndex)
    {
        _tape->StartPlaybackAtCursor();
        _tape->handleFrameStart();
        ASSERT_TRUE(_tape->IsPlaying());
        ASSERT_NE(_tape->_currentTapeBlock, nullptr);
        _tape->_currentOffsetWithinPulse = edgeIndex;
    }

    /// One frame with `count` reads from the IN at `inAt` (PC = after the IN)
    void Frame(uint16_t inAt, int count, bool countB = false)
    {
        _tape->handleFrameStart();
        for (int i = 0; i < count; i++)
        {
            _cpu->pc = static_cast<uint16_t>(inAt + 2);
            if (countB)
                _cpu->b++;
            _tape->handlePortIn(0xFEFE);
        }
        _tape->handleFrameEnd();
    }

    /// The ROM loader's first read of an LD-BYTES call: IN A,(#FE) at #0562, PC on the RRA at #0564
    void RomAnchorRead()
    {
        _context->pMemory->SetROM48k();
        const uint8_t* rom = _context->pMemory->GetPhysicalAddressForZ80Page(0);
        ASSERT_EQ(rom[0x0564], 0x1F) << "48 BASIC ROM with LD-BYTES at #0000";
        _tape->handleFrameStart();
        _cpu->pc = 0x0564;
        _tape->handlePortIn(0xFEFE);
    }

    /// The block under the head once playback runs again, and the pulse it starts from
    void ExpectPlayingFrom(size_t block, size_t edge)
    {
        ASSERT_TRUE(_tape->IsPlaying());
        _tape->handleFrameStart();
        ASSERT_NE(_tape->_currentTapeBlock, nullptr);
        EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, block);
        EXPECT_EQ(_tape->_currentOffsetWithinPulse, edge);
    }

    void FreezeInData(size_t edge)
    {
        StartAt(edge);
        for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
            Frame(EAR_LOOP_AT, 0);
        ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
        ASSERT_EQ(_tape->_currentOffsetWithinPulse, edge);
    }

    void RomKeyScanFrame(int count)
    {
        _tape->handleFrameStart();
        for (int i = 0; i < count; i++)
        {
            _cpu->pc = ROM_KEY_SCAN_PC;
            _tape->handlePortIn(0xFEFE);
        }
        _tape->handleFrameEnd();
    }
};

// Requirement R / test T2: a key prompt between blocks parks the tape at the
// next block's pilot, the key-wait loop never starts it again, however long it
// runs, and the loader's next EAR reads start the next block from its first pulse
TEST_F(TapeLoaderFollow_Test, KeyPromptBetweenBlocksParksAndResumesOnEarReads)
{
    StartAt(0);
    ASSERT_TRUE(_tape->_currentTapeBlock->trailingPause);
    _tape->_currentOffsetWithinPulse = _tape->_currentTapeBlock->edgePulseTimings.size() - 1;

    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(KEY_WAIT_AT, 2000);

    EXPECT_FALSE(_tape->IsPlaying()) << "A key-wait loop is not a loader listening";
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u) << "Parked at the next block";

    for (int i = 0; i < 500; i++)
        Frame(KEY_WAIT_AT, 2000);
    EXPECT_FALSE(_tape->IsPlaying()) << "Key reads must never start the tape";
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);

    Frame(EAR_LOOP_AT, TAPE_START_LISTEN_READS);
    EXPECT_TRUE(_tape->IsPlaying()) << "EAR reads must start the tape";
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
    _tape->handleFrameStart();
    ASSERT_NE(_tape->_currentTapeBlock, nullptr);
    EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 1u);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 0u) << "The next block starts from its first pilot pulse";
}

// Test T3/T5: silence (unpacking, AY or beeper music with the ROM keyboard
// scan in the interrupt) in the gap parks the tape the same way
TEST_F(TapeLoaderFollow_Test, BusyLoaderWithInterruptKeyScanParks)
{
    StartAt(0);
    _tape->_currentOffsetWithinPulse = _tape->_currentTapeBlock->edgePulseTimings.size() - 1;

    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        RomKeyScanFrame(8);

    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
}

// Test T6: an interrupt keyboard scan inside a block does not pause a loader
// that keeps listening in the rest of the frame
TEST_F(TapeLoaderFollow_Test, InterruptKeyScanInsideBlockDoesNotPause)
{
    StartAt(0);
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 10;
    _tape->_currentOffsetWithinPulse = dataEdge;

    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES * 2; i++)
    {
        _tape->handleFrameStart();
        for (int k = 0; k < 8; k++)
        {
            _cpu->pc = ROM_KEY_SCAN_PC;
            _tape->handlePortIn(0xFEFE);
        }
        _cpu->pc = static_cast<uint16_t>(EAR_LOOP_AT + 2);
        _tape->handlePortIn(0xFEFE);
        _tape->handleFrameEnd();
    }

    EXPECT_TRUE(_tape->IsPlaying());
}

// Test T10/T11: a loader gone quiet inside a block freezes it. In the pilot the
// tape goes back to the pilot's first pulse; in the data it keeps the exact pulse
TEST_F(TapeLoaderFollow_Test, QuietInsideBlockFreezesAndRewindsOnlyThePilot)
{
    StartAt(0);
    const size_t pilotEdges = _tape->_currentTapeBlock->pilotEdgeCount;
    ASSERT_GT(pilotEdges, 100u);
    _tape->_currentOffsetWithinPulse = 100;

    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
        Frame(EAR_LOOP_AT, 0);
    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 0u) << "A frozen pilot restarts from its first pulse";

    Frame(EAR_LOOP_AT, TAPE_START_LISTEN_READS);
    ASSERT_TRUE(_tape->IsPlaying());
    _tape->_currentOffsetWithinPulse = pilotEdges + 20;

    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
        Frame(EAR_LOOP_AT, 0);
    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, pilotEdges + 20) << "Frozen data keeps the exact pulse";
    EXPECT_EQ(_tape->GetConsumptionCursor(), 0u);
}

// Test T12 (P3): the ROM gave up inside a block's data (BREAK) and LOAD "" calls LD-BYTES again. A fresh
// LD-BYTES needs a pilot: the frozen block starts over, never from the pulse it froze on
TEST_F(TapeLoaderFollow_Test, RomRestartAfterFreezeInDataStartsTheBlockOver)
{
    StartAt(0);
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 20;
    FreezeInData(dataEdge);
    ASSERT_FALSE(HasFatalFailure());

    RomAnchorRead();
    ASSERT_FALSE(HasFatalFailure());
    ExpectPlayingFrom(0, 0);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 0u);
}

// P3: stopped in a block's trailing silence the block was read: the ROM loads the next one
TEST_F(TapeLoaderFollow_Test, RomLoadAfterParkStartsTheNextBlock)
{
    StartAt(0);
    _tape->_currentOffsetWithinPulse = _tape->_currentTapeBlock->edgePulseTimings.size() - 1;
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(KEY_WAIT_AT, 2000);
    ASSERT_EQ(_tape->GetConsumptionCursor(), 1u);

    RomAnchorRead();
    ASSERT_FALSE(HasFatalFailure());
    ExpectPlayingFrom(1, 0);
}

// P3: frozen in a pilot, the ROM gets the whole pilot again (it needs 256 pulses to lock on)
TEST_F(TapeLoaderFollow_Test, RomLoadAfterFreezeInPilotStartsThePilot)
{
    StartAt(300);
    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
        Frame(EAR_LOOP_AT, 0);
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);

    RomAnchorRead();
    ASSERT_FALSE(HasFatalFailure());
    ExpectPlayingFrom(0, 0);
}

// Design §5.1, "nothing moved it since": only a user action moves a frozen position. Picking a block
// (even the same one) or rewinding drops the frozen pulse: the loader then gets the block from its start
TEST_F(TapeLoaderFollow_Test, UserActionDropsTheFrozenPulse)
{
    StartAt(0);
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 20;
    FreezeInData(dataEdge);
    ASSERT_FALSE(HasFatalFailure());

    ASSERT_TRUE(_tape->SeekToBlock(0));
    Frame(EAR_LOOP_AT, TAPE_START_LISTEN_READS);
    ExpectPlayingFrom(0, 0);

    FreezeInData(dataEdge);
    ASSERT_FALSE(HasFatalFailure());
    _tape->RewindToStart();
    Frame(EAR_LOOP_AT, TAPE_START_LISTEN_READS);
    ExpectPlayingFrom(0, 0);
}

// Test T7: a key-wait loop on a parked tape never starts it, however long it waits (5 s here)
TEST_F(TapeLoaderFollow_Test, KeyWaitOnParkedTapeNeverStarts)
{
    StartAt(0);
    _tape->_currentOffsetWithinPulse = _tape->_currentTapeBlock->edgePulseTimings.size() - 1;
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(KEY_WAIT_AT, 2000);
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);

    for (int i = 0; i < 250; i++)
        Frame(KEY_WAIT_AT, 2000);
    EXPECT_FALSE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetConsumptionCursor(), 1u);
    EXPECT_EQ(_tape->_currentTapeBlock, nullptr) << "Parked: the head waits before block 1's pilot";
}

// Test T13 (xpeccy-plus 529c8201): the end of the tape must not swallow the level change that ends the
// last pulse. A loader sampling the EAR line through the whole last block sees one change per edge
TEST_F(TapeLoaderFollow_Test, LastBlockEndsWithItsFinalEdge)
{
    const std::vector<uint8_t> block = { 0xFF, 0x5A, 0xA5 ^ 0xFF ^ 0x5A };
    TzxTapeBuilder builder;
    builder.AddStandardBlock(0, block);  // no pause: the tape ends on the last data edge
    const std::string path = TestPathHelper::GetUniqueTestScratchPath("tape-final-edge.tzx");
    ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
    _tape->stopTape();
    _context->coreState.tapeFilePath = path;
    ASSERT_TRUE(_tape->EnsureImageLoaded());

    StartAt(0);
    const std::vector<uint32_t> pulses = _tape->_currentTapeBlock->edgePulseTimings;
    ASSERT_FALSE(pulses.empty());

    // Sample every 100 T (shorter than any pulse) until the tape ends, and a little after
    _cpu->pc = static_cast<uint16_t>(EAR_LOOP_AT + 2);
    _cpu->t = 0;
    uint64_t now = 1000;
    _context->emulatorState.t_states = now;
    int level = _tape->handlePortIn(0xFEFE) & 0x40;
    size_t changes = 0;
    uint64_t total = 0;
    for (uint32_t pulse : pulses)
        total += pulse;
    for (uint64_t elapsed = 0; elapsed < total + 5000 && _tape->IsPlaying(); elapsed += 100)
    {
        now += 100;
        _context->emulatorState.t_states = now;
        const int sample = _tape->handlePortIn(0xFEFE) & 0x40;
        if (sample != level)
            changes++;
        level = sample;
    }

    EXPECT_FALSE(_tape->IsPlaying()) << "The tape ends after its last pulse";
    EXPECT_EQ(changes, pulses.size()) << "One level change per edge, the last one included";
}

// Test T8: a single EAR read per frame (an issue 2/3 check) never starts the tape
TEST_F(TapeLoaderFollow_Test, OneOffEarReadNeverStarts)
{
    for (int i = 0; i < 100; i++)
        Frame(EAR_LOOP_AT, 1);
    EXPECT_FALSE(_tape->IsPlaying());
}

// Test T9: reads from ROM outside LD-BYTES (keyboard scan, TR-DOS calling
// BREAK-KEY) never start the tape, however many there are
TEST_F(TapeLoaderFollow_Test, RomReadsOutsideLdBytesNeverStart)
{
    for (int i = 0; i < 100; i++)
        RomKeyScanFrame(2000);
    EXPECT_FALSE(_tape->IsPlaying());
}

// §4.2: code the tracker cannot follow starts the tape only as a counting edge loop
TEST_F(TapeLoaderFollow_Test, UnclassifiedReadsStartOnlyAsCountingLoop)
{
    for (int i = 0; i < 10; i++)
        Frame(OTHER_LOOP_AT, 2000, false);
    EXPECT_FALSE(_tape->IsPlaying()) << "A loop that moves nothing is a wait, not a loader";

    Frame(OTHER_LOOP_AT, TAPE_PATTERN_RUN + TAPE_START_LISTEN_READS + 1, true);
    EXPECT_TRUE(_tape->IsPlaying()) << "One counter moving per read is an edge loop";
}

/// endregion </Loader-follow>
