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

#include <optional>
#include <sstream>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
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
