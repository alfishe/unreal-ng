/// @file timetravelmanager_journalcapacity_test.cpp
/// @brief The write journal ring's size (TimeTravelController::SetWriteJournalCapacity)
/// on the engine: the ring holds the frame being recorded and is drained into the
/// engine's write index at each boundary, so its size does not cut the session's
/// journal - a small ring and a large one keep the same writes, and a session
/// loads whole into a machine with a small ring. (v1's ring held the whole
/// session and dropped the oldest writes: experiment E7, Phase 3, Step 7.)
///
/// Boots a Pentagon and records 100 frames of the ROM's memory test: slower
/// than the 50 ms guideline, one check of a session-wide property.

#include <gtest/gtest.h>

#include <sstream>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/ttdwriterecords.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
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

/// Record @p frames of a cold boot with a ring of @p bytes; the session's journal records
size_t Record(Emulator* emulator, size_t bytes, int frames)
{
    ttd::TimeTravelController* ttd = emulator->GetContext()->pTimeTravelController;
    EXPECT_TRUE(ttd->SetWriteJournalCapacity(bytes));
    ttd->SetEnableWriteJournal(true);
    EXPECT_TRUE(ttd->StartRecording());
    EXPECT_FALSE(ttd->SetWriteJournalCapacity(bytes * 2)) << "not while a session exists";
    emulator->RunNFrames(frames, /*skipBreakpoints=*/true);
    ttd->StopRecording();
    return ttdtest::WriteRecordCount(*ttd);
}
}  // namespace

TEST(TimeTravelManager_JournalCapacity_Test, TheRingSizeDoesNotCutTheSessionsJournal)
{
    Emulator* small = Start();
    ASSERT_NE(small, nullptr);
    const size_t smallRing = Record(small, 64 * 1024, 100);
    EXPECT_GT(smallRing, 5461u) << "the boot writes more than a 64 KB ring holds";

    Emulator* large = Start();
    ASSERT_NE(large, nullptr);
    const size_t largeRing = Record(large, 16u * 1024 * 1024, 100);
    EXPECT_EQ(smallRing, largeRing) << "drained at each boundary: the ring's size does not cut the journal";

    // Saved, then loaded into a machine whose ring is the small one: every record arrives
    std::stringstream file;
    std::string err;
    ASSERT_TRUE(large->GetContext()->pTimeTravelController->SerializeSession(file, err)) << err;
    EmulatorTestHelper::CleanupEmulator(large);

    Emulator* reader = Start();
    ASSERT_NE(reader, nullptr);
    ttd::TimeTravelController* ttd = reader->GetContext()->pTimeTravelController;
    ASSERT_TRUE(ttd->SetWriteJournalCapacity(64 * 1024));
    ASSERT_TRUE(ttd->DeserializeSession(file, err)) << err;
    EXPECT_EQ(ttdtest::WriteRecordCount(*ttd), largeRing);

    EmulatorTestHelper::CleanupEmulator(reader);
    EmulatorTestHelper::CleanupEmulator(small);
}
