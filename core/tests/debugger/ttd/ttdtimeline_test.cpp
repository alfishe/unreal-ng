#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <string>

#include "debugger/ttd/bench/ttdtimelinefixture.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "debugger/ttd/ttdportjournal.h"
#include "emulator/emulatorcontext.h"

/// The TTD timeline summary (docs/inprogress/2026-10-31-ttd-timeline-summary): the synthetic phase program, recorded
/// once for the suite (4 cycles of 24-frame phases)
class TTDTimeline_Test : public ::testing::Test
{
protected:
    static constexpr unsigned kPhaseFrames = 24;
    static constexpr unsigned kCycles = 2;
    static std::unique_ptr<ttd::bench::TimelineSession> _s;

    static void SetUpTestSuite() { _s = ttd::bench::RecordTimelineProgram(kPhaseFrames, kCycles); }
    static void TearDownTestSuite() { _s.reset(); }

    /// Port records per frame of `journal`, for frames [from, to)
    static std::map<uint64_t, uint64_t> PerFrame(const ttd::TTDPortJournal& journal, uint64_t from, uint64_t to,
                                                 uint16_t mask = 0, uint16_t match = 0)
    {
        std::map<uint64_t, uint64_t> counts;
        ttd::TTDPortRecord r;
        for (uint64_t i = journal.FirstIndex(); i < journal.Size(); ++i)
            if (journal.Get(i, r) && r.frame >= from && r.frame < to && (r.port & mask) == match)
                ++counts[r.frame];
        return counts;
    }
};

std::unique_ptr<ttd::bench::TimelineSession> TTDTimeline_Test::_s;

/// The fixture itself: four phases a cycle, each as long as asked, each doing what it says, the keys journaled
TEST_F(TTDTimeline_Test, FixtureRunsItsPhases)
{
    ASSERT_TRUE(_s && _s->Ok());
    ASSERT_EQ(_s->phases.size(), 4u * kCycles);
    using Kind = ttd::bench::TimelinePhaseKind;
    for (size_t i = 0; i < _s->phases.size(); ++i)
    {
        const ttd::bench::TimelinePhase& p = _s->phases[i];
        EXPECT_EQ(static_cast<int>(p.kind), static_cast<int>(i % 4)) << "phase " << i;
        // The first phase starts with the recording; the last ends with it
        if (i > 0 && i + 1 < _s->phases.size())
        {
            EXPECT_GE(p.endFrame - p.firstFrame, kPhaseFrames - 1) << "phase " << i;
            EXPECT_LE(p.endFrame - p.firstFrame, kPhaseFrames + 1) << "phase " << i;
        }
    }

    const ttd::TTDPortJournal& reads = _s->ttd->GetPortReadJournal();
    const ttd::TTDPortJournal& writes = _s->ttd->GetPortWriteJournal();
    for (const ttd::bench::TimelinePhase& p : _s->phases)
    {
        // A whole frame inside the phase
        const uint64_t f = p.firstFrame + 2;
        const uint64_t ula = PerFrame(reads, f, f + 1, 0x00FF, 0x00FE)[f];
        const uint64_t ay = PerFrame(writes, f, f + 1, 0xC002, 0xC000)[f];
        switch (p.kind)
        {
            case Kind::Tape: EXPECT_GE(ula, 1000u); EXPECT_EQ(ay, 0u); break;
            case Kind::Burst: EXPECT_EQ(ula, 0u); EXPECT_EQ(ay, 0u); break;
            case Kind::Idle: EXPECT_EQ(ula, 8u); EXPECT_EQ(ay, 0u); break;
            case Kind::Game: EXPECT_EQ(ula, 0u); EXPECT_EQ(ay, 3u); break;
        }
    }

    ASSERT_EQ(_s->keys.size(), static_cast<size_t>(kCycles));
    size_t downs = 0;
    for (const ttd::TTDInputEvent& e : _s->ttd->GetInputJournal().Events())
        downs += e.kind == ttd::TTDInputKind::Key && e.pressed;
    EXPECT_EQ(downs, _s->keys.size());
}

namespace
{
/// The timeline phase that holds @p frame
const ttd::TTDTimelinePhase* PhaseAt(const ttd::TTDTimelineResult& r, uint64_t frame)
{
    for (const ttd::TTDTimelinePhase& p : r.phases)
        if (frame >= p.frameStart && frame <= p.frameEnd)
            return &p;
    return nullptr;
}
}  // namespace

/// Every bucket's counts are what the journals and the coverage index answer directly for its frames
TEST_F(TTDTimeline_Test, FactsMatchTheDirectQueries)
{
    ASSERT_TRUE(_s && _s->Ok());
    ttd::TTDTimelineOptions o;
    o.buckets = true;
    o.bucketSize = 8;
    o.limit = 500;
    const ttd::TTDTimelineResult r = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;
    ASSERT_FALSE(r.buckets.empty());
    EXPECT_EQ(r.fromFrame, _s->firstFrame);
    EXPECT_EQ(r.toFrame, _s->endFrame - 1);

    const ttd::TTDCoverageSummaryResult coverage =
        _s->ttd->QueryCoverageSummary(r.fromFrame, r.toFrame, std::nullopt, 8, 500);
    ASSERT_EQ(coverage.buckets.size(), r.buckets.size());

    const ttd::TTDPortJournal& reads = _s->ttd->GetPortReadJournal();
    const ttd::TTDPortJournal& writes = _s->ttd->GetPortWriteJournal();
    const auto sum = [](const std::map<uint64_t, uint64_t>& m) {
        uint64_t n = 0;
        for (const auto& [frame, count] : m)
            n += count;
        return n;
    };
    for (size_t b = 0; b < r.buckets.size(); ++b)
    {
        const ttd::TTDTimelineBucket& bucket = r.buckets[b];
        const uint64_t from = bucket.frameStart;
        const uint64_t to = bucket.frameEnd + 1;
        EXPECT_EQ(bucket.in, sum(PerFrame(reads, from, to))) << "bucket " << b;
        EXPECT_EQ(bucket.out, sum(PerFrame(writes, from, to))) << "bucket " << b;
        EXPECT_EQ(bucket.ulaReads, sum(PerFrame(reads, from, to, 0x0001, 0x0000))) << "bucket " << b;
        EXPECT_EQ(bucket.borderOuts, sum(PerFrame(writes, from, to, 0x0001, 0x0000))) << "bucket " << b;
        EXPECT_EQ(bucket.executed, coverage.buckets[b].executedDistinct) << "bucket " << b;
        EXPECT_EQ(bucket.written, coverage.buckets[b].writtenDistinct) << "bucket " << b;
        EXPECT_EQ(bucket.read, coverage.buckets[b].readDistinct) << "bucket " << b;
        uint32_t downs = 0;
        for (const ttd::TTDInputEvent& e : _s->ttd->GetInputJournal().Events())
            downs += e.kind == ttd::TTDInputKind::Key && e.pressed && e.time.frame >= from && e.time.frame < to;
        EXPECT_EQ(bucket.keyEvents, downs) << "bucket " << b;
    }
    EXPECT_GT(r.portRecords, 0u);
}

/// The phases are the script's: each boundary within half the tagging window of the real one, each with the tags of
/// what it did
TEST_F(TTDTimeline_Test, PhasesFollowTheScript)
{
    ASSERT_TRUE(_s && _s->Ok());
    ttd::TTDTimelineOptions o;
    o.minPhaseMs = 200;   // 10 frames; the script's phases are 24
    const ttd::TTDTimelineResult r = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;

    std::string got;
    for (const ttd::TTDTimelinePhase& p : r.phases)
    {
        got += std::to_string(p.frameStart) + "-" + std::to_string(p.frameEnd) + ":";
        for (const std::string& tag : ttd::TimelineTagNames(p.tags))
            got += tag + ",";
        got += " ";
    }
    ASSERT_EQ(r.phases.size(), _s->phases.size()) << got;

    using Kind = ttd::bench::TimelinePhaseKind;
    for (size_t i = 0; i < _s->phases.size(); ++i)
    {
        const ttd::bench::TimelinePhase& script = _s->phases[i];
        const ttd::TTDTimelinePhase& p = r.phases[i];
        if (i > 0)
            EXPECT_LE(p.frameStart > script.firstFrame ? p.frameStart - script.firstFrame
                                                       : script.firstFrame - p.frameStart,
                      5u)
                << "phase " << i << ": " << got;
        const uint32_t tags = PhaseAt(r, (script.firstFrame + script.endFrame) / 2)->tags;
        switch (script.kind)
        {
            case Kind::Tape:
                EXPECT_TRUE(tags & ttd::kTimelineTape) << got;
                EXPECT_FALSE(tags & (ttd::kTimelineHalt | ttd::kTimelineSound | ttd::kTimelineBurst)) << got;
                break;
            case Kind::Burst:
                EXPECT_TRUE(tags & ttd::kTimelineBurst) << got;
                EXPECT_FALSE(tags & (ttd::kTimelineTape | ttd::kTimelineHalt | ttd::kTimelineSound)) << got;
                break;
            case Kind::Idle:
                EXPECT_TRUE(tags & ttd::kTimelineHalt) << got;
                EXPECT_FALSE(tags & (ttd::kTimelineTape | ttd::kTimelineSound | ttd::kTimelineBurst)) << got;
                break;
            case Kind::Game:
                EXPECT_EQ(tags & (ttd::kTimelineSound | ttd::kTimelineScreen | ttd::kTimelinePaging),
                          ttd::kTimelineSound | ttd::kTimelineScreen | ttd::kTimelinePaging)
                    << got;
                EXPECT_FALSE(tags & (ttd::kTimelineTape | ttd::kTimelineHalt | ttd::kTimelineBurst)) << got;
                break;
        }
    }
    // The keys pressed in the idle phases land in them
    for (const ttd::bench::TimelineKey& key : _s->keys)
    {
        const ttd::TTDTimelinePhase* p = PhaseAt(r, key.downFrame);
        ASSERT_NE(p, nullptr);
        std::string keys;
        for (uint8_t k : p->facts.keys)
            keys += std::to_string(k) + " ";
        std::string journal;
        for (const ttd::TTDInputEvent& e : _s->ttd->GetInputJournal().Events())
            journal += std::to_string(e.time.frame) + ":" + std::to_string(e.key) + (e.pressed ? "+ " : "- ");
        EXPECT_TRUE(std::find(p->facts.keys.begin(), p->facts.keys.end(), key.key) != p->facts.keys.end())
            << "key " << key.key << " down " << key.downFrame << ", phase keys " << keys << "events " << p->facts.keyEvents
            << ", journal " << journal << "; " << got;
    }
}

/// The phases are cut per frame: the overview buckets, their count or size, change nothing in them
TEST_F(TTDTimeline_Test, PhasesDoNotDependOnTheBuckets)
{
    ASSERT_TRUE(_s && _s->Ok());
    const auto cut = [](const ttd::TTDTimelineResult& r) {
        std::string text;
        for (const ttd::TTDTimelinePhase& p : r.phases)
            text += std::to_string(p.frameStart) + "-" + std::to_string(p.frameEnd) + ":" + std::to_string(p.tags) +
                    "/" + std::to_string(p.facts.in) + " ";
        return text;
    };
    ttd::TTDTimelineOptions o;
    const ttd::TTDTimelineResult plain = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(plain.error.empty()) << plain.error;
    EXPECT_TRUE(plain.buckets.empty());
    EXPECT_EQ(plain.bucketSize, 0u);
    o.buckets = true;
    for (const size_t limit : {size_t{1}, size_t{5}, size_t{100}, size_t{500}})
    {
        o.limit = limit;
        const ttd::TTDTimelineResult r = _s->ttd->QueryTimeline(o);
        ASSERT_TRUE(r.error.empty()) << r.error;
        EXPECT_LE(r.buckets.size(), limit);
        EXPECT_EQ(cut(r), cut(plain)) << "limit " << limit;
    }
}

/// What the session does not hold is named, not reported as zero
TEST_F(TTDTimeline_Test, MissingFactsAreNamed)
{
    ASSERT_TRUE(_s && _s->Ok());
    ttd::TTDTimelineOptions o;
    o.ports = false;
    const ttd::TTDTimelineResult r = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;
    std::set<std::string> missing;
    for (const ttd::TTDTimelineMissing& m : r.missing)
    {
        missing.insert(m.fact);
        EXPECT_FALSE(m.reason.empty()) << m.fact;
    }
    EXPECT_TRUE(missing.count("ports"));
    EXPECT_TRUE(missing.count("writes"));      // the fixture records without the write journal
    EXPECT_FALSE(missing.count("screen"));     // a Pentagon
    EXPECT_FALSE(missing.count("coverage"));
    EXPECT_TRUE(r.devices.empty());
    EXPECT_EQ(r.portRecords, 0u);
    for (const ttd::TTDTimelineBucket& b : r.buckets)
        EXPECT_EQ(b.ulaReads, 0u);
}

/// The window is clamped to the history, an explicit bucket size cuts it at `limit` buckets and says so
TEST_F(TTDTimeline_Test, WindowAndBucketOptions)
{
    ASSERT_TRUE(_s && _s->Ok());
    ttd::TTDTimelineOptions o;
    o.buckets = true;
    o.limit = 10;
    ttd::TTDTimelineResult r = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_LE(r.buckets.size(), 10u);
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.buckets.front().frameStart, _s->firstFrame);
    EXPECT_EQ(r.buckets.back().frameEnd, _s->endFrame - 1);

    o = {};
    o.buckets = true;
    o.fromFrame = _s->firstFrame + 10;
    o.bucketSize = 5;
    o.limit = 3;
    r = _s->ttd->QueryTimeline(o);
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(r.truncated);
    ASSERT_EQ(r.buckets.size(), 3u);
    EXPECT_EQ(r.fromFrame, _s->firstFrame + 10);
    EXPECT_EQ(r.toFrame, _s->firstFrame + 24);
    EXPECT_EQ(r.buckets[2].frameStart, _s->firstFrame + 20);

    o = {};
    o.fromFrame = _s->endFrame + 100;
    r = _s->ttd->QueryTimeline(o);
    EXPECT_FALSE(r.error.empty());
}

/// The query reads: the session and the machine are where they were
TEST_F(TTDTimeline_Test, QueryLeavesTheMachineAlone)
{
    ASSERT_TRUE(_s && _s->Ok());
    const ttd::TTDTimePoint end = _s->ttd->SessionEndPosition();
    const uint64_t reads = _s->ttd->GetPortReadJournal().Size();
    const size_t checkpoints = _s->ttd->GetEngine().CheckpointCount();
    const uint64_t frame = _s->context->emulatorState.frame_counter;
    const ttd::TTDTimelineResult r = _s->ttd->QueryTimeline({});
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_TRUE(_s->ttd->SessionEndPosition() == end);
    EXPECT_EQ(_s->ttd->GetPortReadJournal().Size(), reads);
    EXPECT_EQ(_s->ttd->GetEngine().CheckpointCount(), checkpoints);
    EXPECT_EQ(_s->context->emulatorState.frame_counter, frame);
}
