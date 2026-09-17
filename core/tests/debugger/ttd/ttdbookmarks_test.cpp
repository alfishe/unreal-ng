/// @file ttdbookmarks_test.cpp
/// @brief TD-4 — agent bookmarks tests.
///
/// Per TD-4 (ttd-coverage-evaluation.md §TD-4). Acceptance criteria mapped:
///   1. "mark → seek elsewhere → return by label"      — API suite below.
///   2. "dump/load round-trips bookmarks"               — API suite below.
///   3. "a bookmark never appears as a halt_reason"     — by construction
///      (TTDSeekHaltReason has no bookmark value); tested both as the
///      success path (seek TO a bookmark → halt_reason target) and as the
///      barrier distinction (a real marker between restore point and
///      bookmark target still halts as external_event, never anything else).
///
/// Two suites, mirroring ttdexternalevents_test.cpp:
///   1. TTD_Bookmarks_Test — pure journal (no emulator): Add validation
///      (empty / overlong / duplicate labels), time-sorted Snapshot, Find,
///      Remove, DropAfter boundary semantics, Clear.
///   2. TTD_Bookmarks_API_Test — TimeTravelManager integration: bounds
///      validation, the acceptance flows, lifecycle (StartRecording /
///      InvalidateSession / ResumeRecordingFrom / DeserializeSession) and
///      .ttd round-trip.

#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdbookmarks.h"
#include "debugger/ttd/ttdexternalevents.h"
#include "debugger/ttd/timetravelmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

using SeekHaltReason = ttd::TimeTravelManager::TTDSeekHaltReason;
using SeekResult     = ttd::TimeTravelManager::TTDSeekResult;

// ===========================================================================
// Suite 1 — pure journal unit tests (no emulator, no manager).
// ===========================================================================

class TTD_Bookmarks_Test : public ::testing::Test
{
protected:
    ttd::TTDBookmarkJournal _journal;

    ttd::TTDBookmark MakeBm(uint64_t frame, uint32_t tInFrame, const std::string& label)
    {
        ttd::TTDBookmark bm;
        bm.time.frame    = frame;
        bm.time.tInFrame = tInFrame;
        bm.label         = label;
        return bm;
    }
};

TEST_F(TTD_Bookmarks_Test, InitiallyEmpty)
{
    EXPECT_TRUE(_journal.IsEmpty());
    EXPECT_EQ(_journal.Size(), 0u);
    EXPECT_TRUE(_journal.Snapshot().empty());
}

TEST_F(TTD_Bookmarks_Test, Add_StoresFields)
{
    ASSERT_TRUE(_journal.Add(MakeBm(5, 1000, "umt entry")));
    EXPECT_EQ(_journal.Size(), 1u);

    const auto bookmarks = _journal.Snapshot();
    ASSERT_EQ(bookmarks.size(), 1u);
    EXPECT_EQ(bookmarks[0].time.frame,    5u);
    EXPECT_EQ(bookmarks[0].time.tInFrame, 1000u);
    EXPECT_EQ(bookmarks[0].label, "umt entry");
}

TEST_F(TTD_Bookmarks_Test, Add_KeepsTimeSortedOrder)
{
    // Insert out of order on purpose — Snapshot must come back time-sorted.
    ASSERT_TRUE(_journal.Add(MakeBm(5, 0, "five")));
    ASSERT_TRUE(_journal.Add(MakeBm(1, 0, "one")));
    ASSERT_TRUE(_journal.Add(MakeBm(3, 0, "three")));
    ASSERT_TRUE(_journal.Add(MakeBm(3, 700, "three-mid")));  // same frame, later t

    const auto bookmarks = _journal.Snapshot();
    ASSERT_EQ(bookmarks.size(), 4u);
    EXPECT_EQ(bookmarks[0].label, "one");
    EXPECT_EQ(bookmarks[1].label, "three");
    EXPECT_EQ(bookmarks[2].label, "three-mid");
    EXPECT_EQ(bookmarks[3].label, "five");
}

TEST_F(TTD_Bookmarks_Test, Add_RejectsEmptyLabel)
{
    std::string err;
    EXPECT_FALSE(_journal.Add(MakeBm(1, 0, ""), &err));
    EXPECT_NE(err.find("empty"), std::string::npos);
    EXPECT_TRUE(_journal.IsEmpty());
}

TEST_F(TTD_Bookmarks_Test, Add_RejectsOverlongLabel)
{
    const std::string tooLong(ttd::kMaxBookmarkLabelLength + 1, 'x');
    std::string err;
    EXPECT_FALSE(_journal.Add(MakeBm(1, 0, tooLong), &err));
    EXPECT_NE(err.find("63"), std::string::npos);
    EXPECT_TRUE(_journal.IsEmpty());
}

TEST_F(TTD_Bookmarks_Test, Add_AcceptsMaxLengthLabel)
{
    const std::string maxLabel(ttd::kMaxBookmarkLabelLength, 'a');
    ASSERT_TRUE(_journal.Add(MakeBm(1, 0, maxLabel)));
    EXPECT_EQ(_journal.Snapshot()[0].label, maxLabel);
}

TEST_F(TTD_Bookmarks_Test, Add_RejectsDuplicateLabel_ReportsExistingFrame)
{
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "entry")));
    std::string err;
    EXPECT_FALSE(_journal.Add(MakeBm(9, 0, "entry"), &err));
    EXPECT_NE(err.find("already exists"), std::string::npos);
    EXPECT_NE(err.find("frame 2"), std::string::npos);
    EXPECT_EQ(_journal.Size(), 1u);  // the original is untouched
}

TEST_F(TTD_Bookmarks_Test, Find_ResolvesLabel)
{
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "entry")));
    ASSERT_TRUE(_journal.Add(MakeBm(7, 40, "exit")));

    ttd::TTDBookmark found;
    ASSERT_TRUE(_journal.Find("exit", found));
    EXPECT_EQ(found.time.frame, 7u);
    EXPECT_EQ(found.time.tInFrame, 40u);
}

TEST_F(TTD_Bookmarks_Test, Find_UnknownLabel_ReturnsFalse)
{
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "entry")));
    ttd::TTDBookmark found;
    EXPECT_FALSE(_journal.Find("nope", found));
}

TEST_F(TTD_Bookmarks_Test, Remove_ByLabel_ThenFindFails)
{
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "entry")));
    EXPECT_TRUE(_journal.Remove("entry"));
    EXPECT_TRUE(_journal.IsEmpty());

    ttd::TTDBookmark found;
    EXPECT_FALSE(_journal.Find("entry", found));

    // The label is free again after removal.
    EXPECT_TRUE(_journal.Add(MakeBm(3, 0, "entry")));
}

TEST_F(TTD_Bookmarks_Test, Remove_UnknownLabel_ReturnsFalse)
{
    EXPECT_FALSE(_journal.Remove("ghost"));
}

TEST_F(TTD_Bookmarks_Test, DropAfter_RemovesStrictlyFuture_KeepsBoundary)
{
    ASSERT_TRUE(_journal.Add(MakeBm(1, 0, "past")));
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "at-boundary")));
    ASSERT_TRUE(_journal.Add(MakeBm(2, 1, "just-after")));
    ASSERT_TRUE(_journal.Add(MakeBm(4, 0, "future")));

    _journal.DropAfter(ttd::TTDTimePoint{2, 0});

    const auto bookmarks = _journal.Snapshot();
    ASSERT_EQ(bookmarks.size(), 2u);
    EXPECT_EQ(bookmarks[0].label, "past");
    EXPECT_EQ(bookmarks[1].label, "at-boundary");
}

TEST_F(TTD_Bookmarks_Test, Clear_ResetsToEmpty)
{
    ASSERT_TRUE(_journal.Add(MakeBm(1, 0, "a")));
    ASSERT_TRUE(_journal.Add(MakeBm(2, 0, "b")));
    _journal.Clear();
    EXPECT_TRUE(_journal.IsEmpty());
}

// ===========================================================================
// Suite 2 — TimeTravelManager integration.
// ===========================================================================

class TTD_Bookmarks_API_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    FeatureManager* _fm = nullptr;
    Memory* _memory = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        _ttd = _context->pTimeTravelManager;
        ASSERT_NE(_ttd, nullptr);
        _memory = _context->pMemory;
        ASSERT_NE(_memory, nullptr);
        _fm = _emulator->GetFeatureManager();
        ASSERT_NE(_fm, nullptr);

        _fm->setFeature(Features::kDebugMode, true);
        _fm->setFeature(Features::kTimeTravel, true);
        _memory->UpdateFeatureCache();
    }

    void TearDown() override
    {
        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
        }
    }

    void RunFrames(size_t n)
    {
        _emulator->RunNFrames(static_cast<unsigned>(n), /*skipBreakpoints=*/true);
    }
};

// ---------------------------------------------------------------------------
// Bounds validation at creation — bad bookmarks can never exist.
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, AddBookmark_EmptyTimeline_Rejected)
{
    std::string err;
    EXPECT_FALSE(_ttd->AddBookmark({1, 0}, "boot", &err));
    EXPECT_NE(err.find("no recorded history"), std::string::npos) << err;
    EXPECT_TRUE(_ttd->GetBookmarks().empty());
}

TEST_F(TTD_Bookmarks_API_Test, AddBookmark_BeyondSessionEnd_Rejected)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    std::string err;
    EXPECT_FALSE(_ttd->AddBookmark({10, 0}, "future", &err));
    EXPECT_NE(err.find("beyond the session end"), std::string::npos) << err;
    EXPECT_TRUE(_ttd->GetBookmarks().empty());
}

TEST_F(TTD_Bookmarks_API_Test, AddBookmark_Valid_ReportsInSessionInfo)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    std::string err;
    ASSERT_TRUE(_ttd->AddBookmark({2, 0}, "boot", &err)) << err;

    EXPECT_EQ(_ttd->GetSessionInfo().bookmarkCount, 1u);
    const auto bookmarks = _ttd->GetBookmarks();
    ASSERT_EQ(bookmarks.size(), 1u);
    EXPECT_EQ(bookmarks[0].label, "boot");
    EXPECT_EQ(bookmarks[0].time.frame, 2u);
}

// ---------------------------------------------------------------------------
// Acceptance #1 — mark → seek elsewhere → return by label.
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, MarkThenSeekElsewhere_ReturnByLabel)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    // Mark.
    std::string err;
    ASSERT_TRUE(_ttd->AddBookmark({2, 0}, "umt entry", &err)) << err;

    // Seek elsewhere — forward past the bookmark.
    SeekResult fwd;
    EXPECT_TRUE(_ttd->SeekTo({4, 0}, &fwd));
    EXPECT_TRUE(fwd.reached);
    EXPECT_EQ(fwd.haltReason, SeekHaltReason::Target);

    // Return by label.
    SeekResult back;
    EXPECT_TRUE(_ttd->SeekToBookmark("umt entry", &back));
    EXPECT_TRUE(back.reached);
    EXPECT_EQ(back.arrivedAt.frame, 2u);
    EXPECT_EQ(back.arrivedAt.tInFrame, 0u);
    // A bookmark is advisory: reaching one is a plain Target arrival.
    EXPECT_EQ(back.haltReason, SeekHaltReason::Target);
}

TEST_F(TTD_Bookmarks_API_Test, SeekAcrossBookmark_NeverBlocks)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->AddBookmark({2, 0}, "passed-over"));

    // Forward past the bookmark, then backward before it — neither direction
    // may be influenced by a bookmark (contrast with external-event markers).
    SeekResult fwd;
    EXPECT_TRUE(_ttd->SeekTo({4, 0}, &fwd));
    EXPECT_EQ(fwd.haltReason, SeekHaltReason::Target);

    SeekResult back;
    EXPECT_TRUE(_ttd->SeekTo({1, 0}, &back));
    EXPECT_EQ(back.haltReason, SeekHaltReason::Target);
}

// ---------------------------------------------------------------------------
// Label resolution failures.
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, SeekToBookmark_UnknownLabel_Fails)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    SeekResult r;
    std::string err;
    EXPECT_FALSE(_ttd->SeekToBookmark("ghost", &r, &err));
    EXPECT_NE(err.find("unknown bookmark"), std::string::npos) << err;
    EXPECT_FALSE(r.reached);
}

TEST_F(TTD_Bookmarks_API_Test, SeekToBookmark_AfterRemove_Fails)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->AddBookmark({2, 0}, "gone-soon"));
    EXPECT_TRUE(_ttd->RemoveBookmark("gone-soon"));

    SeekResult r;
    std::string err;
    EXPECT_FALSE(_ttd->SeekToBookmark("gone-soon", &r, &err));
    EXPECT_NE(err.find("unknown bookmark"), std::string::npos) << err;
}

// ---------------------------------------------------------------------------
// Acceptance #3 — a bookmark never appears as a halt_reason. A real marker
// between the restore point and the bookmark target still halts as
// external_event; the bookmark neither masks nor replaces the barrier.
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, SeekToBookmark_BehindRealMarker_HaltsAtMarkerNotBookmark)
{
    // Same timeline shape as TTD_ExternalEvents_Seek_Test.IntraFrame_MarkerInInterval_
    // StopsAtMarker: checkpoint at (1,0), marker at (1,M), timeline past it.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(1);
    _emulator->RunTStates(500, true);
    _ttd->RecordExternalEvent(ttd::TTDExternalEventKind::DiskWrite, "wd1793 write");
    RunFrames(2);
    _ttd->StopRecording();

    ASSERT_EQ(_ttd->GetExternalEvents().Size(), 1u);
    const uint32_t markerT = _ttd->GetExternalEvents().Events()[0].time.tInFrame;
    ASSERT_GT(markerT, 0u);

    // Bookmark strictly past the marker, same frame.
    const uint32_t bmT = markerT + 500;
    ASSERT_TRUE(_ttd->AddBookmark({1, bmT}, "post-marker"));

    SeekResult r;
    std::string err;
    EXPECT_FALSE(_ttd->SeekToBookmark("post-marker", &r, &err));
    EXPECT_FALSE(r.reached);
    // The real marker is the halt reason — never the bookmark.
    EXPECT_EQ(r.haltReason, SeekHaltReason::ExternalEvent);
    EXPECT_EQ(r.arrivedAt.frame, 1u);
    EXPECT_EQ(r.arrivedAt.tInFrame, markerT);
    EXPECT_STREQ(r.blockingMarker.reason, "wd1793 write");
}

// ---------------------------------------------------------------------------
// Lifecycle — mirrors the journals.
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, InvalidateSession_ClearsBookmarks)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->AddBookmark({1, 0}, "boot"));
    _ttd->InvalidateSession("test");
    EXPECT_TRUE(_ttd->GetBookmarks().empty());
    EXPECT_EQ(_ttd->GetSessionInfo().bookmarkCount, 0u);
}

TEST_F(TTD_Bookmarks_API_Test, StopRecording_KeepsBookmarks_FreshStartRecordingClears)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->AddBookmark({1, 0}, "browse-mark"));

    // Browsing after StopRecording keeps annotations.
    EXPECT_EQ(_ttd->GetBookmarks().size(), 1u);

    // A fresh capture starts from a clean slate.
    ASSERT_TRUE(_ttd->StartRecording());
    EXPECT_TRUE(_ttd->GetBookmarks().empty());
}

TEST_F(TTD_Bookmarks_API_Test, ResumeFromPast_DropsFutureBookmarks_KeepsPast)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(5);
    _ttd->StopRecording();

    ASSERT_TRUE(_ttd->AddBookmark({1, 0}, "early"));
    ASSERT_TRUE(_ttd->AddBookmark({4, 0}, "late"));

    SeekResult r;
    ASSERT_TRUE(_ttd->SeekTo({2, 0}, &r));
    ASSERT_EQ(_ttd->GetState(), ttd::TTDSessionState::Detached);

    // Resuming from (2,0) rewinds the timeline: bookmarks strictly after the
    // resume point describe history that no longer exists.
    ASSERT_TRUE(_ttd->ResumeRecordingFrom({2, 0}));

    const auto bookmarks = _ttd->GetBookmarks();
    ASSERT_EQ(bookmarks.size(), 1u);
    EXPECT_EQ(bookmarks[0].label, "early");
    EXPECT_EQ(bookmarks[0].time.frame, 1u);
}

// ---------------------------------------------------------------------------
// Acceptance #2 — dump/load round-trips bookmarks (schema-additive section).
// ---------------------------------------------------------------------------

TEST_F(TTD_Bookmarks_API_Test, SerializeDeserialize_RoundTripPreservesBookmarks)
{
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(3);
    _ttd->StopRecording();

    std::string err;
    ASSERT_TRUE(_ttd->AddBookmark({1, 0}, "boot", &err)) << err;
    ASSERT_TRUE(_ttd->AddBookmark({2, 300}, "mid", &err)) << err;

    std::ostringstream out;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    std::istringstream in(out.str());
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;

    const auto bookmarks = _ttd->GetBookmarks();
    ASSERT_EQ(bookmarks.size(), 2u);
    EXPECT_EQ(bookmarks[0].label, "boot");
    EXPECT_EQ(bookmarks[0].time.frame, 1u);
    EXPECT_EQ(bookmarks[0].time.tInFrame, 0u);
    EXPECT_EQ(bookmarks[1].label, "mid");
    EXPECT_EQ(bookmarks[1].time.frame, 2u);
    EXPECT_EQ(bookmarks[1].time.tInFrame, 300u);
    EXPECT_EQ(_ttd->GetSessionInfo().bookmarkCount, 2u);

    // Loaded bookmarks remain seekable.
    SeekResult r;
    EXPECT_TRUE(_ttd->SeekToBookmark("mid", &r));
    EXPECT_TRUE(r.reached);
    EXPECT_EQ(r.arrivedAt.frame, 2u);
    EXPECT_EQ(r.arrivedAt.tInFrame, 300u);
    EXPECT_EQ(r.haltReason, SeekHaltReason::Target);
}

TEST_F(TTD_Bookmarks_API_Test, LoadFileWithoutBookmarks_ClearsInMemoryOnes)
{
    // A dump taken before any bookmark existed must load cleanly (flag bit
    // off) — and must not resurrect bookmarks added after the dump.
    ASSERT_TRUE(_ttd->StartRecording());
    RunFrames(2);
    _ttd->StopRecording();

    std::ostringstream out;
    std::string err;
    ASSERT_TRUE(_ttd->SerializeSession(out, err)) << err;

    ASSERT_TRUE(_ttd->AddBookmark({1, 0}, "ephemeral"));

    std::istringstream in(out.str());
    ASSERT_TRUE(_ttd->DeserializeSession(in, err)) << err;
    EXPECT_TRUE(_ttd->GetBookmarks().empty());
    EXPECT_EQ(_ttd->GetSessionInfo().bookmarkCount, 0u);
}
