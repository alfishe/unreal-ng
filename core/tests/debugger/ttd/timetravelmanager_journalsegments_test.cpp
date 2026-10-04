/// @file timetravelmanager_journalsegments_test.cpp
/// @brief The write journal on demand (D40): off by default, switched on and
/// off at any moment of a recording (also inside a frame), each span a
/// segment kept with the session and its file. "Who wrote this last" gives
/// the same answer wherever the journal is missing: outside the segments the
/// search uses the coverage index and replays one frame.
///
/// Two identical machines run the same program: one records the whole
/// session with the journal, the other with segments only. Both boot a
/// Pentagon and replay frames: slower than the 50 ms guideline.

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <sstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
struct Machine
{
    Emulator* emulator = nullptr;
    ttd::TimeTravelManager* ttd = nullptr;

    Machine()
    {
        emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        if (!emulator)
            return;
        emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        emulator->GetContext()->pMemory->UpdateFeatureCache();
        ttd = emulator->GetContext()->pTimeTravelManager;
        // DI; loop: INC A; LD (#C000),A; OUT (#FE),A; JP loop - a write every 38 T-states
        Memory* memory = emulator->GetContext()->pMemory;
        const uint8_t program[] = {0xF3, 0x3C, 0x32, 0x00, 0xC0, 0xD3, 0xFE, 0xC3, 0x01, 0x80};
        for (uint16_t i = 0; i < sizeof(program); ++i)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
        emulator->GetZ80State()->pc = 0x8000;
        emulator->GetZ80State()->sp = 0xFF00;
    }
    ~Machine()
    {
        if (emulator)
            EmulatorTestHelper::CleanupEmulator(emulator);
    }
    void Frames(unsigned n) { emulator->RunNFrames(n, /*skipBreakpoints=*/true); }
    void TStates(unsigned n) { emulator->RunTStates(n, /*skipBreakpoints=*/true); }

    /// The newest write to #C000 at or before @p t
    std::optional<ttd::TTDSearchResult> LastWrite(uint64_t t)
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = 0xC000;
        q.access = ttd::TTDAccessType::Write;
        q.beforeGlobalT = t;
        return ttd->FindLastAccess(q);
    }
};

/// Query times across the whole session, a few thousand T-states apart
std::vector<uint64_t> QueryTimes(ttd::TimeTravelManager& ttd)
{
    std::vector<uint64_t> out;
    const uint64_t from = ttd.GlobalT(ttd.GetCheckpoint(0)->time);
    const uint64_t to = ttd.GlobalT(ttd.SessionEndPosition());
    for (uint64_t t = from + 1000; t < to; t += 7919)
        out.push_back(t);
    return out;
}

/// Every query answers the same on both sessions
void ExpectSameAnswers(Machine& whole, Machine& partial)
{
    const std::vector<uint64_t> times = QueryTimes(*whole.ttd);
    ASSERT_GT(times.size(), 10u);
    for (uint64_t t : times)
    {
        SCOPED_TRACE("t " + std::to_string(t));
        const auto a = whole.LastWrite(t);
        const auto b = partial.LastWrite(t);
        ASSERT_EQ(a.has_value(), b.has_value());
        if (!a)
            continue;
        EXPECT_TRUE(a->time == b->time);
        EXPECT_EQ(a->value, b->value);
        EXPECT_EQ(a->pc, b->pc);
        EXPECT_EQ(a->addr, 0xC000);
    }
}
}  // namespace

TEST(TimeTravelManager_JournalSegments_Test, OffByDefault_TheSearchStillAnswers)
{
    Machine whole, none;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(none.ttd, nullptr);
    EXPECT_FALSE(none.ttd->GetEnableWriteJournal()) << "recorded on demand";

    whole.ttd->SetEnableWriteJournal(true);
    for (Machine* m : {&whole, &none})
    {
        ASSERT_TRUE(m->ttd->StartRecording());
        m->Frames(3);
        m->ttd->StopRecording();
    }
    const ttd::TTDSessionInfo w = whole.ttd->GetSessionInfo();
    ASSERT_EQ(w.writeJournalSegments.size(), 1u);
    EXPECT_TRUE(w.writeJournalComplete) << "one segment over the whole session";
    EXPECT_TRUE(none.ttd->GetSessionInfo().writeJournalSegments.empty());
    ExpectSameAnswers(whole, none);
}

TEST(TimeTravelManager_JournalSegments_Test, SwitchingDuringARecordingMakesSegments_SameAnswers)
{
    Machine whole, partial;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(partial.ttd, nullptr);
    whole.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(whole.ttd->StartRecording());
    ASSERT_TRUE(partial.ttd->StartRecording());

    // The same run on both; the partial one switches the journal inside frames
    auto both = [&](auto&& step) { step(whole); step(partial); };
    both([](Machine& m) { m.Frames(1); });
    both([](Machine& m) { m.TStates(10000); });
    const uint64_t on1 = partial.ttd->GlobalT(partial.ttd->CurrentPosition());
    partial.ttd->SetEnableWriteJournal(true);
    both([](Machine& m) { m.Frames(1); });
    both([](Machine& m) { m.TStates(20000); });
    const uint64_t off1 = partial.ttd->GlobalT(partial.ttd->CurrentPosition());
    partial.ttd->SetEnableWriteJournal(false);
    both([](Machine& m) { m.Frames(2); });
    const uint64_t on2 = partial.ttd->GlobalT(partial.ttd->CurrentPosition());
    partial.ttd->SetEnableWriteJournal(true);
    both([](Machine& m) { m.Frames(2); });
    whole.ttd->StopRecording();
    partial.ttd->StopRecording();

    const std::vector<ttd::TTDJournalSegment> segments = partial.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].from, on1) << "starts at the instruction it was switched on at";
    EXPECT_EQ(segments[0].to, off1);
    EXPECT_EQ(segments[1].from, on2);
    EXPECT_FALSE(partial.ttd->GetSessionInfo().writeJournalComplete);
    ExpectSameAnswers(whole, partial);
}

TEST(TimeTravelManager_JournalSegments_Test, ARunBetweenStopAndLiveResumeIsNotCovered)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    m.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(2);
    m.ttd->StopRecording();
    const uint64_t stop = m.ttd->GlobalT(m.ttd->CurrentPosition());
    m.TStates(200);   // same frame: a live resume stays possible
    const uint64_t resume = m.ttd->GlobalT(m.ttd->CurrentPosition());
    ASSERT_TRUE(m.ttd->ResumeRecordingLive());
    m.Frames(1);
    m.ttd->StopRecording();

    const std::vector<ttd::TTDJournalSegment> segments = m.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(segments.size(), 2u);
    EXPECT_EQ(segments[0].to, stop);
    EXPECT_EQ(segments[1].from, resume) << "the writes made while stopped are in no segment";
}

TEST(TimeTravelManager_JournalSegments_Test, SegmentsTravelWithTheSessionFile)
{
    Machine whole, partial;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(partial.ttd, nullptr);
    whole.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(whole.ttd->StartRecording());
    ASSERT_TRUE(partial.ttd->StartRecording());
    whole.Frames(1);
    partial.Frames(1);
    partial.ttd->SetEnableWriteJournal(true);
    whole.Frames(2);
    partial.Frames(2);
    whole.ttd->StopRecording();
    partial.ttd->StopRecording();
    const std::vector<ttd::TTDJournalSegment> recorded = partial.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(recorded.size(), 1u);

    std::stringstream file;
    std::string err;
    ASSERT_TRUE(partial.ttd->SerializeSession(file, err)) << err;
    Machine loaded;
    ASSERT_NE(loaded.ttd, nullptr);
    ASSERT_TRUE(loaded.ttd->DeserializeSession(file, err)) << err;
    EXPECT_EQ(loaded.ttd->GetSessionInfo().writeJournalSegments, recorded);
    ExpectSameAnswers(whole, loaded);
}

/// Switching the journal off with no session frees the memory it reserved;
/// with a retained session the journal still answers for it
TEST(TimeTravelManager_JournalSegments_Test, SwitchingOffWithNoSessionFreesTheJournal)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    m.ttd->SetEnableWriteJournal(true);
    m.ttd->UpdateFeatureCache();
    ASSERT_NE(m.ttd->GetWriteJournal(), nullptr) << "precondition: the feature pre-allocates the journal";
    m.ttd->SetEnableWriteJournal(false);
    EXPECT_EQ(m.ttd->GetWriteJournal(), nullptr);

    m.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(1);
    m.ttd->StopRecording();
    m.ttd->SetEnableWriteJournal(false);
    EXPECT_NE(m.ttd->GetWriteJournal(), nullptr) << "the retained session's journal was freed";
}


/// Inside a segment the answer comes from the journal and the machine stays
/// where it is; outside, the search replays the frame and lands on the write
TEST(TimeTravelManager_JournalSegments_Test, InsideASegmentTheJournalAnswers)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(2);
    m.ttd->SetEnableWriteJournal(true);
    m.Frames(2);
    m.ttd->StopRecording();
    const ttd::TTDJournalSegment seg = m.ttd->GetSessionInfo().writeJournalSegments.at(0);

    const ttd::TTDTimePoint before = m.ttd->CurrentPosition();
    const auto inside = m.LastWrite(seg.to - 100);
    ASSERT_TRUE(inside.has_value());
    EXPECT_TRUE(m.ttd->CurrentPosition() == before) << "answered from the journal, nothing replayed";

    const auto outside = m.LastWrite(seg.from - 100);
    ASSERT_TRUE(outside.has_value());
    EXPECT_TRUE(m.ttd->CurrentPosition() == outside->time) << "answered by replay, the machine is at the write";
}

/// Recording again from an earlier point drops the journal after it; the
/// segment continues from there
TEST(TimeTravelManager_JournalSegments_Test, RecordingAgainFromAnEarlierPointCutsTheSegments)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    m.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(4);
    m.ttd->StopRecording();
    const ttd::TTDTimePoint cut = m.ttd->GetCheckpoint(2)->time;
    ASSERT_TRUE(m.ttd->SeekTo(cut));
    ASSERT_TRUE(m.ttd->ResumeRecordingFrom(cut));
    const uint64_t cutT = m.ttd->GlobalT(m.ttd->CurrentPosition());
    EXPECT_LE(m.ttd->GetWriteJournal()->NewestGlobalT(), cutT) << "the old future's writes are gone";
    m.Frames(1);
    m.ttd->StopRecording();

    const std::vector<ttd::TTDJournalSegment> segments = m.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(segments.size(), 1u) << "one span: up to the cut, then on from it";
    EXPECT_TRUE(m.ttd->GetSessionInfo().writeJournalComplete);
}

// ===========================================================================
// Building the journal by replay (BuildWriteJournal, J2)
// ===========================================================================

namespace
{
/// The journal's records with @p from < globalT <= @p to
std::vector<ttd::TTDWriteRecord> Records(const ttd::TTDWriteJournal& j, uint64_t from, uint64_t to)
{
    std::vector<ttd::TTDWriteRecord> out;
    for (uint64_t seq = j.SeqTail(); seq < j.SeqHead(); ++seq)
        if (j.RecordAt(seq).globalT > from && j.RecordAt(seq).globalT <= to)
            out.push_back(j.RecordAt(seq));
    return out;
}

void ExpectSameRecords(const std::vector<ttd::TTDWriteRecord>& a, const std::vector<ttd::TTDWriteRecord>& b)
{
    ASSERT_EQ(a.size(), b.size());
    for (size_t k = 0; k < a.size(); ++k)
    {
        ASSERT_EQ(uint64_t(a[k].globalT), uint64_t(b[k].globalT)) << "record " << k;
        ASSERT_EQ(a[k].addr, b[k].addr) << "record " << k;
        ASSERT_EQ(a[k].value, b[k].value) << "record " << k;
        ASSERT_EQ(a[k].m1pc, b[k].m1pc) << "record " << k;
        ASSERT_EQ(a[k].physPage, b[k].physPage) << "record " << k;
    }
}
}  // namespace

TEST(TimeTravelManager_JournalSegments_Test, BuildingTheWholeSessionEqualsTheRecordedJournal)
{
    Machine whole, built;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(built.ttd, nullptr);
    whole.ttd->SetEnableWriteJournal(true);
    for (Machine* m : {&whole, &built})
    {
        ASSERT_TRUE(m->ttd->StartRecording());
        m->Frames(4);
        m->ttd->StopRecording();
    }
    ASSERT_TRUE(built.ttd->SeekTo({built.ttd->GetCheckpoint(1)->time.frame, 3000}));
    const ttd::TTDTimePoint home = built.ttd->CurrentPosition();

    uint64_t lastDone = 0, calls = 0;
    const ttd::TTDJournalBuildResult r = built.ttd->BuildWriteJournal(0, UINT64_MAX, [&](uint64_t done, uint64_t total) {
        EXPECT_LE(done, total);
        lastDone = done;
        ++calls;
        return true;
    });
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_FALSE(r.cancelled);
    EXPECT_EQ(r.framesBuilt, built.ttd->GetCheckpointCount() - 1) << "every frame but the last";
    EXPECT_EQ(r.framesRefused, 0u);
    EXPECT_GT(r.records, 0u);
    EXPECT_EQ(lastDone, r.framesBuilt) << "the last call reports the end";
    EXPECT_EQ(calls, r.framesBuilt + 1);
    EXPECT_TRUE(built.ttd->CurrentPosition() == home) << "the machine is back where it stood";

    const ttd::TTDSessionInfo info = built.ttd->GetSessionInfo();
    ASSERT_EQ(info.writeJournalSegments.size(), 1u);
    EXPECT_TRUE(info.writeJournalComplete) << "the whole session is covered now";
    const ttd::TTDJournalSegment seg = info.writeJournalSegments[0];
    ExpectSameRecords(Records(*built.ttd->GetWriteJournal(), seg.from, seg.to),
                      Records(*whole.ttd->GetWriteJournal(), seg.from, seg.to));
    ExpectSameAnswers(whole, built);

    const ttd::TTDJournalBuildResult again = built.ttd->BuildWriteJournal(0, UINT64_MAX);
    ASSERT_TRUE(again.ok);
    EXPECT_EQ(again.framesBuilt, 0u) << "covered frames are left as they are";
    EXPECT_EQ(again.framesCovered, r.framesBuilt);
}

TEST(TimeTravelManager_JournalSegments_Test, ABuiltSpanJoinsTheRecordedSegment)
{
    Machine whole, partial;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(partial.ttd, nullptr);
    whole.ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(whole.ttd->StartRecording());
    ASSERT_TRUE(partial.ttd->StartRecording());
    whole.Frames(3);
    partial.Frames(3);
    partial.TStates(5000);
    whole.TStates(5000);
    partial.ttd->SetEnableWriteJournal(true);   // a segment starting inside frame 4
    whole.Frames(3);
    partial.Frames(3);
    whole.ttd->StopRecording();
    partial.ttd->StopRecording();
    const ttd::TTDJournalSegment recorded = partial.ttd->GetSessionInfo().writeJournalSegments.at(0);

    // Build from the second frame up to inside the segment: the partly
    // covered frame is replaced by its full writes, and the spans join
    const uint64_t from = partial.ttd->GlobalT(partial.ttd->GetCheckpoint(1)->time);
    const ttd::TTDJournalBuildResult r = partial.ttd->BuildWriteJournal(from, recorded.from + 1);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_GE(r.framesBuilt, 2u);
    const std::vector<ttd::TTDJournalSegment> segments = partial.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(segments.size(), 1u) << "the built frames and the recorded segment are one span";
    EXPECT_LT(segments[0].from, recorded.from);
    EXPECT_EQ(segments[0].to, recorded.to);
    ExpectSameRecords(Records(*partial.ttd->GetWriteJournal(), segments[0].from, segments[0].to),
                      Records(*whole.ttd->GetWriteJournal(), segments[0].from, segments[0].to));
    ExpectSameAnswers(whole, partial);
}

TEST(TimeTravelManager_JournalSegments_Test, ACancelledBuildKeepsWhatItBuilt)
{
    Machine whole, built;
    ASSERT_NE(whole.ttd, nullptr);
    ASSERT_NE(built.ttd, nullptr);
    whole.ttd->SetEnableWriteJournal(true);
    for (Machine* m : {&whole, &built})
    {
        ASSERT_TRUE(m->ttd->StartRecording());
        m->Frames(5);
        m->ttd->StopRecording();
    }
    const ttd::TTDJournalBuildResult r =
        built.ttd->BuildWriteJournal(0, UINT64_MAX, [](uint64_t done, uint64_t) { return done < 2; });
    ASSERT_TRUE(r.ok);
    EXPECT_TRUE(r.cancelled);
    EXPECT_EQ(r.framesBuilt, 2u);
    ASSERT_EQ(built.ttd->GetSessionInfo().writeJournalSegments.size(), 1u);
    EXPECT_FALSE(built.ttd->GetSessionInfo().writeJournalComplete);
    ExpectSameAnswers(whole, built);
}

TEST(TimeTravelManager_JournalSegments_Test, BuildingIsRefusedWhileRecording_AndSkipsFramesItCannotReplay)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(2);
    const ttd::TTDJournalBuildResult refused = m.ttd->BuildWriteJournal(0, UINT64_MAX);
    EXPECT_FALSE(refused.ok);
    EXPECT_NE(refused.error.find("recording"), std::string::npos) << refused.error;

    m.TStates(3000);
    m.ttd->RecordExternalEvent(ttd::TTDExternalEventKind::Other, "test marker");   // a v1 barrier: no data
    m.Frames(3);
    m.ttd->StopRecording();
    const ttd::TTDJournalBuildResult r = m.ttd->BuildWriteJournal(0, UINT64_MAX);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.framesRefused, 1u) << "the frame holding the marker";
    EXPECT_EQ(r.framesBuilt, m.ttd->GetCheckpointCount() - 2);
    EXPECT_EQ(m.ttd->GetSessionInfo().writeJournalSegments.size(), 2u) << "around the refused frame";
}

// ===========================================================================
// What the automation surfaces call (J3): CLI, WebAPI, MCP, Lua and Python
// switch the journal and build it through these
// ===========================================================================

TEST(TimeTravelManager_JournalSegments_Test, SwitchingFromAnotherThreadWhileTheMachineRuns)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.emulator->StartAsync();
    const uint64_t& frame = m.emulator->GetContext()->emulatorState.frame_counter;
    ASSERT_TRUE(TestWait::For([&] { return m.emulator->IsRunning() && frame > 3; }, std::chrono::seconds(10)));

    // This thread is not the emulation thread: the switch pauses the machine,
    // switches at the instruction it stopped on and resumes it
    ASSERT_TRUE(m.ttd->SwitchWriteJournal(true));
    const uint64_t on = frame;
    ASSERT_TRUE(TestWait::For([&] { return frame > on + 3; }, std::chrono::seconds(10)));
    ASSERT_TRUE(m.ttd->SwitchWriteJournal(false));
    m.emulator->Stop();
    m.ttd->StopRecording();

    const ttd::TTDSessionInfo info = m.ttd->GetSessionInfo();
    ASSERT_EQ(info.writeJournalSegments.size(), 1u);
    ASSERT_EQ(info.writeJournalSpans.size(), 1u);
    EXPECT_GE(info.writeJournalSpans[0].first.frame, on - 1);
    EXPECT_GT(info.writeJournalRecords, 0u) << "the program's writes in the span";
}

TEST(TimeTravelManager_JournalSegments_Test, ABuildReportsProgressAndStopsFromAnotherThread)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(12);
    m.ttd->StopRecording();
    EXPECT_FALSE(m.ttd->GetJournalBuildState().active);

    std::atomic<bool> sawProgress{false};
    std::atomic<bool> asked{false};
    std::thread observer([&] {
        // A surface polls the state and asks to stop (WebAPI .../build/cancel)
        TestWait::For([&] {
            const auto state = m.ttd->GetJournalBuildState();
            return state.active && state.done >= 3;
        }, std::chrono::seconds(10));
        const auto state = m.ttd->GetJournalBuildState();
        sawProgress = state.active && state.total > state.done;
        m.ttd->CancelJournalBuild();
        asked = true;
    });
    // The build waits at its third frame until the observer has seen it and
    // asked to stop: the two threads meet there whatever the scheduler does
    // (yielding alone let the build finish first on a loaded host)
    const ttd::TTDJournalBuildResult r = m.ttd->BuildWriteJournalFrames(0, UINT64_MAX, [&](uint64_t done, uint64_t) {
        if (done == 3)
            TestWait::For([&] { return asked.load(); }, std::chrono::seconds(10));
        return true;
    });
    observer.join();
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(sawProgress.load());
    EXPECT_TRUE(r.cancelled);
    EXPECT_GE(r.framesBuilt, 3u);
    EXPECT_LT(r.framesBuilt, m.ttd->GetCheckpointCount() - 1);
    EXPECT_FALSE(m.ttd->GetJournalBuildState().active) << "over";
}

TEST(TimeTravelManager_JournalSegments_Test, BuildingByFrameNumbers)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(10);
    m.ttd->StopRecording();
    const uint64_t first = m.ttd->GetCheckpoint(3)->time.frame;
    const ttd::TTDJournalBuildResult r = m.ttd->BuildWriteJournalFrames(first, first + 2);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.framesBuilt, 3u) << "frames first..first+2, both included";
    const ttd::TTDSessionInfo info = m.ttd->GetSessionInfo();
    ASSERT_EQ(info.writeJournalSpans.size(), 1u);
    EXPECT_EQ(info.writeJournalSpans[0].first.frame, first);
    EXPECT_EQ(info.writeJournalSpans[0].second.frame, first + 3) << "the third frame ends where the next begins";
}


// ===========================================================================
// The engine keeps the same journal (J6)
// ===========================================================================

TEST(TimeTravelManager_JournalSegments_Test, TheShadowEngineKeepsTheSameJournalAndSegments)
{
    Machine m;
    ASSERT_NE(m.ttd, nullptr);
    ttd::TimeTravelEngine engine;
    m.ttd->SetShadowEngine(&engine);
    ASSERT_TRUE(m.ttd->StartRecording());
    m.Frames(2);
    m.TStates(9000);
    m.ttd->SetEnableWriteJournal(true);
    m.Frames(3);
    m.TStates(4000);
    m.ttd->SetEnableWriteJournal(false);
    m.Frames(2);
    m.ttd->SetEnableWriteJournal(true);
    m.Frames(2);
    m.ttd->StopRecording();
    m.ttd->SetShadowEngine(nullptr);

    // The engine takes the journal at each capture: its records are v1's up
    // to the last checkpoint; the writes after it (to the stop) are v1's alone
    const ttd::TTDWriteJournal& v1 = *m.ttd->GetWriteJournal();
    std::vector<ttd::TTDWriteRecord> engineRecords;
    engine.Writes().ForEach([&](const ttd::TTDWriteRecord& r) { engineRecords.push_back(r); });
    const std::vector<ttd::TTDWriteRecord> v1Records = Records(v1, 0, UINT64_MAX);
    ASSERT_GT(engineRecords.size(), 0u);
    ASSERT_LE(engineRecords.size(), v1Records.size());
    ExpectSameRecords(engineRecords,
                      std::vector<ttd::TTDWriteRecord>(v1Records.begin(), v1Records.begin() + engineRecords.size()));
    const uint64_t lastCheckpoint = m.ttd->GlobalT(m.ttd->GetCheckpoint(m.ttd->GetCheckpointCount() - 1)->time);
    for (size_t k = engineRecords.size(); k < v1Records.size(); ++k)
        ASSERT_GT(uint64_t(v1Records[k].globalT), lastCheckpoint) << "only writes after the last capture are missing";

    // The segments as v1 had them at its last capture: the open one closed there
    const std::vector<ttd::TTDJournalSegment> v1Segments = m.ttd->GetSessionInfo().writeJournalSegments;
    ASSERT_EQ(engine.Writes().Segments().size(), v1Segments.size());
    EXPECT_EQ(engine.Writes().Segments()[0], v1Segments[0]);
    EXPECT_LT(engine.HeapBreakdown().writeJournal, v1.Size() * sizeof(ttd::TTDWriteRecord))
        << "compressed blocks, not v1's raw ring";
}
