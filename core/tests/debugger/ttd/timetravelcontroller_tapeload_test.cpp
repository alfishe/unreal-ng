/// @file timetravelcontroller_tapeload_test.cpp
/// @brief A tape loaded while a recording runs lands at the next frame boundary (an event of the recording there,
/// M3), so a play right after the load (the WebAPI / MCP load_software autostart) found no blocks and failed. Play is
/// now queued for a tape on its way and starts when the tape lands; a stop drops it.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/tape/tape.h"
#include "emulator/memory/memory.h"

class TimeTravelController_TapeLoad_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        _ttd = _context->pTimeTravelController;
        ASSERT_NE(_ttd, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    static std::string Tape()
    {
        return (TestPathHelper::FindProjectRoot() / "testdata/contention/rak-timing-test/timing.tap").string();
    }
};

TEST_F(TimeTravelController_TapeLoad_Test, APlayRightAfterALoadWhileRecordingStartsWhenTheTapeLands)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(Tape(), &error)) << error;
    ASSERT_TRUE(_ttd->IsRecording());
    EXPECT_EQ(_context->pTape->Play(), TapePlayResult::Queued) << "the tape is on its way to the deck";
    EXPECT_FALSE(_context->pTape->IsPlaying());

    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_FALSE(_context->coreState.tapeFilePath.empty()) << "the tape landed at the frame boundary";
    EXPECT_TRUE(_context->pTape->IsPlaying()) << "the queued play started with it";
}

TEST_F(TimeTravelController_TapeLoad_Test, AStopDropsAQueuedPlay)
{
    ASSERT_TRUE(_ttd->StartRecording());
    _emulator->RunNFrames(3, /*skipBreakpoints=*/true);

    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(Tape(), &error)) << error;
    ASSERT_EQ(_context->pTape->Play(), TapePlayResult::Queued);
    _context->pTape->stopTape();

    _emulator->RunNFrames(1, /*skipBreakpoints=*/true);
    EXPECT_FALSE(_context->coreState.tapeFilePath.empty());
    EXPECT_FALSE(_context->pTape->IsPlaying());
}

TEST_F(TimeTravelController_TapeLoad_Test, WithoutARecordingTheTapeIsInAtOnce)
{
    std::string error;
    ASSERT_TRUE(_emulator->LoadTape(Tape(), &error)) << error;
    EXPECT_EQ(_context->pTape->Play(), TapePlayResult::Started);
    EXPECT_TRUE(_context->pTape->IsPlaying());
}
