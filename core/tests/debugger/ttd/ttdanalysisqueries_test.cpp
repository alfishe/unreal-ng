// The analysis queries (docs/inprogress/2026-10-28-ttd-analysis-feedback): coverage-scan lists every matching frame
// (both orders, runs, a cursor), page filters (ROM, two banks behind one Z80 address), reverse-continue `all`,
// find-last `changes_only` / `all` and the memory-at CPU view. Each answer is checked against a second way to get it:
// a loop of single calls, a brute-force walk of every write, the live machine after a seek.
//
// One program records 30 frames on a Pentagon (about 20 ms with turbo):
//   - 0x8100 changes every loop pass, 0x8101 is set to 5 every pass (one change), 0x8103 flips every 16 passes;
//   - every 64 passes (about every third frame) the routine at 0x8200 swaps page 1 / page 3 into bank 3 and calls
//     0xC000 there, so one Z80 address runs on two pages;
//   - every pass reads 0x0000 in the ROM.

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdcontrol.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
constexpr unsigned kFrames = 30;

class TTDAnalysisQueries_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void Record(bool writeJournal)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _context->pFeatureManager->setFeature(Features::kDebugMode, true);
        _context->pFeatureManager->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _emulator->EnableTurboMode();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
        _ttd->SetEnableWriteJournal(writeJournal);

        Memory* memory = _context->pMemory;
        const std::vector<uint8_t> loop = {
            0xF3,                    // 8000 di
            0x0E, 0x00,              // 8001 ld c,0
            0x3E, 0x11,              // 8003 ld a,#11 (page 1, 48K ROM)
            0x32, 0xF0, 0x80,        // 8005 ld (#80F0),a
            0x06, 0x00,              // 8008 loop: ld b,0
            0x10, 0xFE,              // 800A djnz $ (about 3,300 T)
            0x0C,                    // 800C inc c
            0x79,                    // 800D ld a,c
            0x32, 0x00, 0x81,        // 800E ld (#8100),a      every pass a new value
            0x3E, 0x05,              // 8011 ld a,5
            0x32, 0x01, 0x81,        // 8013 ld (#8101),a      always 5
            0x79,                    // 8016 ld a,c
            0x0F, 0x0F, 0x0F, 0x0F,  // 8017 rrca x4
            0xE6, 0x01,              // 801B and 1
            0x32, 0x03, 0x81,        // 801D ld (#8103),a      flips every 16 passes
            0x3A, 0x00, 0x00,        // 8020 ld a,(#0000)      a ROM read
            0x79,                    // 8023 ld a,c
            0xE6, 0x3F,              // 8024 and #3F
            0xCC, 0x00, 0x82,        // 8026 call z,#8200
            0x18, 0xDD,              // 8029 jr loop
        };
        const std::vector<uint8_t> swap = {
            0xC5,                    // 8200 push bc
            0x3A, 0xF0, 0x80,        // 8201 ld a,(#80F0)
            0xEE, 0x02,              // 8204 xor 2 (page 1 <-> page 3)
            0x32, 0xF0, 0x80,        // 8206 ld (#80F0),a
            0x01, 0xFD, 0x7F,        // 8209 ld bc,#7FFD
            0xED, 0x79,              // 820C out (c),a
            0xCD, 0x00, 0xC0,        // 820E call #C000
            0xC1,                    // 8211 pop bc
            0xC9,                    // 8212 ret
        };
        for (size_t i = 0; i < loop.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), loop[i]);
        for (size_t i = 0; i < swap.size(); i++)
            memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8200 + i), swap[i]);
        for (uint16_t a : {0x8100, 0x8101, 0x8103})
            memory->DirectWriteToZ80Memory(a, 0);
        memory->RAMPageAddress(1)[0] = 0xC9;   // ret on both pages bank 3 shows
        memory->RAMPageAddress(3)[0] = 0xC9;
        _context->pCore->GetZ80()->pc = 0x8000;
        _context->pCore->GetZ80()->sp = 0xBF00;   // the stack in page 2: bank 3 changes under the call

        ASSERT_TRUE(_ttd->StartRecording());
        _emulator->RunNFrames(kFrames, /*skipBreakpoints=*/true);
        _ttd->StopRecording();
        ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
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

    ttd::TTDCoverageScanResult Scan(uint16_t addr, std::optional<ttd::PhysPage> page, size_t limit, bool newest = false,
                                    bool ranges = false, uint64_t from = 0, uint64_t to = UINT64_MAX,
                                    ttd::TTDCoverageKind kind = ttd::TTDCoverageKind::Executed)
    {
        return _ttd->QueryCoverageScan(from, to, kind, addr, addr, page, limit, newest, ranges);
    }

    /// Every instruction start at pc (on page, if given), newest first, the machine stays
    std::vector<ttd::TTDReverseHit> AllHits(uint16_t pc, std::optional<ttd::PhysPage> page = std::nullopt, size_t limit = 10000)
    {
        ttd::TTDReverseContinueQuery q;
        q.pcs = {pc};
        q.physPage = page;
        q.all = true;
        q.limit = limit;
        return _ttd->ReverseContinue(q).hits;
    }

    ttd::TTDSearchQuery Writes(uint16_t addr) const
    {
        ttd::TTDSearchQuery q;
        q.addrFrom = q.addrTo = addr;
        q.access = ttd::TTDAccessType::Write;
        return q;
    }

    /// The writes that changed addr's byte, newest first, from the list of every write (the byte starts at 0)
    std::vector<ttd::TTDFoundAccess> ChangesByBruteForce(uint16_t addr)
    {
        ttd::TTDFindOptions every;
        every.all = true;
        every.limit = 10000;
        ttd::TTDFindResult writes = _ttd->FindAccesses(Writes(addr), every);
        EXPECT_FALSE(writes.truncated);
        std::vector<ttd::TTDFoundAccess> changes;
        uint8_t byte = 0;
        for (auto it = writes.hits.rbegin(); it != writes.hits.rend(); ++it)
        {
            if (it->access.value != byte)
            {
                ttd::TTDFoundAccess change = *it;
                change.hasOldValue = true;
                change.oldValue = byte;
                changes.push_back(change);
            }
            byte = it->access.value;
        }
        std::reverse(changes.begin(), changes.end());
        return changes;
    }

    void ExpectChangesOnlyMatchesBruteForce()
    {
        for (uint16_t addr : {0x8101, 0x8103, 0x8100})
        {
            SCOPED_TRACE(addr);
            const std::vector<ttd::TTDFoundAccess> expected = ChangesByBruteForce(addr);
            ttd::TTDFindOptions changes;
            changes.all = true;
            changes.changesOnly = true;
            changes.limit = 10000;
            const ttd::TTDFindResult found = _ttd->FindAccesses(Writes(addr), changes);
            ASSERT_EQ(found.hits.size(), expected.size());
            for (size_t i = 0; i < expected.size(); ++i)
            {
                EXPECT_EQ(found.hits[i].globalT, expected[i].globalT) << i;
                EXPECT_EQ(found.hits[i].access.value, expected[i].access.value) << i;
                EXPECT_TRUE(found.hits[i].hasOldValue) << i;
                EXPECT_EQ(found.hits[i].oldValue, expected[i].oldValue) << i;
            }
            if (addr == 0x8101)
                EXPECT_EQ(found.hits.size(), 1u) << "set to 5 on every pass: it changed once";
            if (addr == 0x8103)
                EXPECT_GT(found.hits.size(), 20u) << "it flips every 16 passes";
        }
    }
};

std::vector<uint64_t> FramesOf(const std::vector<ttd::TTDReverseHit>& hits)
{
    std::set<uint64_t> frames;
    for (const ttd::TTDReverseHit& h : hits)
        frames.insert(h.time.frame);
    return {frames.begin(), frames.end()};
}
}  // namespace

// ---------------------------------------------------------------------------
// coverage-scan
// ---------------------------------------------------------------------------

/// The frame list is the frames the routine ran in, as the replay finds them; newest order is the same list reversed
TEST_F(TTDAnalysisQueries_Test, CoverageScanListsEveryFrameTheRoutineRan)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const std::vector<uint64_t> truth = FramesOf(AllHits(0x8200));
    ASSERT_GE(truth.size(), 5u);

    const ttd::TTDCoverageScanResult oldest = Scan(0x8200, ttd::PhysPage(2), 10000);
    EXPECT_EQ(oldest.frames, truth);
    EXPECT_EQ(oldest.matchingFrames, truth.size());
    EXPECT_FALSE(oldest.truncated);

    const ttd::TTDCoverageScanResult newest = Scan(0x8200, ttd::PhysPage(2), 10000, /*newest=*/true);
    EXPECT_EQ(newest.frames, std::vector<uint64_t>(truth.rbegin(), truth.rend()));
    EXPECT_EQ(newest.firstMatch, truth.front());
    EXPECT_EQ(newest.lastMatch, truth.back());
}

/// A truncated list keeps the true first / last match, and its cursor walks the rest without a gap or a repeat
TEST_F(TTDAnalysisQueries_Test, CoverageScanCursorWalksTheWholeListBothOrders)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const std::vector<uint64_t> all = Scan(0x8200, ttd::PhysPage(2), 10000).frames;
    ASSERT_GE(all.size(), 5u);

    std::vector<uint64_t> walked;
    uint64_t from = 0;
    for (int calls = 0; calls < 100; ++calls)
    {
        const ttd::TTDCoverageScanResult page = Scan(0x8200, ttd::PhysPage(2), 2, false, false, from);
        EXPECT_EQ(page.lastMatch, all.back()) << "the newest match even when cut";
        walked.insert(walked.end(), page.frames.begin(), page.frames.end());
        if (!page.truncated)
            break;
        from = page.nextFrom;
    }
    EXPECT_EQ(walked, all);

    walked.clear();
    uint64_t to = UINT64_MAX;
    for (int calls = 0; calls < 100; ++calls)
    {
        const ttd::TTDCoverageScanResult page = Scan(0x8200, ttd::PhysPage(2), 2, true, false, 0, to);
        EXPECT_EQ(page.firstMatch, all.front()) << "the oldest match even when cut";
        walked.insert(walked.end(), page.frames.begin(), page.frames.end());
        if (!page.truncated)
            break;
        to = page.nextTo;
    }
    EXPECT_EQ(walked, std::vector<uint64_t>(all.rbegin(), all.rend()));
}

/// Runs: the loop runs in every frame (one run), the routine's frames expand back to its frame list
TEST_F(TTDAnalysisQueries_Test, CoverageScanRangesAreRunsOfTheFrameList)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const ttd::TTDCoverageScanResult loop = Scan(0x8008, ttd::PhysPage(2), 100, false, /*ranges=*/true);
    ASSERT_EQ(loop.ranges.size(), 1u) << "the loop runs in every recorded frame";
    EXPECT_EQ(loop.ranges[0].second - loop.ranges[0].first + 1, loop.matchingFrames);
    EXPECT_TRUE(loop.frames.empty());

    for (bool newest : {false, true})
    {
        const std::vector<uint64_t> frames = Scan(0x8200, ttd::PhysPage(2), 10000, newest).frames;
        const ttd::TTDCoverageScanResult runs = Scan(0x8200, ttd::PhysPage(2), 10000, newest, true);
        std::vector<uint64_t> expanded;
        for (auto [a, b] : runs.ranges)
        {
            if (newest)
                for (uint64_t f = a + 1; f-- > b;)
                    expanded.push_back(f);
            else
                for (uint64_t f = a; f <= b; ++f)
                    expanded.push_back(f);
        }
        EXPECT_EQ(expanded, frames) << (newest ? "newest" : "oldest");
    }
}

/// rom names the page-less bucket: the ROM read is there in every frame, never on RAM page 0 at the same offset
TEST_F(TTDAnalysisQueries_Test, CoverageScanRomIsItsOwnPage)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const auto rom = Scan(0x0000, ttd::kPhysPageNone, 100, false, false, 0, UINT64_MAX, ttd::TTDCoverageKind::Read);
    const auto ram0 = Scan(0x0000, ttd::PhysPage(0), 100, false, false, 0, UINT64_MAX, ttd::TTDCoverageKind::Read);
    EXPECT_EQ(rom.matchingFrames, rom.scannedFrames);
    EXPECT_GT(rom.matchingFrames, 20u);
    EXPECT_EQ(ram0.matchingFrames, 0u);

    // Through the verb: rom parses, the answer says it is exact only with a page
    const ttd::TTDReply exact = Run("coverage-scan", {{"kind", "read"}, {"addr_from", "0"}, {"addr_to", "0"}, {"phys_page", "rom"}});
    ASSERT_TRUE(exact.Ok()) << exact.message;
    EXPECT_TRUE(exact.body.find("exact")->b);
    EXPECT_EQ(exact.body.find("phys_page")->s, "rom");
    const ttd::TTDReply any = Run("coverage-scan", {{"kind", "read"}, {"addr_from", "0"}, {"addr_to", "0"}});
    ASSERT_TRUE(any.Ok()) << any.message;
    EXPECT_FALSE(any.body.find("exact")->b);
}

// ---------------------------------------------------------------------------
// reverse-continue
// ---------------------------------------------------------------------------

/// One Z80 address on two pages: phys_page splits the hits, and every hit names the page it ran on
TEST_F(TTDAnalysisQueries_Test, ReverseContinuePhysPageSeparatesTwoBanks)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const auto any = AllHits(0xC000);
    const auto page1 = AllHits(0xC000, ttd::PhysPage(1));
    const auto page3 = AllHits(0xC000, ttd::PhysPage(3));
    ASSERT_GE(page1.size(), 2u);
    ASSERT_GE(page3.size(), 2u);
    EXPECT_EQ(page1.size() + page3.size(), any.size());
    for (const auto& h : page1)
        EXPECT_EQ(h.physPage, 1u);
    for (const auto& h : page3)
        EXPECT_EQ(h.physPage, 3u);
    // They alternate: the routine swaps the page before each call
    for (size_t i = 1; i < any.size(); ++i)
        EXPECT_NE(any[i].physPage, any[i - 1].physPage) << i;
    EXPECT_TRUE(AllHits(0xC000, ttd::PhysPage(2)).empty());
    EXPECT_TRUE(AllHits(0xC000, ttd::kPhysPageNone).empty()) << "nothing ran there from the ROM";
}

/// all = the hits a loop of single calls lands on, and the machine does not move
TEST_F(TTDAnalysisQueries_Test, ReverseContinueAllMatchesALoopOfSingleCalls)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const ttd::TTDTimePoint before = _ttd->CurrentPosition();
    const uint8_t liveByte = _context->pMemory->DirectReadFromZ80Memory(0x8100);

    ttd::TTDReverseContinueQuery q;
    q.pcs = {0x8200};
    q.all = true;
    q.limit = 4;
    const ttd::TTDReverseContinueResult first = _ttd->ReverseContinue(q);
    ASSERT_EQ(first.hits.size(), 4u);
    EXPECT_TRUE(first.truncated);
    EXPECT_EQ(_ttd->CurrentPosition(), before) << "all leaves the machine where it stood";
    EXPECT_EQ(_context->pMemory->DirectReadFromZ80Memory(0x8100), liveByte);

    // The next page from the oldest hit, at or before
    q.beforeGlobalT = first.hits.back().globalT - 1;
    const ttd::TTDReverseContinueResult second = _ttd->ReverseContinue(q);
    ASSERT_FALSE(second.hits.empty());
    EXPECT_LT(second.hits.front().globalT, first.hits.back().globalT);

    std::vector<ttd::TTDReverseHit> paged = first.hits;
    paged.insert(paged.end(), second.hits.begin(), second.hits.end());
    for (size_t i = 0; i < paged.size(); ++i)
    {
        const ttd::TTDReverseContinueResult single = _ttd->ReverseContinue(std::vector<uint16_t>{0x8200});
        ASSERT_TRUE(single.matched) << i;
        EXPECT_EQ(single.arrivedAt, paged[i].time) << i;
        EXPECT_EQ(_context->pCore->GetZ80()->pc, 0x8200) << i;
    }
}

/// from_frame and before bound the walk on both sides
TEST_F(TTDAnalysisQueries_Test, ReverseContinueBoundsLimitTheWindow)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const auto all = AllHits(0x8200);
    ASSERT_GE(all.size(), 5u);
    const ttd::TTDReverseHit& middle = all[all.size() / 2];

    ttd::TTDReverseContinueQuery q;
    q.pcs = {0x8200};
    q.all = true;
    q.beforeGlobalT = middle.globalT;   // at or before: the middle hit is the newest
    const auto older = _ttd->ReverseContinue(q).hits;
    ASSERT_FALSE(older.empty());
    EXPECT_EQ(older.front().globalT, middle.globalT);
    EXPECT_EQ(older.size(), all.size() - all.size() / 2);

    q.beforeGlobalT = UINT64_MAX;
    q.fromFrame = middle.time.frame;
    const auto newer = _ttd->ReverseContinue(q).hits;
    ASSERT_FALSE(newer.empty());
    for (const auto& h : newer)
        EXPECT_GE(h.time.frame, middle.time.frame);
    EXPECT_EQ(newer.back().time.frame, middle.time.frame);
}

// ---------------------------------------------------------------------------
// find-last
// ---------------------------------------------------------------------------

/// changes_only from the write journal = the brute-force walk of every write
TEST_F(TTDAnalysisQueries_Test, FindChangesOnlyFromTheJournal)
{
    ASSERT_NO_FATAL_FAILURE(Record(true));
    ExpectChangesOnlyMatchesBruteForce();
}

/// changes_only from the replay (no journal) gives the same answer
TEST_F(TTDAnalysisQueries_Test, FindChangesOnlyFromTheReplay)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    ExpectChangesOnlyMatchesBruteForce();
}

/// The journal and the replay give the same list of every write, page by page with the cursor
TEST_F(TTDAnalysisQueries_Test, FindAllPagesWithTheCursor)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    ttd::TTDFindOptions every;
    every.all = true;
    every.limit = 10000;
    const ttd::TTDFindResult whole = _ttd->FindAccesses(Writes(0x8103), every);
    ASSERT_GT(whole.hits.size(), 100u) << "one write a pass";

    ttd::TTDFindOptions page = every;
    page.limit = 128;
    ttd::TTDSearchQuery q = Writes(0x8103);
    std::vector<uint64_t> walked;
    for (int calls = 0; calls < 100; ++calls)
    {
        const ttd::TTDFindResult r = _ttd->FindAccesses(q, page);
        for (const auto& h : r.hits)
            walked.push_back(h.globalT);
        if (!r.truncated)
            break;
        EXPECT_EQ(r.hits.size(), page.limit);
        q.beforeGlobalT = r.hits.back().globalT - 1;
    }
    ASSERT_EQ(walked.size(), whole.hits.size());
    for (size_t i = 0; i < walked.size(); ++i)
        EXPECT_EQ(walked[i], whole.hits[i].globalT) << i;
}

/// The verb: changes_only only for writes; the list carries old_value and the cursor
TEST_F(TTDAnalysisQueries_Test, FindLastVerbListsChanges)
{
    ASSERT_NO_FATAL_FAILURE(Record(true));
    const ttd::TTDReply bad = Run("find-last", {{"addr", "0x8101"}, {"access", "read"}, {"changes_only", "true"}});
    EXPECT_EQ(bad.error, ttd::TTDControlError::BadRequest);

    const ttd::TTDReply once = Run("find-last", {{"addr", "0x8101"}, {"changes_only", "true"}, {"all", "true"}});
    ASSERT_TRUE(once.Ok()) << once.message;
    ASSERT_EQ(once.body.find("count")->i, 1);
    const StateNode& hit = once.body.find("hits")->items[0];
    EXPECT_EQ(hit.find("value")->i, 5);
    EXPECT_EQ(hit.find("old_value")->i, 0);
    EXPECT_FALSE(once.body.find("truncated")->b);

    const ttd::TTDReply cut = Run("find-last", {{"addr", "0x8100"}, {"all", "true"}, {"limit", "3"}});
    ASSERT_TRUE(cut.Ok()) << cut.message;
    EXPECT_EQ(cut.body.find("count")->i, 3);
    ASSERT_TRUE(cut.body.find("next_before"));
    const ttd::TTDReply next = Run("find-last", {{"addr", "0x8100"}, {"all", "true"}, {"limit", "3"},
                                                 {"before", std::to_string(cut.body.find("next_before")->i)}});
    ASSERT_TRUE(next.Ok()) << next.message;
    EXPECT_LT(next.body.find("hits")->items[0].find("t")->i, cut.body.find("hits")->items[2].find("t")->i);
}

// ---------------------------------------------------------------------------
// memory-at space cpu
// ---------------------------------------------------------------------------

/// The CPU view at a point = what the machine shows after a seek there, with the bank behind each window
TEST_F(TTDAnalysisQueries_Test, CpuViewMatchesTheMachineAfterASeek)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const auto calls = AllHits(0xC000);
    ASSERT_GE(calls.size(), 2u);
    for (const ttd::TTDReverseHit& call : {calls[0], calls[1]})
    {
        const ttd::TTDTimePoint before = _ttd->CurrentPosition();
        const ttd::TTDCpuView view = _ttd->ReadCpuView(call.time, 0x8000, 0x8000);
        ASSERT_TRUE(view.ok) << view.error;
        EXPECT_EQ(view.at, call.time);
        EXPECT_EQ(_ttd->CurrentPosition(), before) << "the view leaves the machine where it stood";
        EXPECT_EQ(view.windows[3].kind, 1) << "RAM";
        EXPECT_EQ(view.windows[3].page, call.physPage) << "the page the call ran on";
        EXPECT_EQ(view.windows[2].page, 2u);

        ASSERT_TRUE(_ttd->SeekTo(call.time));
        Memory* memory = _context->pMemory;
        size_t differ = 0;
        for (size_t i = 0; i < view.bytes.size(); ++i)
            differ += view.bytes[i] != memory->DirectReadFromZ80Memory(static_cast<uint16_t>(0x8000 + i));
        EXPECT_EQ(differ, 0u);
        EXPECT_EQ(memory->MapZ80AddressToPhysicalPage(0xC000).page, view.windows[3].page);
        ASSERT_TRUE(_ttd->SeekTo(_ttd->SessionEndPosition()));
    }
}

/// The verb: Z80 addresses, at most up to 0xFFFF, the banks in the reply
TEST_F(TTDAnalysisQueries_Test, MemoryAtCpuVerb)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    const uint64_t frame = _ttd->GetSessionInfo().sessionStartFrame + 10;
    const ttd::TTDReply view = Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(frame)}, {"tinframe", "1000"},
                                                 {"offset", "0x8000"}, {"length", "4"}});
    ASSERT_TRUE(view.Ok()) << view.message;
    EXPECT_EQ(view.body.find("hex")->s, "F30E003E");
    ASSERT_EQ(view.body.find("banks")->items.size(), 4u);
    EXPECT_EQ(view.body.find("banks")->items[2].find("page")->i, 2);

    const ttd::TTDReply over = Run("memory-at", {{"space", "cpu"}, {"frame", std::to_string(frame)}, {"offset", "0xFFFF"},
                                                 {"length", "2"}});
    EXPECT_EQ(over.error, ttd::TTDControlError::BadRequest);
}

// ---------------------------------------------------------------------------
// Option parsing
// ---------------------------------------------------------------------------

TEST_F(TTDAnalysisQueries_Test, VerbsRefuseBadOptions)
{
    ASSERT_NO_FATAL_FAILURE(Record(false));
    struct Case
    {
        const char* verb;
        std::map<std::string, std::string> options;
    };
    const Case cases[] = {
        {"reverse-continue", {{"pcs", "0x8200"}, {"phys_page", "page1"}}},
        {"reverse-continue", {{"pcs", "0x8200"}, {"all", "maybe"}}},
        {"reverse-continue", {{"pcs", "0x8200"}, {"all", "true"}, {"limit", "0"}}},
        {"reverse-continue", {{"pcs", "0x8200"}, {"all", "true"}, {"limit", "10001"}}},
        {"reverse-continue", {{"pcs", "0x8200"}, {"before_tin", "100"}}},
        {"find-last", {{"addr", "0x8100"}, {"before_tin", "100"}}},
        {"find-last", {{"addr", "0x8100"}, {"phys_page", "256"}}},
        {"coverage-scan", {{"order", "sideways"}}},
        {"coverage-scan", {{"ranges", "yes please"}}},
        {"coverage-scan", {{"limit", "10001"}}},
    };
    for (const Case& c : cases)
    {
        const ttd::TTDReply r = Run(c.verb, c.options);
        std::string what = c.verb;
        for (const auto& [k, v] : c.options)
            what += " " + k + "=" + v;
        EXPECT_EQ(r.error, ttd::TTDControlError::BadRequest) << what << ": " << r.message;
    }

    // The good spellings answer
    const ttd::TTDReply rom = Run("reverse-continue", {{"pcs", "0x8200"}, {"phys_page", "rom"}, {"all", "true"}});
    ASSERT_TRUE(rom.Ok()) << rom.message;
    EXPECT_EQ(rom.body.find("count")->i, 0) << "0x8200 runs on RAM page 2";
    const ttd::TTDReply page2 = Run("reverse-continue", {{"pcs", "0x8200"}, {"phys_page", "2"}, {"all", "true"}, {"limit", "2"}});
    ASSERT_TRUE(page2.Ok()) << page2.message;
    EXPECT_EQ(page2.body.find("count")->i, 2);
    EXPECT_TRUE(page2.body.find("truncated")->b);
    EXPECT_EQ(page2.body.find("hits")->items[0].find("phys_page")->i, 2);
    const ttd::TTDReply runs = Run("coverage-scan", {{"addr_from", "0x8200"}, {"addr_to", "0x8200"}, {"phys_page", "2"},
                                                     {"order", "newest"}, {"ranges", "true"}, {"limit", "1"}});
    ASSERT_TRUE(runs.Ok()) << runs.message;
    EXPECT_EQ(runs.body.find("ranges")->items.size(), 1u);
    EXPECT_TRUE(runs.body.find("truncated")->b);
    EXPECT_TRUE(runs.body.find("next_to_frame"));
    EXPECT_EQ(runs.body.find("order")->s, "newest");
}
