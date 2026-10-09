/// @file timetravelmanager_screenshot_test.cpp
/// @brief The shadow engine's screenshot stream (frame-boundary stream 0,
/// phase-4 TDD §5.4): off until switched on; on, every frame's copy is the
/// framebuffer at that boundary (width, height and video mode in front).
///
/// Boots a Pentagon and records a few dozen frames: slower than the 50 ms
/// guideline, one acceptance check.

#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/ttd/timetravelengine.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/memory/memory.h"
#include "emulator/video/screen.h"

TEST(TimeTravelManager_Screenshot_Test, EveryFrameIsTheFramebufferAtItsBoundary)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    EmulatorContext* context = emulator->GetContext();
    emulator->GetFeatureManager()->setFeature(Features::kDebugMode, true);
    emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
    context->pMemory->UpdateFeatureCache();
    ttd::TimeTravelController* ttd = context->pTimeTravelController;
    ttd::TimeTravelEngine engine;
    ttd->SetShadowEngine(&engine);
    ASSERT_TRUE(ttd->StartRecording());
    emulator->RunNFrames(30, true);   // off: nothing captured
    EXPECT_EQ(engine.Streams().CaptureCalls(), 0u);
    ASSERT_TRUE(engine.Streams().IsRegistered(ttd::TimeTravelController::kScreenshotStream));
    engine.Streams().SetEnabled(ttd::TimeTravelController::kScreenshotStream, true);

    for (int i = 0; i < 20; ++i)
    {
        emulator->RunNFrames(1, true);
        const uint64_t frame = context->emulatorState.frame_counter;
        const FramebufferDescriptor& fb = context->pScreen->GetFramebufferDescriptor();
        std::vector<uint8_t> copy;
        ASSERT_TRUE(engine.FrameStreamCopy(ttd::TimeTravelController::kScreenshotStream, frame, copy)) << "frame " << frame;
        ASSERT_EQ(copy.size(), 5 + fb.memoryBufferSize);
        uint16_t width = 0, height = 0;
        std::memcpy(&width, copy.data(), 2);
        std::memcpy(&height, copy.data() + 2, 2);
        EXPECT_EQ(width, fb.width);
        EXPECT_EQ(height, fb.height);
        ASSERT_EQ(std::memcmp(copy.data() + 5, fb.memoryBuffer, fb.memoryBufferSize), 0) << "frame " << frame;
    }
    EXPECT_EQ(engine.Streams().CaptureCalls(), 20u);
    ttd->StopRecording();
    ttd->SetShadowEngine(nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
