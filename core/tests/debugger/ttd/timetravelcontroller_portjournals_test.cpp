// The port journals on the engine (port-journals-on-engine.md): the CPU's IN / OUT hooks record straight into the
// engine's bus journals while recording - one copy, no per-frame feed - and every engine session can be searched
// (v1's machine gate is gone for the engine's controller).
//
// A loader-like loop (in a,(#FE) / out (#FE),a: about 1,750 INs and OUTs a frame) runs a few frames per test,
// 20-60 ms each.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdportsearch.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
class TTDControllerPortJournals_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void Start(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        // di; ld a,7; loop: in a,(#fe); ld a,7; out (#fe),a; jr loop
        const std::vector<uint8_t> program = {0xF3, 0x3E, 0x07, 0xDB, 0xFE, 0x3E, 0x07, 0xD3, 0xFE, 0x18, 0xF8};
        for (size_t i = 0; i < program.size(); i++)
            _context->pMemory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        _context->pCore->GetZ80()->pc = 0x8000;
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }
    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }
    ttd::TTDReply Run(const std::string& verb, std::map<std::string, std::string> options = {})
    {
        return ttd::TTDControl(_context).Execute({verb, std::move(options)});
    }
    /// OUTs to #FE per recorded frame, from the engine's journal
    std::map<uint64_t, uint64_t> OutsPerFrame() const
    {
        std::map<uint64_t, uint64_t> perFrame;
        const ttd::TTDPortJournal& writes = _ttd->GetEngine().BusWrites();
        ttd::TTDPortRecord r;
        for (uint64_t k = writes.FirstIndex(); k < writes.Size() && writes.Get(k, r); ++k)
            ++perFrame[r.frame];
        return perFrame;
    }
};
}  // namespace

/// One copy: the engine's bus journals are the session's port journals, and the session's memory counts them once
TEST_F(TTDControllerPortJournals_Test, TheEngineHoldsTheOnlyCopy)
{
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON"));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(20, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    const ttd::TimeTravelEngine& engine = _ttd->GetEngine();
    EXPECT_GT(engine.BusReads().Size(), 20u * 1500u) << "every IN of the loop (about 1,750 a frame)";
    EXPECT_EQ(engine.BusReads().Size(), engine.BusWrites().Size());
    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_EQ(info.portReadCount, engine.BusReads().Size());
    EXPECT_EQ(info.portWriteCount, engine.BusWrites().Size());

    const ttd::TTDHeapBreakdown heap = _ttd->GetHeapBreakdown();
    const ttd::TTDEngineHeapBreakdown e = engine.HeapBreakdown();
    EXPECT_EQ(heap.portWrites, e.portWrites) << "the OUT journal is held once";
    EXPECT_EQ(heap.portReads, e.portReads + e.busVectors + e.mediaReads) << "the IN journal is held once";
}

/// A search while the recording is paused mid-frame sees the frame being recorded: the CPU wrote into the engine's
/// journal at each OUT, not at the next frame boundary
TEST_F(TTDControllerPortJournals_Test, ASearchWhilePausedSeesTheCurrentFrame)
{
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON"));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _emulator->RunNCPUCycles(20000, /*skipBreakpoints=*/true);   // mid-frame: the frame has OUTs already
    const uint64_t frame = _context->emulatorState.frame_counter;

    ttd::TTDPortQuery q;
    q.direction = ttd::TTDPortJournal::Direction::Write;
    q.portMask = 0x00FF;
    q.portValue = 0x00FE;
    q.from = {frame, 0};
    q.limit = 100000;
    const ttd::TTDPortSearchResult found = _ttd->SearchPortEvents(q);
    ASSERT_TRUE(found.ok) << found.error;
    EXPECT_GT(found.hits.size(), 100u) << "the current frame's OUTs, up to where the machine stands";
    for (const ttd::TTDPortHit& h : found.hits)
        EXPECT_EQ(h.record.frame, frame);
    _ttd->StopRecording();
}

/// v1's machine gate is gone for the engine: TS-Conf (its own interrupt source and DMA) records, reports and
/// searches its port journals like any machine
TEST_F(TTDControllerPortJournals_Test, ATsConfSessionIsSearchable)
{
    ASSERT_NO_FATAL_FAILURE(Start("TSL"));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    const ttd::TTDSessionInfo info = _ttd->GetSessionInfo();
    EXPECT_TRUE(info.portJournalActive) << info.portJournalOffReason;
    const ttd::TTDReply border = Run("port-events", {{"event", "border"}});
    ASSERT_TRUE(border.Ok()) << border.message;
    ttd::TTDPortQuery q;
    q.direction = ttd::TTDPortJournal::Direction::Read;
    q.portMask = 0x00FF;
    q.portValue = 0x00FE;
    const ttd::TTDPortSearchResult reads = _ttd->SearchPortEvents(q);
    ASSERT_TRUE(reads.ok) << reads.error;
    EXPECT_FALSE(reads.hits.empty());
}

/// A seek while recording pauses it; resumed at the paused end, the journals record on from there: every frame
/// holds the loop's OUTs once - no gap, no record twice
TEST_F(TTDControllerPortJournals_Test, ARecordingResumedAfterASeekContinuesTheJournals)
{
    ASSERT_NO_FATAL_FAILURE(Start("PENTAGON"));
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    const ttd::TTDTimePoint end = _ttd->CurrentPosition();
    const uint64_t first = _ttd->GetSessionInfo().sessionStartFrame;

    ttd::TTDReply r = Run("seek", {{"frame", std::to_string(first + 1)}, {"tinframe", "30000"}});
    ASSERT_TRUE(r.Ok()) << r.message;
    r = Run("seek", {{"frame", std::to_string(end.frame)}, {"tinframe", std::to_string(end.tInFrame)}});
    ASSERT_TRUE(r.Ok()) << r.message;
    r = Run("resume");
    ASSERT_TRUE(r.Ok()) << r.message;
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);
    _ttd->StopRecording();

    const std::map<uint64_t, uint64_t> perFrame = OutsPerFrame();
    ASSERT_GE(perFrame.size(), 7u);
    // Whole frames (not the baseline's and the last, partial ones) hold the same number of OUTs
    std::vector<uint64_t> counts;
    for (auto it = std::next(perFrame.begin()); it != std::prev(perFrame.end()); ++it)
        counts.push_back(it->second);
    const auto [lo, hi] = std::minmax_element(counts.begin(), counts.end());
    EXPECT_LE(*hi - *lo, 2u) << "a frame with a gap or with records twice";
}
