// The renderer is a Screen subclass chosen by model family (PLAN #60(e),
// VideoController::CreateScreen): TS-Conf gets ScreenTSConf, every classic
// machine ScreenZX.

#include <gtest/gtest.h>

#include <memory>

#include "_helpers/emulatortesthelper.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/video/tsconf/screentsconf.h"
#include "emulator/video/videocontroller.h"
#include "emulator/video/zx/screenzx.h"

TEST(VideoController_Test, TsConfGetsItsOwnScreenEveryOtherModelTheZxScreen)
{
    // TSL is not creatable yet (no port decoder): the factory is exercised
    // with one machine's context, which is all a screen's constructor needs
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);

    for (int m = 0; m < N_MM_MODELS; m++)
    {
        const MEM_MODEL model = static_cast<MEM_MODEL>(m);
        SCOPED_TRACE(m);
        std::unique_ptr<Screen> screen(VideoController::CreateScreen(model, emulator->GetContext()));
        ASSERT_NE(screen, nullptr);
        EXPECT_NE(dynamic_cast<ScreenZX*>(screen.get()), nullptr) << "every screen is a ZX screen with extra modes so far";
        EXPECT_EQ(dynamic_cast<ScreenTSConf*>(screen.get()) != nullptr, model == MM_TSL);
    }

    EmulatorTestHelper::CleanupEmulator(emulator);
}

TEST(VideoController_Test, TheCoreBuildsTheModelsScreen)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    Screen* screen = emulator->GetContext()->pScreen;
    EXPECT_NE(dynamic_cast<ScreenZX*>(screen), nullptr);
    EXPECT_EQ(dynamic_cast<ScreenTSConf*>(screen), nullptr);
    EmulatorTestHelper::CleanupEmulator(emulator);
}
