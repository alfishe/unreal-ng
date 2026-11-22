/// @file ttdreversesearchindex_test.cpp
/// @brief The reverse-search-index leftovers (docs/inprogress/2026-08-20-ttd-reverse-search-index, section 5) on
/// TTD v2: wide and wrapping ranges pruned by the coverage index (RSI-wide), large reverse breakpoint sets with one
/// decode per frame (RSI-bpset), conditions in reverse continue (RSI-cond), the page bound of every model
/// (RSI-models).
///
/// Every pruning check is differential: the same query with the index and without it must give the same answer,
/// on machines with different page layouts (Pentagon 1024, the 4 MB TS-Conf and Sprinter, the Next's 8K slots, the
/// 48K's pages 0 / 2 / 5). The DISABLED_ tests print the timings quoted in the RSI README (run them with
/// --gtest_also_run_disabled_tests).

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testtier.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "debugger/ttd/ttdcoverageindex.h"
#include "debugger/ttd/ttdprobe.h"
#include "emulator/config.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

struct ModelCase
{
    const char* model;
    uint32_t ramsize;   ///< 0: the model's default
};

std::string ModelCaseName(const ::testing::TestParamInfo<ModelCase>& info)
{
    std::string name = info.param.model;
    if (info.param.ramsize)
        name += std::to_string(info.param.ramsize);
    std::replace_if(name.begin(), name.end(), [](char c) { return !std::isalnum(static_cast<unsigned char>(c)); }, '_');
    return name;
}

/// A recorded session on one model, with the helpers every differential check here needs
class TTDReverseSearchSession
{
public:
    Emulator* emulator = nullptr;
    EmulatorContext* context = nullptr;
    ttd::TimeTravelController* ttd = nullptr;

    bool Create(const ModelCase& m)
    {
        const uint32_t ram = m.ramsize;
        emulator = EmulatorTestHelper::CreateStandardEmulator(m.model, LoggerLevel::LogError, RamPowerOn::Random,
                                                              [ram](CONFIG& c) {
                                                                  if (ram)
                                                                      c.ramsize = ram;
                                                              });
        if (!emulator)
            return false;
        context = emulator->GetContext();
        ttd = context->pTimeTravelController;
        FeatureManager* fm = emulator->GetFeatureManager();
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        context->pMemory->UpdateFeatureCache();
        emulator->EnableTurboMode();   // nothing here looks at pixels
        return ttd != nullptr;
    }

    ~TTDReverseSearchSession() { EmulatorTestHelper::CleanupEmulator(emulator); }

    bool Record(unsigned frames)
    {
        if (!ttd->StartRecording())
            return false;
        emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
        ttd->StopRecording();
        return ttd->GetCheckpointCount() > 1;
    }

    void Park() { ASSERT_TRUE(ttd->SeekTo(ttd->SessionEndPosition())); }

    std::string Find(const ttd::TTDSearchQuery& q)
    {
        Park();
        const std::optional<ttd::TTDSearchResult> r = ttd->FindLastAccess(q);
        if (!r)
            return "none";
        return std::to_string(r->time.frame) + ":" + std::to_string(r->time.tInFrame) + " pc " +
               std::to_string(r->pc) + " addr " + std::to_string(r->addr) + " page " + std::to_string(r->physPage) +
               " value " + std::to_string(r->value);
    }

    /// The query with the index and without it: the answers must be the same
    void ExpectSameWithAndWithoutIndex(const ttd::TTDSearchQuery& q, const std::string& what)
    {
        ttd->SetEnableCoverageIndex(true);
        const std::string with = Find(q);
        ttd->SetEnableCoverageIndex(false);
        const std::string without = Find(q);
        ttd->SetEnableCoverageIndex(true);
        EXPECT_EQ(with, without) << what;
    }

    std::string Reverse(const ttd::TTDReverseContinueQuery& q)
    {
        Park();
        const ttd::TTDReverseContinueResult r = ttd->ReverseContinue(q);
        std::string text = r.error.empty() ? std::string() : "error " + r.error;
        text += r.matched ? " matched" : " none";
        for (const ttd::TTDReverseHit& h : r.hits)
            text += " " + std::to_string(h.globalT) + "@" + std::to_string(h.pc);
        return text;
    }

    void ExpectSameReverse(const ttd::TTDReverseContinueQuery& q, const std::string& what)
    {
        ttd->SetEnableCoverageIndex(true);
        const std::string with = Reverse(q);
        ttd->SetEnableCoverageIndex(false);
        const std::string without = Reverse(q);
        ttd->SetEnableCoverageIndex(true);
        EXPECT_EQ(with, without) << what;
    }

    /// The RAM pages the session's coverage saw (any kind), at most @p most
    std::vector<ttd::PhysPage> PagesSeen(size_t most)
    {
        std::set<uint32_t> fields;
        const ttd::TTDCoverageIndex& index = ttd->GetCoverageIndex();
        uint64_t first = 0, last = 0;
        std::vector<ttd::TTDCoverageKey> keys;
        for (ttd::TTDCoverageKind kind : {ttd::TTDCoverageKind::Read, ttd::TTDCoverageKind::Written})
            if (index.CoveredRange(kind, first, last))
                for (uint64_t f = first; f <= last; ++f)
                    if (index.GetFrameKeys(kind, f, keys))
                        for (const ttd::TTDCoverageKey k : keys)
                            if ((k >> 14) < 0x100)
                                fields.insert(k >> 14);
        std::vector<ttd::PhysPage> pages;
        for (const uint32_t f : fields)
            if (pages.size() < most)
                pages.push_back(static_cast<ttd::PhysPage>(f));
        // The highest pages too: the ones a wrong bound or key would lose first
        if (fields.size() > most && !pages.empty())
            pages.back() = static_cast<ttd::PhysPage>(*fields.rbegin());
        return pages;
    }

    /// PCs that ran in the session (the PC at a few checkpoints)
    std::vector<uint16_t> SomeExecutedPcs()
    {
        std::vector<uint16_t> pcs;
        const size_t n = ttd->GetCheckpointCount();
        for (size_t i = 1; i < n; i += std::max<size_t>(1, n / 4))
        {
            if (!ttd->SeekTo(ttd::TTDTimePoint{ttd->GetCheckpoint(i)->time.frame, 0}))
                continue;
            pcs.push_back(context->pCore->GetZ80()->pc);
        }
        return pcs;
    }
};

const std::vector<ModelCase> kModels = {
    {"PENTAGON", 1024},   // extended #7FFD paging, 64 pages
    {"TSL", 0},           // TS-Conf, 4 MB: 256 pages
    {"SPRINTER", 0},      // 4 MB, its own window ports
    {"NEXT", 0},          // 8K slots: a key names either half of a 16 KB page
    {"48K", 0},           // pages 0, 2, 5
};

class TTD_ReverseSearchIndex_Test : public ::testing::TestWithParam<ModelCase>
{
protected:
    TTDReverseSearchSession s;
    void SetUp() override
    {
        ASSERT_TRUE(s.Create(GetParam())) << "cannot create " << GetParam().model;
        ASSERT_TRUE(s.Record(12)) << "the session captured nothing";
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// RSI-wide: ranges wider than 16 KB and ranges that wrap a 16 KB boundary prune
// ---------------------------------------------------------------------------

/// The offset set marks exactly the offsets asked for, at word edges too
TEST(TTD_CoverageOffsetSet_Test, AddRangeMarksExactlyTheRange)
{
    for (const auto& [lo, hi] : std::vector<std::pair<uint16_t, uint16_t>>{
             {0, 0}, {0, 63}, {63, 64}, {1, 126}, {0x3FC0, 0x3FFF}, {0x3FFF, 0x3FFF}, {0, 0x3FFF}, {100, 3000}})
    {
        ttd::TTDCoverageOffsetSet set;
        set.AddRange(lo, hi);
        for (uint32_t o = 0; o < 0x4000; ++o)
            ASSERT_EQ(set.Contains(o), o >= lo && o <= hi) << lo << ".." << hi << " offset " << o;
    }
    EXPECT_TRUE(ttd::TTDCoverageOffsetSet{}.Empty());
}

/// The set form of FrameMayContain agrees with the interval form, page filter included
TEST(TTD_CoverageOffsetSet_Test, SetQueryAgreesWithTheIntervalQuery)
{
    ttd::TTDCoverageIndex index;
    // Frame 0: page 3 offset 0x3FFF, ROM offset 0x0010; frame 1: nothing; frame 2: page 7 offset 0x2000
    index.Record(ttd::TTDCoverageKind::Read, ttd::MakeCoverageKey(3, 0xFFFF));
    index.Record(ttd::TTDCoverageKind::Read, ttd::MakeCoverageKey(ttd::kPhysPageNone, 0x0010));
    index.SealFrame(0);
    index.SealFrame(1);
    index.Record(ttd::TTDCoverageKind::Read, ttd::MakeCoverageKey(7, 0xA000));
    index.SealFrame(2);

    for (uint64_t frame = 0; frame < 3; ++frame)
        for (const auto& [lo, hi] : std::vector<std::pair<uint16_t, uint16_t>>{
                 {0, 0x3FFF}, {0x3FFF, 0x3FFF}, {0x0010, 0x0010}, {0x2000, 0x2000}, {0x11, 0x1FFF}})
            for (const int page : {-1, 3, 7, 0})
            {
                ttd::TTDCoverageOffsetSet set;
                set.AddRange(lo, hi);
                set.hasPage = page >= 0;
                set.pageField = page >= 0 ? ttd::CoveragePageField(static_cast<ttd::PhysPage>(page)) : 0;
                EXPECT_EQ(index.FrameMayContain(ttd::TTDCoverageKind::Read, frame, set),
                          index.FrameMayContain(ttd::TTDCoverageKind::Read, frame, lo, hi, page >= 0,
                                                static_cast<ttd::PhysPage>(page >= 0 ? page : 0)))
                    << "frame " << frame << " " << lo << ".." << hi << " page " << page;
            }
}

/// Wide and wrapping ranges, with and without a page: the index must not change the answer
TEST_P(TTD_ReverseSearchIndex_Test, WideAndWrappingRangesAreUnchangedByPruning)
{
    const std::vector<std::pair<uint16_t, uint16_t>> ranges = {
        {0x3FF0, 0x4010},   // wraps the ROM / RAM boundary
        {0x7FFE, 0x8001},   // wraps two RAM windows
        {0xBF00, 0xC0FF},   // into the paged window
        {0x4000, 0xBFFF},   // 32 KB
        {0x5B00, 0xFFFF},   // wider than a page, not aligned
        {0x0000, 0xFFFF},   // everything
    };
    for (const ttd::TTDAccessType access :
         {ttd::TTDAccessType::Read, ttd::TTDAccessType::Write, ttd::TTDAccessType::Execute})
        for (const auto& [lo, hi] : ranges)
        {
            ttd::TTDSearchQuery q;
            q.access = access;
            q.addrFrom = lo;
            q.addrTo = hi;
            s.ExpectSameWithAndWithoutIndex(q, "range " + std::to_string(lo) + ".." + std::to_string(hi) + " access " +
                                                   std::to_string(static_cast<int>(access)));
        }

    // Page-filtered wide ranges: only the frames that touched that page are replayed
    for (const ttd::PhysPage page : s.PagesSeen(4))
        for (const auto& [lo, hi] : std::vector<std::pair<uint16_t, uint16_t>>{{0x4000, 0xFFFF}, {0x3FFF, 0x8000}})
        {
            ttd::TTDSearchQuery q;
            q.access = ttd::TTDAccessType::Read;
            q.addrFrom = lo;
            q.addrTo = hi;
            q.hasPhysPageFilter = true;
            q.physPage = page;
            s.ExpectSameWithAndWithoutIndex(q, "page " + std::to_string(page) + " range " + std::to_string(lo) + ".." +
                                                   std::to_string(hi));
        }
}

/// coverage-scan reports a wrapping range's own frames: the two offset intervals, not every offset
TEST_P(TTD_ReverseSearchIndex_Test, CoverageScanOfAWrappingRangeIsExact)
{
    const ttd::TTDCoverageIndex& index = s.ttd->GetCoverageIndex();
    uint64_t first = 0, last = 0;
    ASSERT_TRUE(index.CoveredRange(ttd::TTDCoverageKind::Read, first, last));
    const bool slots8k = s.context->pMemory->HasTtdSlots8k();
    const uint16_t lo = 0x7FF0, hi = 0x800F;
    const ttd::TTDCoverageScanResult scan =
        s.ttd->QueryCoverageScan(first, last, ttd::TTDCoverageKind::Read, lo, hi, std::nullopt, 10000, false, false);
    std::vector<uint64_t> expected;
    std::vector<ttd::TTDCoverageKey> keys;
    for (uint64_t f = first; f <= last; ++f)
    {
        ASSERT_TRUE(index.GetFrameKeys(ttd::TTDCoverageKind::Read, f, keys));
        const bool touched = std::any_of(keys.begin(), keys.end(), [&](ttd::TTDCoverageKey k) {
            const uint32_t o = k & 0x3FFF;
            auto in = [](uint32_t off) { return off >= 0x3FF0 || off <= 0x000F; };
            return in(o) || (slots8k && in(o ^ 0x2000));
        });
        if (touched)
            expected.push_back(f);
    }
    EXPECT_EQ(scan.frames, expected);
}

// ---------------------------------------------------------------------------
// RSI-bpset: many reverse breakpoints, one decode per frame
// ---------------------------------------------------------------------------

TEST_P(TTD_ReverseSearchIndex_Test, LargeBreakpointSetsAreUnchangedByTheIndex)
{
    std::vector<uint16_t> ran = s.SomeExecutedPcs();
    ASSERT_FALSE(ran.empty());
    // A crowd of PCs that mostly never ran, with the real ones inside, on both halves of 8K slots
    std::vector<uint16_t> crowd;
    for (uint32_t i = 0; i < 300; ++i)
        crowd.push_back(static_cast<uint16_t>(0x6000 + i * 131));
    crowd.insert(crowd.end(), ran.begin(), ran.end());

    ttd::TTDReverseContinueQuery q;
    q.pcs = crowd;
    s.ExpectSameReverse(q, "single hit, 300+ PCs");
    q.all = true;
    q.limit = 40;
    s.ExpectSameReverse(q, "all, 300+ PCs");
    q.all = false;
    q.firstPerPc = true;
    s.ExpectSameReverse(q, "first per PC, 300+ PCs");
}

// ---------------------------------------------------------------------------
// RSI-cond: conditions in reverse continue
// ---------------------------------------------------------------------------

/// FRAME < F keeps exactly the hits of the frames before F; PC == its own PC keeps all; the index does not matter
TEST_P(TTD_ReverseSearchIndex_Test, ConditionKeepsTheHitsWhereItHolds)
{
    const std::vector<uint16_t> ran = s.SomeExecutedPcs();
    ASSERT_FALSE(ran.empty());
    ttd::TTDReverseContinueQuery all;
    all.pcs = ran;
    all.all = true;
    all.limit = 10000;
    s.Park();
    const ttd::TTDReverseContinueResult every = s.ttd->ReverseContinue(all);
    ASSERT_TRUE(every.error.empty()) << every.error;
    ASSERT_GE(every.hits.size(), 2u);

    // A frame in the middle of the hits: the condition must cut the list there
    const uint64_t cut = every.hits[every.hits.size() / 2].time.frame;
    ttd::TTDReverseContinueQuery q = all;
    q.condition = "FRAME < " + std::to_string(cut);
    s.Park();
    const ttd::TTDReverseContinueResult conditioned = s.ttd->ReverseContinue(q);
    ASSERT_TRUE(conditioned.error.empty()) << conditioned.error;
    std::vector<uint64_t> expected, got;
    for (const ttd::TTDReverseHit& h : every.hits)
        if (h.time.frame < cut)
            expected.push_back(h.globalT);
    // The unconditioned walk may stop at the hit limit: compare inside the window it listed
    const uint64_t oldest = every.hits.back().globalT;
    for (const ttd::TTDReverseHit& h : conditioned.hits)
        if (!every.truncated || h.globalT >= oldest)
            got.push_back(h.globalT);
    EXPECT_EQ(got, expected);
    s.ExpectSameReverse(q, "FRAME condition, all");

    // Single mode: lands on the newest hit where it holds
    ttd::TTDReverseContinueQuery single;
    single.pcs = ran;
    single.condition = q.condition;
    s.Park();
    const ttd::TTDReverseContinueResult one = s.ttd->ReverseContinue(single);
    ASSERT_EQ(one.matched, !expected.empty());
    if (one.matched)
        EXPECT_EQ(one.hits.front().globalT, expected.front());
    s.ExpectSameReverse(single, "FRAME condition, single");

    // A condition on the machine state: PC is the instruction's own address at its start
    q.condition = "PC == " + std::to_string(every.hits.front().pc);
    s.Park();
    const ttd::TTDReverseContinueResult ownPc = s.ttd->ReverseContinue(q);
    size_t ofThatPc = 0, listed = 0;
    for (const ttd::TTDReverseHit& h : every.hits)
        ofThatPc += h.pc == every.hits.front().pc;
    for (const ttd::TTDReverseHit& h : ownPc.hits)
        listed += !every.truncated || h.globalT >= oldest;
    EXPECT_EQ(listed, ofThatPc);
}

/// A register condition against the state read at each hit by seeking there
TEST_P(TTD_ReverseSearchIndex_Test, RegisterConditionMatchesTheStateAtTheHit)
{
    const std::vector<uint16_t> ran = s.SomeExecutedPcs();
    ASSERT_FALSE(ran.empty());
    ttd::TTDReverseContinueQuery all;
    all.pcs = {ran.back()};
    all.all = true;
    all.limit = 24;
    s.Park();
    const ttd::TTDReverseContinueResult every = s.ttd->ReverseContinue(all);
    ASSERT_FALSE(every.hits.empty());

    // A at each hit; ask for the value of the oldest listed one
    std::vector<uint8_t> a;
    for (const ttd::TTDReverseHit& h : every.hits)
    {
        ASSERT_TRUE(s.ttd->SeekTo(h.time));
        ASSERT_EQ(s.context->pCore->GetZ80()->pc, h.pc) << "the seek did not land on the hit's instruction";
        a.push_back(s.context->pCore->GetZ80()->a);
    }
    const uint8_t wanted = a.back();
    ttd::TTDReverseContinueQuery q = all;
    q.condition = "A == " + std::to_string(wanted);
    s.Park();
    const ttd::TTDReverseContinueResult conditioned = s.ttd->ReverseContinue(q);
    std::vector<uint64_t> expected, got;
    for (size_t i = 0; i < every.hits.size(); ++i)
        if (a[i] == wanted)
            expected.push_back(every.hits[i].globalT);
    for (const ttd::TTDReverseHit& h : conditioned.hits)
        if (h.globalT >= every.hits.back().globalT)   // the listed window only
            got.push_back(h.globalT);
    EXPECT_EQ(got, expected);
}

/// Single mode without a hit leaves the machine where it was, though candidate frames were replayed: a condition
/// that never holds, and PCs whose offsets run in other banks
TEST_P(TTD_ReverseSearchIndex_Test, NoHitLeavesTheMachineWhereItWas)
{
    const std::vector<uint16_t> ran = s.SomeExecutedPcs();
    ASSERT_FALSE(ran.empty());
    const ttd::TTDTimePoint end = s.ttd->SessionEndPosition();
    const ttd::TTDTimePoint at{end.frame - 2, 1000};
    ASSERT_TRUE(s.ttd->SeekTo(at));
    const ttd::TTDTimePoint before = s.ttd->CurrentPosition();
    const uint16_t pcBefore = s.context->pCore->GetZ80()->pc;

    ttd::TTDReverseContinueQuery never;
    never.pcs = ran;
    never.condition = "FRAME > 100000000";
    ttd::TTDReverseContinueQuery aliased;
    for (uint16_t i = 0; i < 64; ++i)
        aliased.pcs.push_back(static_cast<uint16_t>(0xF000 + i * 61));
    for (const ttd::TTDReverseContinueQuery* q : {&never, &aliased})
    {
        const ttd::TTDReverseContinueResult r = s.ttd->ReverseContinue(*q);
        if (r.matched)
            continue;   // an aliased PC that did run: nothing to check
        EXPECT_EQ(s.ttd->CurrentPosition(), before) << (q == &never ? "condition" : "aliased PCs");
        EXPECT_EQ(s.context->pCore->GetZ80()->pc, pcBefore) << (q == &never ? "condition" : "aliased PCs");
    }
}

TEST(TTD_ReverseContinueCondition_Test, BadConditionsAreRefused)
{
    TTDReverseSearchSession s;
    ASSERT_TRUE(s.Create({"PENTAGON", 0}));
    ASSERT_TRUE(s.Record(3));
    ttd::TTDReverseContinueQuery q;
    q.pcs = {0x0038};
    for (const char* text : {"A ==", "A@prev == 1", "RD == 5"})
    {
        q.condition = text;
        s.Park();
        const ttd::TTDReverseContinueResult r = s.ttd->ReverseContinue(q);
        EXPECT_EQ(r.error.rfind("condition", 0), 0u) << text << ": " << r.error;
        EXPECT_FALSE(r.matched) << text;
    }

    // Through the verb: a 400 with the reason; a good one answers
    ttd::TTDControl control(s.context);
    const ttd::TTDReply bad = control.Execute({"reverse-continue", {{"pcs", "0x38"}, {"condition", "A =="}}});
    EXPECT_EQ(bad.error, ttd::TTDControlError::BadRequest);
    const ttd::TTDReply good = control.Execute({"reverse-continue", {{"pcs", "0x38"}, {"condition", "1"}}});
    EXPECT_TRUE(good.Ok()) << good.message;
}

INSTANTIATE_TEST_SUITE_P(Models, TTD_ReverseSearchIndex_Test, ::testing::ValuesIn(kModels), ModelCaseName);

// ---------------------------------------------------------------------------
// RSI-models: the page bound of every model in the table
// ---------------------------------------------------------------------------

/// Every model of the configuration's table: a recording's dirty pages all fall inside the bound
/// (ResolveModelRamPages), and so does every page banked in. A few frames here; the whole boot in the long tier
TEST(TTD_ModelPageBoundsAll_Test, EveryModelRecordsEveryPageItWrites)
{
    const unsigned frames = TestTier::Long() ? 150 : 4;
    size_t checked = 0;
    for (const TMemModel& model : Config::GetAvailableModels())
    {
        if (!Config::IsModelCreatable(model))   // in the table without a port decoder or a config folder (KAY, ...)
            continue;
        ++checked;
        TTDReverseSearchSession s;
        ASSERT_TRUE(s.Create({model.ShortName, 0})) << model.ShortName;
        ASSERT_TRUE(s.ttd->StartRecording()) << model.ShortName;
        for (unsigned f = 0; f < frames; ++f)
        {
            s.emulator->RunNFrames(1, /*skipBreakpoints=*/true);
            for (uint16_t bank = 0; bank < 4; ++bank)
            {
                const ttd::PhysPage page = s.context->pMemory->GetPhysPageForZ80Address(static_cast<uint16_t>(bank * 0x4000));
                if (page != ttd::kPhysPageNone && ttd::SpaceOfPage(page) == ttd::TTDMemorySpace::Ram)
                    ASSERT_LT(page, s.ttd->GetModelRamPages()) << model.ShortName << " frame " << f << " bank " << bank;
            }
        }
        s.ttd->StopRecording();
        EXPECT_EQ(s.ttd->GetPagesBeyondBound(), 0u)
            << model.ShortName << ": a page past the bound " << s.ttd->GetModelRamPages() << " was written";
    }
    EXPECT_GE(checked, 15u) << "the creatable models went missing";
}

// ---------------------------------------------------------------------------
// Measurements (DISABLED_: run on demand, numbers in the RSI README)
// ---------------------------------------------------------------------------

namespace
{
double Ms(const std::chrono::steady_clock::duration d)
{
    return std::chrono::duration<double, std::milli>(d).count();
}

template <typename F>
double BestOf(int rounds, F&& f)
{
    double best = 1e300;
    for (int i = 0; i < rounds; ++i)
    {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        best = std::min(best, Ms(std::chrono::steady_clock::now() - t0));
    }
    return best;
}
}  // namespace

/// RSI-wide: a 300-frame Pentagon 1024 session, queries the index could not prune before (with the index = now,
/// without = the old full scan)
TEST(TTD_ReverseSearchIndexMeasure, DISABLED_WideAndWrappingRanges)
{
    TTDReverseSearchSession s;
    ASSERT_TRUE(s.Create({"PENTAGON", 1024}));
    ASSERT_TRUE(s.Record(300));
    struct Case
    {
        const char* what;
        uint16_t lo, hi;
        int page;
        ttd::TTDAccessType access;
    };
    const Case cases[] = {
        {"read #7FF0..#800F (wraps)", 0x7FF0, 0x800F, -1, ttd::TTDAccessType::Read},
        {"execute #BFF0..#C00F (wraps)", 0xBFF0, 0xC00F, -1, ttd::TTDAccessType::Execute},
        {"read #4000..#FFFF page 9 (48 KB)", 0x4000, 0xFFFF, 9, ttd::TTDAccessType::Read},
        {"execute #4000..#FFFF (48 KB)", 0x4000, 0xFFFF, -1, ttd::TTDAccessType::Execute},
        {"read #0000..#FFFF (all)", 0x0000, 0xFFFF, -1, ttd::TTDAccessType::Read},
    };
    for (const Case& c : cases)
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = c.lo;
        q.addrTo = c.hi;
        q.access = c.access;
        q.hasPhysPageFilter = c.page >= 0;
        q.physPage = static_cast<ttd::PhysPage>(c.page >= 0 ? c.page : 0);
        s.ttd->SetEnableCoverageIndex(true);
        const double with = BestOf(3, [&] { (void)s.Find(q); });
        s.ttd->SetEnableCoverageIndex(false);
        const double without = BestOf(3, [&] { (void)s.Find(q); });
        s.ttd->SetEnableCoverageIndex(true);
        std::printf("RSI-wide %-36s index %8.2f ms  full scan %8.2f ms  x%.1f\n", c.what, with, without,
                    without / std::max(with, 1e-3));
    }
}

/// RSI-bpset: reverse continue over 300 frames with N breakpoints that never ran (the index walk alone), and N
/// breakpoints with one that ran early
TEST(TTD_ReverseSearchIndexMeasure, DISABLED_LargeBreakpointSets)
{
    TTDReverseSearchSession s;
    ASSERT_TRUE(s.Create({"PENTAGON", 0}));
    ASSERT_TRUE(s.Record(300));
    for (const size_t n : {size_t{1}, size_t{16}, size_t{64}, size_t{256}, size_t{1024}})
    {
        ttd::TTDReverseContinueQuery q;
        for (size_t i = 0; i < n; ++i)
            q.pcs.push_back(static_cast<uint16_t>(0x6000 + i * 13));   // the 128 menu never runs there
        s.ttd->SetEnableCoverageIndex(true);
        const double with = BestOf(3, [&] { (void)s.Reverse(q); });
        s.ttd->SetEnableCoverageIndex(false);
        const double without = BestOf(1, [&] { (void)s.Reverse(q); });
        s.ttd->SetEnableCoverageIndex(true);
        std::printf("RSI-bpset %5zu PCs, no hit: index %8.2f ms  no index %8.2f ms\n", n, with, without);
    }
}

/// RSI-cond: reverse continue with a condition false everywhere (the whole walk) against the same without one
TEST(TTD_ReverseSearchIndexMeasure, DISABLED_Condition)
{
    TTDReverseSearchSession s;
    ASSERT_TRUE(s.Create({"PENTAGON", 0}));
    ASSERT_TRUE(s.Record(300));
    const std::vector<uint16_t> ran = s.SomeExecutedPcs();
    ttd::TTDReverseContinueQuery q;
    q.pcs = ran;
    q.all = true;
    q.limit = 10000;
    const double plain = BestOf(3, [&] { (void)s.Reverse(q); });
    const size_t hits = [&] { s.Park(); return s.ttd->ReverseContinue(q).hits.size(); }();
    q.condition = "A == 1 && B == 2 && FRAME == 1";
    const double conditioned = BestOf(3, [&] { (void)s.Reverse(q); });
    std::printf("RSI-cond all hits of %zu PCs (%zu hits): plain %8.2f ms  condition false everywhere %8.2f ms\n",
                ran.size(), hits, plain, conditioned);
}

/// RSI-zone: what zone maps (written-value set, port set, SP range) would save. The searches they would prune
/// answer from the journals without a replay; a search that never matches scans the whole journal - its cost is
/// the ceiling a zone map could remove
TEST(TTD_ReverseSearchIndexMeasure, DISABLED_ZoneMapCandidates)
{
    // The queries a zone map (value set, port set per block) would answer, with the write journal off (the
    // default, D40: writes come from replay, pruned by write coverage) and on (writes come from the journal)
    for (const bool journal : {false, true})
    {
        TTDReverseSearchSession s;
        ASSERT_TRUE(s.Create({"PENTAGON", 0}));
        s.ttd->SetEnableWriteJournal(journal);
        const unsigned frames = 1500;   // 30 s of a 128K boot and its menu (the ROM clears and tests RAM first)
        ASSERT_TRUE(s.Record(frames));
        const size_t records = s.ttd->GetSessionInfo().writeJournalRecords;

        // A write by an instruction at #0001 (never): every record is read and rejected, the worst case a
        // zone map would cut
        ttd::TTDSearchQuery write;
        write.access = ttd::TTDAccessType::Write;
        write.addrFrom = 0;
        write.addrTo = 0xFFFF;
        write.hasPcFilter = true;
        write.pcFrom = write.pcTo = 0x0001;
        std::string answer;
        const double writeMs = BestOf(3, [&] { answer = s.Find(write); });
        EXPECT_EQ(answer, "none");

        // A value written to one address: the value-set zone map's query
        ttd::TTDSearchQuery value;
        value.access = ttd::TTDAccessType::Write;
        value.addrFrom = value.addrTo = 0x5C78;
        value.hasValueFilter = true;
        value.value = 0xEE;
        const double valueMs = BestOf(3, [&] { answer = s.Find(value); });
        const std::string valueAnswer = answer;

        // A port never written: the port-set zone map's query
        ttd::TTDSearchQuery io;
        io.access = ttd::TTDAccessType::Io;
        io.addrFrom = io.addrTo = 0x1234;
        const double ioMs = BestOf(3, [&] { answer = s.Find(io); });

        std::printf("RSI-zone journal %s, %u frames, %zu write records: never-matching write %.2f ms, value %.2f ms "
                    "(%s), port %.2f ms\n",
                    journal ? "on" : "off", frames, records, writeMs, valueMs, valueAnswer.c_str(), ioMs);
        std::printf("RSI-zone journal %s per hour (x%.0f): write %.0f ms, port %.0f ms, %.1f M records\n",
                    journal ? "on" : "off", 180000.0 / frames, writeMs * 180000.0 / frames,
                    ioMs * 180000.0 / frames, records * 180000.0 / frames / 1e6);
    }
}
