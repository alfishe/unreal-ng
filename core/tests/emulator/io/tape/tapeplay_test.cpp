#include "tape_test.h"

#include "_helpers/testpathhelper.h"
#include "_helpers/tzxtapebuilder.h"
#include "emulator/memory/memory.h"
#include "emulator/ports/portdecoder.h"
#include "emulator/state/statenode.h"

/// region <Documentation>

/// Plan item F5 (docs/inprogress/2026-11-12-re-api-efficiency): the play request and what it answers.
///
/// - A play while the tape plays leaves the block in flight alone (it used to restart it from its pilot,
///   which broke a load in progress: the Terramex report).
/// - A play never starts on a block without a signal (TZX $2A, $20 pause 0): it passes them and says so.
/// - TZX $2A stops the tape on a machine in 48K mode and is passed in 128K mode; the stop is resumable.
/// - The play report says what the deck does after the call and why it does not play.
/// - The last data block ($10, $11, $14) gives the loader its final edge, with or without a control
///   entry after it.

/// endregion </Documentation>

namespace
{
constexpr uint16_t EAR_LOOP_AT = 0x9000;  // IN A,(#FE); AND #40; JR: a loader listening

const std::vector<uint8_t> kBlock = { 0xFF, 0x01, 0x02, 0x03, 0xFF ^ 0x01 ^ 0x02 ^ 0x03 };
}  // anonymous namespace

class TapePlay_Test : public Tape_Test
{
protected:
    Z80* _cpu = nullptr;
    uint64_t _clock = 1000;

    void SetUp() override
    {
        Tape_Test::SetUp();
        _cpu = _context->pCore->GetZ80();
        const uint8_t earLoop[] = { 0xDB, 0xFE, 0xE6, 0x40, 0x18, 0xFA };
        for (size_t i = 0; i < sizeof(earLoop); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(EAR_LOOP_AT + i), earLoop[i]);
        Machine128k();
    }

    /// A 48K model: 48K mode whatever #7FFD holds
    void Machine48k() { _context->config.mem_model = MM_SPECTRUM48; }

    /// A 128K model with paging free (128K mode), or locked by #7FFD bit 5 (48K mode)
    void Machine128k(bool locked = false)
    {
        _context->config.mem_model = MM_SPECTRUM128;
        if (locked)
        {
            _context->emulatorState.p7FFD |= PORT_7FFD_LOCK;
            // The test machine's own decoder answers (PortDecoder::IsPagingLocked): on a Pentagon 1024 bit 5
            // is the lock only with the memory extension off (#EFF7 bit 2)
            _context->emulatorState.pEFF7 |= 0x04;
        }
        else
            _context->emulatorState.p7FFD &= static_cast<uint8_t>(~PORT_7FFD_LOCK);
    }

    void Insert(TzxTapeBuilder& builder, const char* name)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath(name);
        ASSERT_TRUE(TzxTapeBuilder::WriteToFile(builder.Bytes(), path));
        _tape->stopTape();
        _context->coreState.tapeFilePath = path;
        ASSERT_TRUE(_tape->EnsureImageLoaded());
    }

    /// Blocks: 0 data, 1 TZX $2A, 2 data
    void InsertStop48kTape()
    {
        TzxTapeBuilder builder;
        builder.AddStandardBlock(1000, kBlock).AddStopIf48K().AddStandardBlock(1000, kBlock);
        Insert(builder, "tape-play-2a.tzx");
        ASSERT_EQ(_tape->GetBlocks().size(), 3u);
    }

    /// Blocks: 0 data, 1 TZX $20 pause 0, 2 data
    void InsertPauseZeroTape()
    {
        TzxTapeBuilder builder;
        builder.AddStandardBlock(1000, kBlock).AddPause(0).AddStandardBlock(1000, kBlock);
        Insert(builder, "tape-play-20.tzx");
        ASSERT_EQ(_tape->GetBlocks().size(), 3u);
    }

    /// Playing, the head on the cursor block's edge `edgeIndex`
    void StartAt(size_t edgeIndex)
    {
        _tape->StartPlaybackAtCursor();
        _tape->handleFrameStart();
        ASSERT_TRUE(_tape->IsPlaying());
        ASSERT_NE(_tape->_currentTapeBlock, nullptr);
        _tape->_currentOffsetWithinPulse = edgeIndex;
    }

    /// Playing, the head in the silence after the cursor block's data (its last edge)
    void StartInTrailingPause()
    {
        StartAt(0);
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_TRUE(_tape->_currentTapeBlock->trailingPause);
        _tape->_currentOffsetWithinPulse = _tape->_currentTapeBlock->edgePulseTimings.size() - 1;
    }

    /// Tape time: `tstates` more of the signal go under the head
    void Advance(uint64_t tstates)
    {
        _tape->getTapeStreamBit(_clock);  // seeds the clock after a start or a resume
        _clock += tstates;
        _tape->getTapeStreamBit(_clock);
    }

    /// One frame with `listeningReads` reads from a loader's edge loop
    void Frame(int listeningReads)
    {
        _tape->handleFrameStart();
        for (int i = 0; i < listeningReads; i++)
        {
            _cpu->pc = static_cast<uint16_t>(EAR_LOOP_AT + 2);
            _tape->handlePortIn(0xFEFE);
        }
        _tape->handleFrameEnd();
    }

    /// The head runs over the whole tape, sampled every 250 T (shorter than any pulse) as a loader does.
    /// Returns the level changes it saw; `edges` is the number of edges of every block with a signal
    size_t PlayOutCountingEdges(size_t& edges)
    {
        edges = 0;
        _tape->StartPlaybackAtCursor();
        _cpu->pc = static_cast<uint16_t>(EAR_LOOP_AT + 2);
        _cpu->t = 0;
        _context->emulatorState.t_states = _clock;
        int level = _tape->handlePortIn(0xFEFE) & 0x40;
        size_t changes = 0;
        uint64_t total = 0;
        size_t counted = SIZE_MAX;
        // 4 s is past the end of every tape of these tests (a $10 block: 2 s of pilot, 1 s of pause)
        for (uint64_t elapsed = 0; elapsed < 4 * 3500000ull && _tape->IsPlaying(); elapsed += 250)
        {
            // Each block's edges once, when the head enters it (they are generated then)
            if (_tape->_currentTapeBlock != nullptr && _tape->_currentTapeBlockIndex != counted)
            {
                counted = _tape->_currentTapeBlockIndex;
                edges += _tape->_currentTapeBlock->edgePulseTimings.size();
                for (uint32_t pulse : _tape->_currentTapeBlock->edgePulseTimings)
                    total += pulse;
            }
            _clock += 250;
            _context->emulatorState.t_states = _clock;
            const int sample = _tape->handlePortIn(0xFEFE) & 0x40;
            if (sample != level)
                changes++;
            level = sample;
        }
        EXPECT_GT(total, 0u);
        return changes;
    }

    /// The level a loader reads from now on stays where the last edge left it
    void ExpectEndedHoldingItsLevel()
    {
        EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
        const int expected = _tape->_tapeBitState ? 0x40 : 0;
        for (int i = 0; i < 3; i++)
        {
            _clock += 100;
            _context->emulatorState.t_states = _clock;
            EXPECT_EQ(_tape->handlePortIn(0xFEFE) & 0x40, expected) << "read " << i << " after the end";
        }
        EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended) << "reads after the end start nothing";
    }
};

/// region <The loader marks the blocks>

TEST_F(TapePlay_Test, LoaderMarksTheStop48kBlock)
{
    InsertStop48kTape();
    ASSERT_FALSE(HasFatalFailure());

    const std::vector<TapeBlock>& blocks = _tape->GetBlocks();
    EXPECT_FALSE(blocks[0].stopIn48kMode);
    EXPECT_TRUE(blocks[1].stopIn48kMode);
    EXPECT_TRUE(blocks[1].data.empty());
    EXPECT_TRUE(blocks[1].edgePulseTimings.empty()) << "a $2A has no signal of its own";

    const std::vector<TapeBlockDescriptor>& catalog = _tape->GetBlockCatalog();
    ASSERT_EQ(catalog.size(), 3u);
    EXPECT_EQ(catalog[0].control, TapeControlEnum::None);
    EXPECT_EQ(catalog[1].control, TapeControlEnum::StopIn48kMode);
    EXPECT_EQ(catalog[1].kind, TapeBlockKindEnum::Control);

    InsertPauseZeroTape();
    ASSERT_FALSE(HasFatalFailure());
    EXPECT_FALSE(_tape->GetBlocks()[1].stopIn48kMode);
    EXPECT_EQ(_tape->GetBlockCatalog()[1].control, TapeControlEnum::StopTape);
}

/// endregion </The loader marks the blocks>

/// region <48K mode>

TEST_F(TapePlay_Test, Mode48kIsTheModelOrTheLockedPaging)
{
    Machine128k();
    EXPECT_FALSE(_tape->IsIn48kMode());
    Machine128k(true);
    EXPECT_TRUE(_tape->IsIn48kMode()) << "#7FFD bit 5: the machine is a 48K until reset";
    Machine128k();
    _context->emulatorState.p7FFD |= 0x10;
    EXPECT_FALSE(_tape->IsIn48kMode()) << "the 48K ROM paged in with paging free (USR 0) is 128K mode";
    Machine48k();
    EXPECT_TRUE(_tape->IsIn48kMode());
}

/// endregion </48K mode>

/// region <TZX $2A while the tape plays>

// The head runs from block 0's trailing pause into the $2A with a loader listening all the time: on a 48K the
// tape stops there, before block 2, and a play goes on with block 2
TEST_F(TapePlay_Test, Stop48k_StopsOn48kAndPlayGoesOnWithTheNextBlock)
{
    Machine48k();
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());

    Advance(2 * 3500000ull);  // past the 1 s pause, deep into what follows

    EXPECT_FALSE(_tape->IsPlaying()) << "TZX $2A stops the tape in 48K mode";
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u) << "stopped past the $2A, before the next block";
    EXPECT_EQ(_tape->_currentTapeBlock, nullptr) << "no pulse of block 2 was played";

    const TapePlayReport stopped = _tape->DescribeTransport();
    EXPECT_EQ(stopped.reason, "stop_48k");
    EXPECT_NE(stopped.StateText().find("$2A"), std::string::npos) << stopped.StateText();

    const TapePlayReport report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::Started);
    EXPECT_TRUE(report.Playing());
    EXPECT_EQ(report.position.blockIndex, 2u);
    EXPECT_TRUE(report.skippedBlocks.empty()) << "the stop left the cursor on a block with a signal";

    _tape->handleFrameStart();
    ASSERT_NE(_tape->_currentTapeBlock, nullptr);
    EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 2u);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 0u) << "block 2 from its first pilot pulse";

    // ... and it does move: one pilot pulse later the head is one edge on
    Advance(PILOT_TONE_HALF_PERIOD + 10);
    EXPECT_TRUE(_tape->IsPlaying());
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 1u);
}

// A loader's listening reads restart a tape stopped by $2A, as they restart a parked one
TEST_F(TapePlay_Test, Stop48k_AListeningLoaderGoesOnWithTheNextBlock)
{
    Machine48k();
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    Advance(2 * 3500000ull);
    ASSERT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k);

    Frame(TAPE_START_LISTEN_READS);
    EXPECT_TRUE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
}

TEST_F(TapePlay_Test, Stop48k_PassedIn128kMode)
{
    Machine128k();
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());

    Advance(3500000ull + 10 * PILOT_TONE_HALF_PERIOD + 10);

    EXPECT_TRUE(_tape->IsPlaying()) << "TZX $2A is passed in 128K mode";
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
    ASSERT_NE(_tape->_currentTapeBlock, nullptr);
    EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 2u);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 10u) << "block 2 follows the pause with no gap";
}

TEST_F(TapePlay_Test, Stop48k_StopsOn128kWithLockedPaging)
{
    Machine128k(true);
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());

    Advance(2 * 3500000ull);

    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
}

// The usual way to the $2A: the loader stops listening after block 0 and the watchdog parks the tape
TEST_F(TapePlay_Test, Stop48k_ParkLandsPastItWithItsReason)
{
    Machine48k();
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(0);

    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k) << "the park passed a $2A in 48K mode";
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u) << "never parked on the block without a signal";

    Machine128k();
    _tape->RewindToStart();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(0);

    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::Parked);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
}

// $20 with pause 0 is passed on every machine, as before
TEST_F(TapePlay_Test, PauseZero_IsPassedOnEveryMachine)
{
    Machine48k();
    InsertPauseZeroTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());

    Advance(3500000ull + 10 * PILOT_TONE_HALF_PERIOD + 10);

    EXPECT_TRUE(_tape->IsPlaying());
    ASSERT_NE(_tape->_currentTapeBlock, nullptr);
    EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 2u);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, 10u);
}

/// endregion </TZX $2A while the tape plays>

/// region <Play>

// The Terramex fault: a play sent while the loader reads a block restarted the block from its pilot
TEST_F(TapePlay_Test, PlayWhilePlayingLeavesTheBlockInFlight)
{
    InsertStop48kTape();
    StartAt(0);
    ASSERT_FALSE(HasFatalFailure());
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 20;
    _tape->_currentOffsetWithinPulse = dataEdge;
    const TapeBlock* inFlight = _tape->_currentTapeBlock;

    const TapePlayReport report = _tape->PlayWithReport();

    EXPECT_EQ(report.result, TapePlayResult::AlreadyPlaying);
    EXPECT_TRUE(report.Playing());
    EXPECT_EQ(_tape->_currentTapeBlock, inFlight) << "the block in flight must stay";
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, dataEdge) << "a play must not rewind a block that plays";
    EXPECT_EQ(report.position.blockIndex, 0u);
    EXPECT_EQ(report.position.pulseIndex, dataEdge);
    EXPECT_NE(report.Text().find("already playing"), std::string::npos) << report.Text();
}

// The cursor on a block without a signal (a seek, a park of the old code, a trap that consumed the block
// before): play starts the next block that has one and says what it passed. A play is not stopped by a $2A
TEST_F(TapePlay_Test, PlayOnABlockWithoutSignalStartsTheNextOneThatHasIt)
{
    for (int tape = 0; tape < 2; tape++)
    {
        SCOPED_TRACE(tape == 0 ? "$2A on a 48K" : "$20 pause 0");
        Machine48k();
        if (tape == 0)
            InsertStop48kTape();
        else
            InsertPauseZeroTape();
        ASSERT_FALSE(HasFatalFailure());
        ASSERT_TRUE(_tape->SeekToBlock(1));

        const TapePlayReport report = _tape->PlayWithReport();

        EXPECT_EQ(report.result, TapePlayResult::Started);
        EXPECT_TRUE(report.Playing());
        EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
        EXPECT_EQ(report.position.blockIndex, 2u);
        EXPECT_EQ(report.blockKind, "data");
        ASSERT_EQ(report.skippedBlocks.size(), 1u);
        EXPECT_EQ(report.skippedBlocks[0], 1u);
        EXPECT_NE(report.Text().find("passed block 1"), std::string::npos) << report.Text();

        _tape->handleFrameStart();
        ASSERT_NE(_tape->_currentTapeBlock, nullptr);
        EXPECT_EQ(_tape->_currentTapeBlock->blockIndex, 2u);
    }
}

// The same for the loader's own start (listening reads, the ROM anchor): never on a block without a signal
TEST_F(TapePlay_Test, ListeningStartOnABlockWithoutSignalStartsTheNextOne)
{
    InsertStop48kTape();
    ASSERT_TRUE(_tape->SeekToBlock(1));

    Frame(TAPE_START_LISTEN_READS);

    EXPECT_TRUE(_tape->IsPlaying());
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);
}

// Paused by the watchdog inside a block: play resumes in place. Parked between blocks: play starts the next
// block. With no loader listening the watchdog pauses again, and the state says who did it
TEST_F(TapePlay_Test, PlayAfterAWatchdogPause)
{
    InsertStop48kTape();
    StartAt(0);
    ASSERT_FALSE(HasFatalFailure());
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 20;
    _tape->_currentOffsetWithinPulse = dataEdge;
    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
        Frame(0);
    ASSERT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::LoaderQuiet);
    EXPECT_EQ(_tape->DescribeTransport().reason, "loader_quiet");

    TapePlayReport report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::Resumed);
    EXPECT_TRUE(report.Playing());
    EXPECT_EQ(report.position.blockIndex, 0u);
    EXPECT_EQ(report.position.pulseIndex, dataEdge) << "in place";
    EXPECT_NE(report.Text().find("pauses again"), std::string::npos) << report.Text();

    // Nobody listens: the same pause again, in the same place
    for (uint32_t i = 0; i < TAPE_BLOCK_HOLD_FRAMES; i++)
        Frame(0);
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::LoaderQuiet);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, dataEdge);

    // Parked after block 0 (128K mode: the $2A is passed)
    _tape->RewindToStart();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(0);
    ASSERT_EQ(_tape->GetPauseReason(), TapePauseReason::Parked);

    report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::Started) << "a parked tape starts its next block, nothing is in flight";
    EXPECT_TRUE(report.Playing());
    EXPECT_EQ(report.position.blockIndex, 2u);
    EXPECT_EQ(report.position.pulseIndex, 0u);
}

TEST_F(TapePlay_Test, UserPauseKeepsItsReasonAndResumesInPlace)
{
    InsertStop48kTape();
    StartAt(0);
    ASSERT_FALSE(HasFatalFailure());
    const size_t dataEdge = _tape->_currentTapeBlock->pilotEdgeCount + 20;
    _tape->_currentOffsetWithinPulse = dataEdge;

    _tape->pausePlayback();
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::User);
    EXPECT_EQ(_tape->DescribeTransport().reason, "user");

    EXPECT_EQ(_tape->Play(), TapePlayResult::Resumed);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::None);
    EXPECT_EQ(_tape->_currentOffsetWithinPulse, dataEdge);
}

TEST_F(TapePlay_Test, PlayAtTheEndOfTheTapeStartsNothingAndSaysSo)
{
    // Blocks: 0 data, 1 TZX $2A: nothing with a signal after block 0
    TzxTapeBuilder builder;
    builder.AddStandardBlock(1000, kBlock).AddStopIf48K();
    Insert(builder, "tape-play-tail.tzx");
    ASSERT_FALSE(HasFatalFailure());
    ASSERT_TRUE(_tape->SeekToBlock(1));

    TapePlayReport report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::EndOfTape);
    EXPECT_TRUE(report.Ok());
    EXPECT_FALSE(report.Playing());
    EXPECT_EQ(report.state, TapePlaybackState::Ended);
    EXPECT_EQ(report.reason, "end_of_tape");
    EXPECT_FALSE(report.hasBlock);
    ASSERT_EQ(report.skippedBlocks.size(), 1u);

    // Again, now from the end itself
    report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::EndOfTape);
    EXPECT_TRUE(report.skippedBlocks.empty());
    EXPECT_NE(report.Text().find("end of tape"), std::string::npos) << report.Text();
}

// The watchdog's park after the last data block: a control entry after it is not a place to park at
TEST_F(TapePlay_Test, ParkAfterTheLastBlockWithSignalIsTheEndOfTheTape)
{
    TzxTapeBuilder builder;
    builder.AddStandardBlock(1000, kBlock).AddPause(0);
    Insert(builder, "tape-play-tail20.tzx");
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    for (uint32_t i = 0; i < TAPE_GAP_HOLD_FRAMES; i++)
        Frame(0);

    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Ended);
}

TEST_F(TapePlay_Test, PlayWithoutATape)
{
    _tape->stopTape();
    _context->coreState.tapeFilePath.clear();

    const TapePlayReport report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::NoBlocks);
    EXPECT_FALSE(report.Ok());
    EXPECT_FALSE(report.Playing());
    EXPECT_EQ(report.reason, "no_tape");
}

/// endregion </Play>

/// region <The report>

TEST_F(TapePlay_Test, ReportNodeAndText)
{
    // Blocks: 0 header "GAME", 1 TZX $2A, 2 data
    std::vector<uint8_t> header = { 0x00, 0x03, 'G', 'A', 'M', 'E', ' ', ' ', ' ', ' ', ' ', ' ',
                                    0x03, 0x00, 0x00, 0x80, 0x00, 0x00 };
    uint8_t checksum = 0;
    for (uint8_t byte : header)
        checksum ^= byte;
    header.push_back(checksum);
    TzxTapeBuilder builder;
    builder.AddStandardBlock(1000, header).AddStopIf48K().AddStandardBlock(1000, kBlock);
    Insert(builder, "tape-play-report.tzx");
    ASSERT_FALSE(HasFatalFailure());
    Machine48k();

    TapePlayReport report = _tape->PlayWithReport();
    EXPECT_EQ(report.result, TapePlayResult::Started);
    EXPECT_EQ(report.blockKind, "header");
    EXPECT_EQ(report.blockType, "Code");
    EXPECT_EQ(report.blockName, "GAME");
    EXPECT_EQ(report.blockBytes, 19u);
    EXPECT_EQ(report.blockCount, 3u);
    EXPECT_TRUE(report.mode48k);
    EXPECT_EQ(report.ActionText(), "Tape playback started");
    EXPECT_EQ(report.StateText().rfind("Tape: playing, block 0 of 3 (header Code \"GAME\", 19 bytes), pulse 0, 0.0 of ", 0),
              0u)
        << report.StateText();

    StateNode node = report.ToNode();
    ASSERT_NE(node.find("result"), nullptr);
    EXPECT_EQ(node.find("result")->s, "started");
    EXPECT_TRUE(node.find("playing")->b);
    EXPECT_EQ(node.find("state")->s, "playing");
    EXPECT_EQ(node.find("reason"), nullptr) << "no reason while it plays";
    EXPECT_TRUE(node.find("mode_48k")->b);
    EXPECT_EQ(node.find("block_count")->i, 3);
    ASSERT_NE(node.find("position"), nullptr);
    EXPECT_EQ(node.find("position")->find("block")->i, 0);
    ASSERT_NE(node.find("block"), nullptr);
    EXPECT_EQ(node.find("block")->find("kind")->s, "header");
    EXPECT_EQ(node.find("block")->find("name")->s, "GAME");
    EXPECT_EQ(node.find("block")->find("type")->s, "Code");
    EXPECT_EQ(node.find("block")->find("bytes")->i, 19);
    EXPECT_TRUE(node.find("skipped_blocks")->isArray());
    EXPECT_EQ(node.find("text")->s, report.Text());

    // Stopped by the $2A: the node says why it does not play
    ASSERT_TRUE(_tape->SeekToBlock(0));
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    Advance(2 * 3500000ull);
    node = _tape->DescribeTransport().ToNode();
    EXPECT_FALSE(node.find("playing")->b);
    EXPECT_EQ(node.find("state")->s, "paused");
    ASSERT_NE(node.find("reason"), nullptr);
    EXPECT_EQ(node.find("reason")->s, "stop_48k");
    EXPECT_EQ(node.find("position")->find("block")->i, 2);

    // On the control entry itself (a seek): the block says what it is
    ASSERT_TRUE(_tape->SeekToBlock(1));
    report = _tape->DescribeTransport();
    EXPECT_EQ(report.blockKind, "control");
    EXPECT_EQ(report.blockControl, "stop_48k");
    EXPECT_EQ(report.reason, "stopped");
}

TEST_F(TapePlay_Test, PauseReasonSurvivesATtdRoundTrip)
{
    Machine48k();
    InsertStop48kTape();
    StartInTrailingPause();
    ASSERT_FALSE(HasFatalFailure());
    Advance(2 * 3500000ull);
    ASSERT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k);

    std::vector<uint8_t> state(_tape->TTDStateSize());
    _tape->TTDSaveState(state.data());

    ASSERT_EQ(_tape->Play(), TapePlayResult::Started);
    ASSERT_EQ(_tape->GetPauseReason(), TapePauseReason::None);

    _tape->TTDLoadState(state.data());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::Stop48k);
    EXPECT_EQ(_tape->GetConsumptionCursor(), 2u);

    // A state saved before the reason was kept has 0 there: paused, reason not known
    state[44] = 0;
    _tape->TTDLoadState(state.data());
    EXPECT_EQ(_tape->GetPlaybackState(), TapePlaybackState::Paused);
    EXPECT_EQ(_tape->GetPauseReason(), TapePauseReason::None);
    EXPECT_EQ(_tape->DescribeTransport().reason, "paused");
}

/// endregion </The report>

/// region <The final edge of the last data block>

// A loader counts the edge that ends the last bit. The last block with a signal is the last on the tape
// (the Deflektor case: $11, pause 0) or has only control entries after it; $10, $11 and $14
TEST_F(TapePlay_Test, LastDataBlockEndsWithItsFinalEdge)
{
    const std::vector<uint8_t> data = { 0xFF, 0x5A, 0xFF ^ 0x5A };
    for (int kind = 0; kind < 3; kind++)
    {
        for (int tail = 0; tail < 3; tail++)
        {
            SCOPED_TRACE(std::string(kind == 0 ? "$10" : kind == 1 ? "$11" : "$14") +
                         (tail == 0 ? " last" : tail == 1 ? " then $20 pause 0" : " then $2A"));
            TzxTapeBuilder builder;
            if (kind == 0)
                builder.AddStandardBlock(0, data);
            else if (kind == 1)
                builder.AddTurboBlock(1500, 500, 600, 700, 1400, 200, 8, 0, data);
            else
                builder.AddPureData(700, 1400, 8, 0, data);
            if (tail == 1)
                builder.AddPause(0);
            else if (tail == 2)
                builder.AddStopIf48K();
            Insert(builder, "tape-play-final-edge.tzx");
            ASSERT_FALSE(HasFatalFailure());
            Machine48k();  // the $2A tail on the machine it stops

            size_t edges = 0;
            const size_t changes = PlayOutCountingEdges(edges);

            EXPECT_FALSE(_tape->IsPlaying()) << "the tape ends after its last pulse";
            ASSERT_GT(edges, 0u);
            EXPECT_EQ(changes, edges) << "one level change per edge, the last one included";
            ExpectEndedHoldingItsLevel();
        }
    }
}

/// endregion </The final edge of the last data block>
