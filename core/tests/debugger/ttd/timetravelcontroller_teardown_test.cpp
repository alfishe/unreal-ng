/// @file timetravelcontroller_teardown_test.cpp
/// @brief A TTD controller that goes away unbinds what it bound in the machine: the media manager's read journal
/// (its adapter) and the CPU's interrupt-vector journal (its engine). Both outlive it in Emulator::Release, and a
/// sector read after it would have reached freed memory.

#include <gtest/gtest.h>

#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/media/mediamanager.h"
#include "emulator/memory/memory.h"

TEST(TimeTravelController_Teardown_Test, AGoneControllerLeavesNoJournalBound)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::ITimeTravelHooks* own = context->pTimeTravelHooks;
    ttd::ITTDWriteSink* ownSink = context->ttdWriteSink;

    auto controller = std::make_unique<ttd::TimeTravelController>(context);
    context->pTimeTravelHooks = controller.get();
    context->ttdWriteSink = controller.get();
    ASSERT_TRUE(controller->StartRecording());
    emulator->RunNFrames(2, /*skipBreakpoints=*/true);
    ASSERT_NE(context->pMediaManager->GetReadJournal(), nullptr) << "a recording journals the media reads";

    context->pTimeTravelHooks = own;
    context->ttdWriteSink = ownSink;
    controller.reset();   // recording: the destructor runs on a live session
    EXPECT_EQ(context->pMediaManager->GetReadJournal(), nullptr);
    EXPECT_EQ(context->ttdVectors, nullptr);

    EmulatorTestHelper::CleanupEmulator(emulator);
}
