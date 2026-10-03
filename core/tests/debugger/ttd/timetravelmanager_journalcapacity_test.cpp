/// @file timetravelmanager_journalcapacity_test.cpp
/// @brief The write journal ring's size (TimeTravelManager::SetWriteJournalCapacity):
/// a small ring drops the oldest writes, a ring large enough keeps every one,
/// and a session saved with more writes than the loading machine's ring holds
/// loads whole. Input of experiment E7 (TTD v2, Phase 3, Step 7).
///
/// Boots a Pentagon and records 100 frames of the ROM's memory test: slower
/// than the 50 ms guideline, one check of a session-wide property.

#include <gtest/gtest.h>

#include <sstream>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdwritejournal.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"

namespace
{
Emulator* Start()
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    if (!emulator)
        return nullptr;
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    emulator->GetContext()->pMemory->UpdateFeatureCache();
    return emulator;
}

/// Record @p frames of a cold boot with a ring of @p bytes; the session's journal
const ttd::TTDWriteJournal* Record(Emulator* emulator, size_t bytes, int frames)
{
    ttd::TimeTravelManager* ttd = emulator->GetContext()->pTimeTravelManager;
    EXPECT_TRUE(ttd->SetWriteJournalCapacity(bytes));
    EXPECT_TRUE(ttd->StartRecording());
    EXPECT_FALSE(ttd->SetWriteJournalCapacity(bytes * 2)) << "not while a session exists";
    emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
    ttd->StopRecording();
    return ttd->GetWriteJournal();
}
}  // namespace

TEST(TimeTravelManager_JournalCapacity_Test, ASmallRingWraps_ALargeOneKeepsEveryWrite)
{
    Emulator* small = Start();
    ASSERT_NE(small, nullptr);
    const ttd::TTDWriteJournal* wrapped = Record(small, 64 * 1024, 100);
    ASSERT_NE(wrapped, nullptr);
    EXPECT_TRUE(wrapped->HasEvictedRecords()) << "the boot writes more than 5,461 records";
    const size_t smallCapacity = wrapped->Capacity();

    Emulator* large = Start();
    ASSERT_NE(large, nullptr);
    const ttd::TTDWriteJournal* whole = Record(large, 16u * 1024 * 1024, 100);
    ASSERT_NE(whole, nullptr);
    EXPECT_FALSE(whole->HasEvictedRecords());
    EXPECT_GT(whole->Size(), smallCapacity);

    // Saved, then loaded into a machine whose ring is the small one: every record arrives
    std::stringstream file;
    std::string err;
    ASSERT_TRUE(large->GetContext()->pTimeTravelManager->SerializeSession(file, err)) << err;
    const size_t recorded = whole->Size();
    EmulatorTestHelper::CleanupEmulator(large);

    Emulator* reader = Start();
    ASSERT_NE(reader, nullptr);
    ttd::TimeTravelManager* ttd = reader->GetContext()->pTimeTravelManager;
    ASSERT_TRUE(ttd->SetWriteJournalCapacity(64 * 1024));
    ASSERT_TRUE(ttd->DeserializeSession(file, err)) << err;
    ASSERT_NE(ttd->GetWriteJournal(), nullptr);
    EXPECT_EQ(ttd->GetWriteJournal()->Size(), recorded);
    EXPECT_FALSE(ttd->GetWriteJournal()->HasEvictedRecords());

    EmulatorTestHelper::CleanupEmulator(reader);
    EmulatorTestHelper::CleanupEmulator(small);
}
