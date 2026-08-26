/// @file ttd_write_journal_e2e_test.cpp
/// @brief End-to-end coverage for the TTD write journal.
///
/// The existing TTDWriteJournal_Test suite exercises the container: append,
/// query, DropAfter, Clear, serialize. Nothing covered the path that fills it -
/// a running Z80 writing to RAM through Memory::MemoryWriteDebug, which calls
/// TimeTravelManager::RecordMemoryWrite.
///
/// The fixture deliberately enables nothing but the timetravel feature: turning
/// it on must bring its dependencies with it. FeatureManager::setFeature()
/// auto-enables the master debugmode flag, and onFeatureChanged() refreshes
/// Memory's cache, sets Z80::isDebugMode and installs the instrumented memory
/// interface. These tests fail if any link in that chain is dropped.

#include <gtest/gtest.h>

#include <cstdint>

#include "base/featuremanager.h"
#include "common/modulelogger.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttd_dirty_tracker.h"
#include "debugger/ttd/ttd_write_journal.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/mainloop.h"
#include "emulator/memory/memory.h"
#include "emulator/platform.h"

namespace
{

class TTD_WriteJournal_E2E_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;
    Memory* _memory = nullptr;
    FeatureManager* _fm = nullptr;
    MainLoop_CUT* _mainloop = nullptr;

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

        // ONLY the time-travel feature. Everything it needs - the master
        // debugmode flag, Memory's feature cache, Z80::isDebugMode and the
        // instrumented memory interface - must come with it.
        _fm->setFeature(Features::kTimeTravel, true);

        _mainloop = new MainLoop_CUT(_context);

        SkipRomStartupDelay();
    }

    void TearDown() override
    {
        delete _mainloop;
        _mainloop = nullptr;

        if (_emulator)
        {
            _emulator->Stop();
            _emulator->Release();
            delete _emulator;
            _emulator = nullptr;
        }
    }

    /// The ROM opens with a delay loop - LD BC,#692B / DEC BC / LD A,B / OR C /
    /// JR NZ - about 27000 iterations that touch no memory at all. A test running
    /// a couple of frames from reset therefore sees an empty journal and looks
    /// like a broken recorder. Step over the loop rather than paying for it: BC
    /// is the counter, so setting it to 1 makes the next DEC BC fall through and
    /// the ROM moves on to clearing RAM, which is what writes.
    void SkipRomStartupDelay()
    {
        Z80* z80 = _context->pCore->GetZ80();

        // Reach the loop body: PC lands on DEC BC at #0004.
        for (int i = 0; i < 8 && z80->pc < 0x0004; ++i)
            z80->Z80Step(true);

        if (z80->pc == 0x0004)
            z80->bc = 1;
    }

    /// Frames go through MainLoop, the path the emulator really uses.
    void RunFrames(size_t n)
    {
        for (size_t i = 0; i < n; ++i)
            _mainloop->RunFramePublic();
    }
};

}  // namespace

/// The journal must fill from executed code, not only from direct Memory pokes.
TEST_F(TTD_WriteJournal_E2E_Test, RunningCode_FillsJournal)
{
    _ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(_ttd->StartRecording());

    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr) << "journal not allocated although it was enabled before StartRecording";
    ASSERT_EQ(journal->Size(), 0u) << "journal should start empty";

    RunFrames(4);

    EXPECT_GT(journal->Size(), 0u)
        << "executed frames produced no journal records - "
        << "RecordMemoryWrite is not reached from the write hot path";
}

/// Dirty tracking is the sibling call in the same hot-path branch; if it is dead
/// the checkpoints hold no changed pages.
TEST_F(TTD_WriteJournal_E2E_Test, RunningCode_MarksPagesDirty)
{
    ASSERT_TRUE(_ttd->StartRecording());

    RunFrames(4);

    ttd::TTDDirtyTracker* tracker = _memory->GetTTDDirtyTracker();
    ASSERT_NE(tracker, nullptr);

    int everDirty = 0;
    for (uint16_t page = 0; page < MAX_RAM_PAGES; ++page)
        if (tracker->WasEverDirty(page))
            ++everDirty;

    EXPECT_GT(everDirty, 0) << "no RAM page was marked dirty by executed writes";
}

/// The flag has to gate the hot path, not merely the getter.
TEST_F(TTD_WriteJournal_E2E_Test, JournalDisabled_RecordsNothing)
{
    _ttd->SetEnableWriteJournal(false);
    ASSERT_TRUE(_ttd->StartRecording());

    RunFrames(4);

    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    if (journal != nullptr)
    {
        EXPECT_EQ(journal->Size(), 0u) << "records were journalled although the journal is disabled";
    }
}

/// Records must carry usable data - a journal of zeroed entries would pass a size
/// check while being useless for reverse watchpoints.
TEST_F(TTD_WriteJournal_E2E_Test, Records_CarryUsableFields)
{
    _ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(_ttd->StartRecording());

    RunFrames(4);

    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr);
    ASSERT_GT(journal->Size(), 0u);

    auto latest = journal->FindLast(UINT64_MAX, [](const ttd::TTDWriteRecord& r) { return r.isIo == 0; });
    ASSERT_TRUE(latest.has_value()) << "no memory-write record found";

    EXPECT_GT(latest->globalT, 0u) << "globalT not stamped";
    EXPECT_LT(latest->physPage, MAX_RAM_PAGES) << "physical page out of range";
}

/// globalT is the ordering key every reverse query relies on.
TEST_F(TTD_WriteJournal_E2E_Test, GlobalT_AdvancesAcrossFrames)
{
    _ttd->SetEnableWriteJournal(true);
    ASSERT_TRUE(_ttd->StartRecording());

    RunFrames(2);
    const ttd::TTDWriteJournal* journal = _ttd->GetWriteJournal();
    ASSERT_NE(journal, nullptr);
    ASSERT_GT(journal->Size(), 0u);

    auto first = journal->FindLast(UINT64_MAX, [](const ttd::TTDWriteRecord&) { return true; });
    ASSERT_TRUE(first.has_value());
    const uint64_t afterFirstFrames = first->globalT;

    RunFrames(2);
    auto second = journal->FindLast(UINT64_MAX, [](const ttd::TTDWriteRecord&) { return true; });
    ASSERT_TRUE(second.has_value());

    EXPECT_GT(second->globalT, afterFirstFrames)
        << "globalT did not advance between frames - reverse queries cannot order records";
}
