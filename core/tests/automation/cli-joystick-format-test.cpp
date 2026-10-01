/// @file cli-joystick-format-test.cpp
/// @brief Kempston joystick CLI (joystick TDD §5, JOY-13): the exact command text run against a real
/// ZX-Evo (ATM3) instance, so the state change and the errors are the manager's, as on every surface.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../automation/cli/src/commands/cli-joystick-format.h"
#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"

namespace
{
const std::string kNl = "\n";
}

class CliJoystick_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    DebugJoystickManager* _manager = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM3", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        _manager = _context->pDebugManager->GetJoystickManager();
        ASSERT_NE(_manager, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    std::string Run(std::vector<std::string> args)
    {
        return CliJoystick::Execute(*_manager, args, kNl);
    }
};

TEST(CliJoystickParse_Test, ParseIntegerAcceptsDecimalAndHexOnly)
{
    long long value = 0;
    EXPECT_TRUE(CliJoystick::ParseInteger("24", value));
    EXPECT_EQ(value, 24);
    EXPECT_TRUE(CliJoystick::ParseInteger("0x18", value));
    EXPECT_EQ(value, 24);
    EXPECT_TRUE(CliJoystick::ParseInteger("#18", value));
    EXPECT_EQ(value, 24);
    EXPECT_TRUE(CliJoystick::ParseInteger("-3", value));
    EXPECT_EQ(value, -3);
    EXPECT_FALSE(CliJoystick::ParseInteger("", value));
    EXPECT_FALSE(CliJoystick::ParseInteger("up", value));
    EXPECT_FALSE(CliJoystick::ParseInteger("5x", value));
    EXPECT_FALSE(CliJoystick::ParseInteger(" 5", value));
}

TEST_F(CliJoystick_Test, Press_HoldsButtonsAndReportsState)
{
    const std::string text = Run({"press", "up+fire"});
    EXPECT_EQ(text, "Joystick pressed: up,fire -> state=0x18 buttons=up,fire (IN #1F=0x18)" + kNl);
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);
}

TEST_F(CliJoystick_Test, Press_SeveralTokensAreOneList)
{
    Run({"press", "left", "fire"});
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kLeft | Joystick::kFire);
}

TEST_F(CliJoystick_Test, Release_DropsOnlyTheNamedButtons)
{
    Run({"press", "up+fire"});
    const std::string text = Run({"release", "up"});
    EXPECT_NE(text.find("Joystick released: up -> state=0x10"), std::string::npos) << text;
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kFire);
}

TEST_F(CliJoystick_Test, Set_TakesAByteAHexByteAListOrNone)
{
    EXPECT_NE(Run({"set", "5"}).find("state=0x05"), std::string::npos);
    EXPECT_EQ(_context->pJoystick->State(), 0x05);
    Run({"set", "0xE3"});
    EXPECT_EQ(_context->pJoystick->State(), 0xE3) << "D5..D7 included";
    Run({"set", "up,fire"});
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);
    Run({"set", "none"});
    EXPECT_EQ(_context->pJoystick->State(), 0x00);
}

TEST_F(CliJoystick_Test, Tap_PressesNowAndReleasesAfterTheFrames)
{
    const std::string text = Run({"tap", "fire", "3"});
    EXPECT_NE(text.find("Joystick tap: fire for 3 frames"), std::string::npos) << text;
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kFire);
    EXPECT_TRUE(_manager->IsTapPending());
    for (int i = 0; i < 3; i++)
        _manager->OnFrame();
    EXPECT_EQ(_context->pJoystick->State(), 0x00);
}

TEST_F(CliJoystick_Test, Tap_DefaultsToTwoFramesAndAcceptsAList)
{
    Run({"tap", "up", "fire"});
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(_manager->GetState().pendingTapFramesLeft, DebugJoystickManager::DEFAULT_TAP_FRAMES);
}

TEST_F(CliJoystick_Test, Clear_ReleasesEverythingAndCancelsATap)
{
    Run({"set", "0xFF"});
    Run({"tap", "up", "9"});
    const std::string text = Run({"clear"});
    EXPECT_NE(text.find("state=0x00 buttons=none"), std::string::npos) << text;
    EXPECT_FALSE(_manager->IsTapPending());
}

TEST_F(CliJoystick_Test, Status_ShowsStateRoutingKeysAndTap)
{
    Run({"press", "up"});
    const std::string text = Run({"status"});
    EXPECT_NE(text.find("Kempston Joystick [present]"), std::string::npos) << text;
    EXPECT_NE(text.find("State: 0x08 (up)"), std::string::npos) << text;
    EXPECT_NE(text.find("Port: IN #1F=0x08"), std::string::npos) << text;
    EXPECT_NE(text.find("this machine decodes the Kempston joystick port"), std::string::npos) << text;
    EXPECT_NE(text.find("up:kp_8"), std::string::npos) << text;
    EXPECT_NE(text.find("Pending tap: none"), std::string::npos) << text;
}

TEST_F(CliJoystick_Test, List_NamesAllEightButtons)
{
    const std::string text = Run({"list"});
    for (const char* name : {"up", "down", "left", "right", "fire", "b5", "b6", "b7"})
        EXPECT_NE(text.find(std::string("  ") + name + " (0x"), std::string::npos) << name;
}

/// JOY-13: the same errors as every other surface, worded by the manager
TEST_F(CliJoystick_Test, Errors_UseTheManagerMessages)
{
    EXPECT_EQ(Run({"press", "jump"}),
              "Error: unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)" + kNl);
    EXPECT_EQ(Run({"set", "300"}), "Error: state=300 out of range 0..255" + kNl);
    EXPECT_EQ(Run({"set", "-1"}), "Error: state=-1 out of range 0..255" + kNl);
    EXPECT_EQ(Run({"set", "jump"}),
              "Error: unknown joystick button 'jump' (up, down, left, right, fire, b5, b6, b7)" + kNl);
    EXPECT_EQ(Run({"tap", "fire", "0"}), "Error: frames=0 out of range 1..65535" + kNl);
    EXPECT_EQ(Run({"tap", "fire", "-4"}), "Error: frames=-4 out of range 1..65535" + kNl);
    EXPECT_EQ(Run({"tap", "fire", "70000"}), "Error: frames=70000 out of range 1..65535" + kNl);
    EXPECT_EQ(Run({"press"}), "Error: Missing button name. Usage: joystick press <buttons>" + kNl);
    EXPECT_EQ(Run({"set"}), "Error: Missing state. Usage: joystick set <state|buttons|none>" + kNl);
    EXPECT_NE(Run({"dance"}).find("Error: Unknown subcommand 'dance'"), std::string::npos);
    EXPECT_EQ(_context->pJoystick->State(), 0x00) << "rejected commands change nothing";
}

TEST_F(CliJoystick_Test, ReplayRefusesLiveInput)
{
    Run({"press", "left"});
    _context->ttdReplayActive = true;
    const std::string text = Run({"press", "up"});
    _context->ttdReplayActive = false;
    EXPECT_EQ(text.rfind("Error: TTD replay in progress", 0), 0u) << text;
    EXPECT_EQ(_context->pJoystick->State(), Joystick::kLeft);
}

TEST_F(CliJoystick_Test, NotFittedAcceptsWithAWarning)
{
    _emulator->GetFeatureManager()->setFeature(Features::kKempstonJoystick, false);
    const std::string text = Run({"press", "up"});
    EXPECT_NE(text.find("Warning: joystick not present: the guest reads 0x00 on the joystick port"),
              std::string::npos)
        << text;
    EXPECT_NE(Run({"status"}).find("Kempston Joystick [absent]"), std::string::npos);
}

TEST_F(CliJoystick_Test, NoDeviceIsAnError)
{
    Joystick* saved = _context->pJoystick;
    _context->pJoystick = nullptr;
    EXPECT_EQ(Run({"press", "up"}), "Error: Joystick device not available" + kNl);
    EXPECT_EQ(Run({"status"}), "Kempston Joystick [not available]" + kNl);
    _context->pJoystick = saved;
}

TEST_F(CliJoystick_Test, HelpListsEverySubcommand)
{
    const std::string text = Run({});
    for (const char* verb : {"press", "release", "set", "tap", "clear", "status", "list"})
        EXPECT_NE(text.find(std::string("  ") + verb), std::string::npos) << verb;
    EXPECT_EQ(Run({"help"}), text);
}
