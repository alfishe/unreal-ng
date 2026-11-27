/// @file ttdstorereads_test.cpp
/// @brief RQ-2 (docs/inprogress/2026-10-28-ttd-analysis-feedback/replay-queries-2026-11-27.md): the verbs that read
/// the store at frame starts (memory-at at a frame start, memory-diff, memory-series) leave a running machine
/// running and a recording recording; what they read while it records is what the stopped recording holds, and the
/// recording replays exactly afterwards. What moves the machine keeps D8: a point inside a frame, frames of an
/// ancestor line, a segment that left memory for its file.
///
/// The guest counts at #5B00, writes the count over the bitmap and flips the shown screen (#7FFD bit 3) every 64
/// loops: every frame start differs.

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/state/statenodejson.h"

using ttd::TTDControl;
using ttd::TTDReply;
using ttd::TTDSessionState;

namespace
{
using Options = std::map<std::string, std::string>;

class TTDStoreReads_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        FeatureManager* features = _emulator->GetFeatureManager();
        features->setFeature(Features::kDebugMode, true);
        features->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _context->config.turbo_mode = false;

        // DI; LD HL,#4000
        // loop: LD A,(#5B00); INC A; LD (#5B00),A; LD (HL),A; INC HL; AND #3F; JR NZ,skip
        //       LD A,(#5B01); XOR 8; LD (#5B01),A; LD BC,#7FFD; OUT (C),A
        // skip: LD A,H; CP #58; JR C,cont; LD H,#40
        // cont: JR loop
        static const uint8_t program[] = {0xF3, 0x21, 0x00, 0x40, 0x3A, 0x00, 0x5B, 0x3C, 0x32, 0x00, 0x5B, 0x77,
                                          0x23, 0xE6, 0x3F, 0x20, 0x0D, 0x3A, 0x01, 0x5B, 0xEE, 0x08, 0x32, 0x01,
                                          0x5B, 0x01, 0xFD, 0x7F, 0xED, 0x79, 0x7C, 0xFE, 0x58, 0x38, 0x02, 0x26,
                                          0x40, 0x18, 0xDD};
        Memory* memory = _context->pMemory;
        for (size_t i = 0; i < sizeof(program); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        memory->DirectWriteToZ80Memory(0x5B00, 0);
        memory->DirectWriteToZ80Memory(0x5B01, 0);
        Z80* z80 = _context->pCore->GetZ80();
        z80->pc = 0x8000;
        z80->sp = 0xBFF0;
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
        _emulator = nullptr;
    }

    TTDReply Run(const std::string& verb, Options options = {})
    {
        return TTDControl(_context).Execute({verb, std::move(options)});
    }
    uint64_t Frame() const { return _context->emulatorState.frame_counter; }
    static std::string Str(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n ? n->s : std::string("<missing>");
    }
    static bool Bool(const TTDReply& r, const char* key)
    {
        const StateNode* n = r.body.find(key);
        return n && n->b;
    }

    /// The four store reads over frames @p from .. @p to, as text: memory-at of the CPU's view and of RAM page 5 at
    /// @p frames, memory-diff of page 5 and a memory-series of both
    std::vector<std::string> StoreReads(uint64_t from, uint64_t to, const std::vector<uint64_t>& frames)
    {
        std::vector<std::string> answers;
        auto keep = [&](const std::string& verb, Options options) {
            const TTDReply r = Run(verb, std::move(options));
            EXPECT_TRUE(r.Ok()) << verb << ": " << r.message;
            answers.push_back(verb + " " + StateNodeToJsonText(r.body));
        };
        for (const uint64_t f : frames)
        {
            keep("memory-at", {{"space", "cpu"}, {"frame", std::to_string(f)}, {"offset", "0x4000"}, {"length", "6914"}});
            keep("memory-at", {{"space", "ram5"}, {"frame", std::to_string(f)}, {"offset", "0"}, {"length", "6914"}});
        }
        keep("memory-diff", {{"space", "ram5"}, {"from_frame", std::to_string(from)}, {"to_frame", std::to_string(to)}});
        TTDReply series = Run("memory-series", {{"ranges", "0x5B00:2, ram5:0:64, cpu:0x4000:64"},
                                                {"from_frame", std::to_string(from)},
                                                {"to_frame", std::to_string(to)},
                                                {"format", "hex"}});
        EXPECT_TRUE(series.Ok()) << series.message;
        // The work counters differ by what the walk had decoded before; the rows are the answer
        const StateNode* rows = series.body.find("rows");
        answers.push_back("memory-series " + (rows ? StateNodeToJsonText(*rows) : std::string("<no rows>")));
        return answers;
    }

    /// The recording runs and is not paused
    void ExpectRecording(const char* after)
    {
        EXPECT_EQ(_ttd->GetState(), TTDSessionState::Recording) << after;
        EXPECT_FALSE(Bool(Run("status"), "recording_paused")) << after;
    }
    /// D8: the recording is paused for browsing
    void ExpectRecordingPaused(const char* after)
    {
        EXPECT_EQ(_ttd->GetState(), TTDSessionState::Detached) << after;
        EXPECT_TRUE(Bool(Run("status"), "recording_paused")) << after;
    }

    /// The loop running from this thread's view, past @p frames frames from here
    void RunAsync(uint64_t frames)
    {
        const uint64_t from = Frame();
        _emulator->StartAsync();
        const uint64_t& frame = _context->emulatorState.frame_counter;
        ASSERT_TRUE(TestWait::For([&] { return _emulator->IsRunning() && frame >= from + frames; },
                                  std::chrono::seconds(10)));
    }
    /// The machine went on after a read: not paused, and frames keep ending
    void ExpectRunsOn(const char* after)
    {
        EXPECT_FALSE(_emulator->IsPaused()) << after;
        const uint64_t& frame = _context->emulatorState.frame_counter;
        const uint64_t from = frame;
        EXPECT_TRUE(TestWait::For([&] { return frame > from; }, std::chrono::seconds(10))) << after;
    }
};
}  // namespace

/// The proof for the recording state: what the store reads give while the recording runs (the machine inside a
/// frame) is what they give once it is stopped, for the first frame, the middle and the newest checkpoint; the
/// recording is not paused by them, records on, and replays exactly afterwards
TEST_F(TTDStoreReads_Test, ReadsWhileRecordingEqualTheStoppedRecordingAndLeaveItExact)
{
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    // The live bytes at each frame start: the CPU's view of #4000..#5B01
    std::map<uint64_t, std::string> live;
    auto keepLive = [&] {
        static const char kHex[] = "0123456789ABCDEF";
        std::string hex;
        for (uint32_t a = 0x4000; a < 0x5B02; ++a)
        {
            const uint8_t b = _context->pMemory->DirectReadFromZ80Memory(static_cast<uint16_t>(a));
            hex += kHex[b >> 4];
            hex += kHex[b & 0x0F];
        }
        live[Frame()] = hex;
    };
    keepLive();
    for (int i = 0; i < 8; ++i)
    {
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
        keepLive();
    }
    // Inside the frame in progress: its start is the newest checkpoint
    _emulator->RunTStates(20000);
    const uint64_t newest = Frame();
    ASSERT_EQ(newest, first + 8);
    const ttd::TTDTimePoint here = _ttd->CurrentPosition();
    const size_t checkpoints = _ttd->GetCheckpointCount();
    const std::vector<uint64_t> frames = {first, first + 3, newest - 1, newest};

    const std::vector<std::string> whileRecording = StoreReads(first, newest, frames);
    ExpectRecording("the store reads");
    EXPECT_EQ(_ttd->CurrentPosition(), here) << "nothing moved";
    EXPECT_EQ(_ttd->GetCheckpointCount(), checkpoints);
    for (const uint64_t f : frames)
    {
        const TTDReply cpu = Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(f)}, {"offset", "0x4000"},
                                               {"length", "6914"}});
        ASSERT_TRUE(cpu.Ok()) << cpu.message;
        EXPECT_EQ(Str(cpu, "moment"), "frame_start");
        EXPECT_EQ(Str(cpu, "hex"), live[f]) << "frame " << f << ": the bytes the machine held live";
    }
    ExpectRecording("memory-at");

    // The recording goes on: the same history, longer
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    EXPECT_EQ(_ttd->GetCheckpointCount(), checkpoints + 4);
    ASSERT_TRUE(Run("stop").Ok());
    EXPECT_EQ(StoreReads(first, newest, frames), whileRecording) << "the stopped recording holds the same bytes";

    // Every recorded frame replays into the next checkpoint
    const TTDReply check = Run("replay-check");
    ASSERT_TRUE(check.Ok()) << check.message;
    EXPECT_EQ(check.body.find("diverged")->i, 0);
    EXPECT_GE(check.body.find("checked")->i, 11);
}

/// D8 stays for what moves the machine or needs the frame's last writes: a point inside a frame pauses a recording
/// that runs, for the CPU's view (a replay) and for a memory (the write journal)
TEST_F(TTDStoreReads_Test, InsideAFrameWhileRecordingPausesTheRecording)
{
    for (const char* space : {"cpu", "ram5"})
    {
        SCOPED_TRACE(space);
        ASSERT_TRUE(Run("start").Ok());
        const uint64_t first = Frame();
        _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
        const TTDReply start = Run("memory-at", {{"space", space}, {"frame", std::to_string(first + 2)},
                                                 {"offset", space == std::string("cpu") ? "0x5B00" : "0x1B00"},
                                                 {"length", "1"}});
        ASSERT_TRUE(start.Ok()) << start.message;
        ExpectRecording("a frame start");
        const TTDReply inside = Run("memory-at", {{"space", space}, {"frame", std::to_string(first + 2)},
                                                  {"tinframe", "30000"},
                                                  {"offset", space == std::string("cpu") ? "0x5B00" : "0x1B00"},
                                                  {"length", "1"}});
        ASSERT_TRUE(inside.Ok()) << inside.message;
        EXPECT_EQ(Str(inside, "moment"), "inside_frame");
        EXPECT_NE(Str(inside, "hex"), Str(start, "hex")) << "the counter moved inside the frame";
        ExpectRecordingPaused("a point inside a frame");
        ASSERT_TRUE(Run("stop").Ok());
        ASSERT_TRUE(Run("invalidate").Ok());
    }
}

/// D8 stays for frames the read cannot take from the recorded line: on a branch that records, a frame of the
/// branch is read with the recording running, a frame of its parent pauses it
TEST_F(TTDStoreReads_Test, AnAncestorsFrameWhileRecordingABranchPausesTheRecording)
{
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    _emulator->RunNFrames(8, /*skipBreakpoints=*/true);
    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(first + 4)}}).Ok());
    ASSERT_TRUE(Run("resume").Ok());
    _emulator->RunNFrames(4, /*skipBreakpoints=*/true);
    ASSERT_NE(_ttd->ActiveBranch(), ttd::kTTDTrunk);
    ExpectRecording("the branch records");

    auto cpuAt = [&](uint64_t frame) {
        return Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(frame)}, {"offset", "0x5B00"}, {"length", "2"}});
    };
    auto seriesFrom = [&](uint64_t frame) {
        return Run("memory-series", {{"ranges", "0x5B00:2"}, {"from_frame", std::to_string(frame)}});
    };
    const TTDReply own = cpuAt(first + 6);
    ASSERT_TRUE(own.Ok()) << own.message;
    ASSERT_TRUE(seriesFrom(first + 5).Ok());
    ExpectRecording("the branch's own frames");

    const TTDReply parent = cpuAt(first + 2);
    ASSERT_TRUE(parent.Ok()) << parent.message;
    ExpectRecordingPaused("the parent's frame");
    // The paused recording answers as it did
    EXPECT_EQ(Str(cpuAt(first + 6), "hex"), Str(own, "hex"));

    // The whole lineage as a series pauses too
    ASSERT_TRUE(Run("resume").Ok());
    _emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ExpectRecording("resumed at the paused end");
    const TTDReply whole = Run("memory-series", {{"ranges", "0x5B00:2"}});
    ASSERT_TRUE(whole.Ok()) << whole.message;
    ExpectRecordingPaused("a series from the trunk's start");
}

/// D8 stays for a segment that left memory for its file (the two tiers): reading it back changes what the store
/// holds, so the recording pauses first; the frames still in memory are read with the recording running
TEST_F(TTDStoreReads_Test, AnEvictedSegmentWhileRecordingPausesTheRecording)
{
    _ttd->SetShadowRecordingRoot(TestPathHelper::GetUniqueTestScratchPath("ttd-store-reads"));
    _ttd->SetHistoryLimit(16, 0);
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    // Segments leave memory once their files are written (a writer thread): frames until one did
    for (int i = 0; i < 400 && _ttd->GetEngine().EvictedSegments() == 0; ++i)
        _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    ASSERT_GT(_ttd->GetEngine().EvictedSegments(), 0u);
    const uint64_t newest = Frame();

    auto ramAt = [&](uint64_t frame) {
        return Run("memory-at", {{"space", "ram5"}, {"frame", std::to_string(frame)}, {"offset", "0x1B00"}, {"length", "2"}});
    };
    ASSERT_TRUE(ramAt(newest).Ok());
    ASSERT_TRUE(Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(newest)}, {"offset", "0x5B00"}, {"length", "2"}}).Ok());
    ASSERT_TRUE(Run("memory-diff", {{"space", "ram5"}, {"from_frame", std::to_string(newest - 1)}, {"to_frame", std::to_string(newest)}}).Ok());
    ExpectRecording("frames in memory");

    const TTDReply old = ramAt(first);
    ASSERT_TRUE(old.Ok()) << old.message;
    ExpectRecordingPaused("a frame read back from its file");
}

/// Idle with history and detached (a paused machine): the reads change neither the state nor the position
TEST_F(TTDStoreReads_Test, ReadsOfAStoppedAndOfABrowsedRecordingChangeNothing)
{
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    _emulator->RunNFrames(8, /*skipBreakpoints=*/true);
    ASSERT_TRUE(Run("stop").Ok());
    const std::vector<uint64_t> frames = {first, first + 5};
    const ttd::TTDTimePoint stopped = _ttd->CurrentPosition();
    const std::vector<std::string> idle = StoreReads(first, first + 7, frames);
    EXPECT_EQ(_ttd->GetState(), TTDSessionState::Idle);
    EXPECT_EQ(_ttd->CurrentPosition(), stopped);

    ASSERT_TRUE(Run("seek", {{"frame", std::to_string(first + 3)}, {"tinframe", "1000"}}).Ok());
    const ttd::TTDTimePoint browsed = _ttd->CurrentPosition();
    EXPECT_EQ(StoreReads(first, first + 7, frames), idle);
    EXPECT_EQ(_ttd->GetState(), TTDSessionState::Detached);
    EXPECT_EQ(_ttd->CurrentPosition(), browsed);
}

// Runtime: ~250 ms, boot-bound - the loop runs Pentagon frames at real speed: a park only exists with it running
/// A running, recording machine: after each store read it runs and records on (the park is the read's own, given
/// back); a point inside a frame parks it for good and pauses the recording (D8)
TEST_F(TTDStoreReads_Test, ARunningRecordingMachineRunsAndRecordsOnAfterStoreReads)
{
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    RunAsync(4);
    const std::string early = std::to_string(first + 1);
    const std::string later = std::to_string(first + 3);

    const TTDReply cpu = Run("memory-at", {{"space", "cpu"}, {"frame", later}, {"offset", "0x5B00"}, {"length", "2"}});
    ASSERT_TRUE(cpu.Ok()) << cpu.message;
    ExpectRecording("memory-at cpu");
    ExpectRunsOn("memory-at cpu");
    const TTDReply ram = Run("memory-at", {{"space", "ram5"}, {"frame", later}, {"offset", "0x1B00"}, {"length", "2"}});
    ASSERT_TRUE(ram.Ok()) << ram.message;
    EXPECT_EQ(Str(ram, "hex"), Str(cpu, "hex")) << "#5B00 is page 5's #1B00";
    ExpectRecording("memory-at ram5");
    ExpectRunsOn("memory-at ram5");
    const TTDReply diff = Run("memory-diff", {{"space", "ram5"}, {"from_frame", early}, {"to_frame", later}});
    ASSERT_TRUE(diff.Ok()) << diff.message;
    EXPECT_GT(diff.body.find("changed_bytes")->i, 0);
    ExpectRecording("memory-diff");
    ExpectRunsOn("memory-diff");
    const TTDReply series = Run("memory-series", {{"ranges", "0x5B00:2, ram5:0x1B00:2"}, {"from_frame", early},
                                                  {"to_frame", later}});
    ASSERT_TRUE(series.Ok()) << series.message;
    EXPECT_EQ(series.body.find("row_count")->i, 3);
    EXPECT_EQ(series.body.find("restored_frames")->i, 0);
    ExpectRecording("memory-series");
    ExpectRunsOn("memory-series");
    const size_t checkpoints = _ttd->GetSessionInfo().checkpointCount;

    const TTDReply inside = Run("memory-at", {{"space", "cpu"}, {"frame", later}, {"tinframe", "30000"},
                                              {"offset", "0x5B00"}, {"length", "2"}});
    ASSERT_TRUE(inside.Ok()) << inside.message;
    EXPECT_TRUE(_emulator->IsPaused()) << "a replay parks the machine and it stays parked";
    ExpectRecordingPaused("a point inside a frame");
    EXPECT_GE(_ttd->GetSessionInfo().checkpointCount, checkpoints) << "the frames recorded after the reads are kept";
}

// Runtime: ~200 ms, boot-bound (the loop at real speed, as above)
/// A stopped recording whose machine runs on: the store reads leave it running; the machine now and a point inside
/// a frame park it, as before
TEST_F(TTDStoreReads_Test, ARunningMachineWithAStoppedRecordingRunsOnAfterStoreReads)
{
    ASSERT_TRUE(Run("start").Ok());
    const uint64_t first = Frame();
    _emulator->RunNFrames(6, /*skipBreakpoints=*/true);
    ASSERT_TRUE(Run("stop").Ok());
    RunAsync(2);
    const std::string early = std::to_string(first + 1);
    const std::string later = std::to_string(first + 4);

    ASSERT_TRUE(Run("memory-at", {{"space", "cpu"}, {"frame", later}, {"offset", "0x5B00"}, {"length", "2"}}).Ok());
    ExpectRunsOn("memory-at cpu");
    ASSERT_TRUE(Run("memory-at", {{"space", "ram5"}, {"frame", later}, {"offset", "0x1B00"}, {"length", "2"}}).Ok());
    ExpectRunsOn("memory-at ram5");
    ASSERT_TRUE(Run("memory-diff", {{"space", "ram5"}, {"from_frame", early}, {"to_frame", later}}).Ok());
    ExpectRunsOn("memory-diff");
    ASSERT_TRUE(Run("memory-series", {{"ranges", "0x5B00:2"}, {"from_frame", early}, {"to_frame", later}}).Ok());
    ExpectRunsOn("memory-series");
    EXPECT_EQ(_ttd->GetState(), TTDSessionState::Idle);

    const TTDReply inside = Run("memory-at", {{"space", "cpu"}, {"frame", later}, {"tinframe", "30000"},
                                              {"offset", "0x5B00"}, {"length", "2"}});
    ASSERT_TRUE(inside.Ok()) << inside.message;
    EXPECT_TRUE(_emulator->IsPaused()) << "a replay parks the machine and it stays parked";
    EXPECT_EQ(_ttd->GetState(), TTDSessionState::Idle);
}
