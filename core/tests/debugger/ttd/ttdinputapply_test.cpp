/// @file ttdinputapply_test.cpp
/// @brief ApplyInputEvent / InputDevicesOf: every input kind reaches its device,
/// and an absent device leaves the event unapplied.
///
/// The live and replay paths through TimeTravelManager (SubmitLiveInput,
/// ServiceInput playback) are covered where they are driven:
/// ttdinputjournal_test.cpp, ttdinputplayback_test.cpp, debugmousemanager_test.cpp.

#include <gtest/gtest.h>

#include <cstdint>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/soundcardscope.h"
#include "common/modulelogger.h"
#include "debugger/ttd/ttdinputapply.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/sound/chips/gs/generalsoundcard.h"
#include "emulator/sound/soundmanager.h"

using ttd::ApplyInputEvent;
using ttd::InputDevicesOf;
using ttd::TTDInputDevices;
using ttd::TTDInputEvent;
using ttd::TTDInputKind;

namespace
{
TTDInputEvent Event(TTDInputKind kind)
{
    TTDInputEvent ev;
    ev.kind = kind;
    return ev;
}

TTDInputEvent Key(ZXKeysEnum key, bool pressed)
{
    TTDInputEvent ev = Event(TTDInputKind::Key);
    ev.key = static_cast<uint8_t>(key);
    ev.pressed = pressed;
    return ev;
}
}  // namespace

// ===========================================================================
// Keyboard and Kempston Mouse (standard machine)
// ===========================================================================

class TTDInputApply_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    Keyboard* _keyboard = nullptr;
    Mouse* _mouse = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        _keyboard = _context->pKeyboard;
        _mouse = _context->pMouse;
        ASSERT_NE(_keyboard, nullptr);
        ASSERT_NE(_mouse, nullptr);
        _keyboard->Reset();
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

    /// A key reads low on its half-row port while pressed
    bool KeyDown(uint16_t port, int bit) { return (_keyboard->HandlePortIn(port) & (1 << bit)) == 0; }

    TTDInputDevices KeyboardOnly() const
    {
        TTDInputDevices devices;
        devices.keyboard = _keyboard;
        return devices;
    }

    TTDInputDevices MouseOnly() const
    {
        TTDInputDevices devices;
        devices.mouse = _mouse;
        return devices;
    }
};

TEST_F(TTDInputApply_Test, InputDevicesOfTheContext)
{
    const TTDInputDevices devices = InputDevicesOf(_context);
    EXPECT_EQ(devices.keyboard, _context->pKeyboard);
    EXPECT_EQ(devices.mouse, _context->pMouse);
    EXPECT_EQ(devices.generalSound, _context->pSoundManager ? _context->pSoundManager->getGeneralSound() : nullptr);

    const TTDInputDevices none = InputDevicesOf(nullptr);
    EXPECT_EQ(none.keyboard, nullptr);
    EXPECT_EQ(none.mouse, nullptr);
    EXPECT_EQ(none.generalSound, nullptr);
}

TEST_F(TTDInputApply_Test, KeyPressAndReleaseDriveTheMatrix)
{
    // SPACE: port #7FFE, bit 0
    ASSERT_TRUE(ApplyInputEvent(Key(ZXKEY_SPACE, true), KeyboardOnly()));
    EXPECT_TRUE(KeyDown(0x7FFE, 0));

    ASSERT_TRUE(ApplyInputEvent(Key(ZXKEY_SPACE, false), KeyboardOnly()));
    EXPECT_FALSE(KeyDown(0x7FFE, 0));
}

TEST_F(TTDInputApply_Test, SimultaneousKeysAllApply)
{
    // A and S: port #FDFE, bits 0 and 1 (two keys held on the same instant)
    ASSERT_TRUE(ApplyInputEvent(Key(ZXKEY_A, true), KeyboardOnly()));
    ASSERT_TRUE(ApplyInputEvent(Key(ZXKEY_S, true), KeyboardOnly()));
    EXPECT_TRUE(KeyDown(0xFDFE, 0));
    EXPECT_TRUE(KeyDown(0xFDFE, 1));
}

TEST_F(TTDInputApply_Test, KeyboardResetReleasesEveryKey)
{
    // Pressed from outside the input path too
    _keyboard->PressKey(ZXKEY_S);
    ASSERT_TRUE(ApplyInputEvent(Key(ZXKEY_A, true), KeyboardOnly()));

    ASSERT_TRUE(ApplyInputEvent(Event(TTDInputKind::KeyboardReset), KeyboardOnly()));
    EXPECT_FALSE(KeyDown(0xFDFE, 0));
    EXPECT_FALSE(KeyDown(0xFDFE, 1));
}

TEST_F(TTDInputApply_Test, MouseKindsDriveTheMouse)
{
    TTDInputEvent move = Event(TTDInputKind::MouseMove);
    move.dx = -5;
    move.dy = 9;
    TTDInputEvent buttons = Event(TTDInputKind::MouseButtons);
    buttons.buttonMask = 0xFD;
    TTDInputEvent wheel = Event(TTDInputKind::MouseWheel);
    wheel.wheelSteps = -1;

    ASSERT_TRUE(ApplyInputEvent(move, MouseOnly()));
    ASSERT_TRUE(ApplyInputEvent(buttons, MouseOnly()));
    ASSERT_TRUE(ApplyInputEvent(wheel, MouseOnly()));
    EXPECT_EQ(_mouse->GetX(), Mouse::RESET_X - 5);
    EXPECT_EQ(_mouse->GetY(), Mouse::RESET_Y + 9);
    EXPECT_EQ(_mouse->GetButtons(), 0xFD);
    EXPECT_EQ(_mouse->GetWheel(), 15);

    TTDInputEvent counters = Event(TTDInputKind::MouseCounters);
    counters.dx = 1;
    counters.dy = 2;
    ASSERT_TRUE(ApplyInputEvent(counters, MouseOnly()));
    EXPECT_EQ(_mouse->GetX(), 1);
    EXPECT_EQ(_mouse->GetY(), 2);
}

/// Every kind without its device is reported as not applied and changes nothing
TEST_F(TTDInputApply_Test, AbsentDeviceLeavesTheEventUnapplied)
{
    const TTDInputDevices none;
    for (auto kind : {TTDInputKind::Key, TTDInputKind::KeyboardReset, TTDInputKind::MouseMove,
                      TTDInputKind::MouseButtons, TTDInputKind::MouseWheel, TTDInputKind::MouseCounters,
                      TTDInputKind::GSCommand, TTDInputKind::GSData, TTDInputKind::GSNmi,
                      TTDInputKind::GSResetCard, TTDInputKind::GSReset})
    {
        EXPECT_FALSE(ApplyInputEvent(Event(kind), none)) << "kind " << static_cast<int>(kind);
    }

    // Only the event's own device counts: a mouse event with just a keyboard
    TTDInputEvent move = Event(TTDInputKind::MouseMove);
    move.dx = 3;
    EXPECT_FALSE(ApplyInputEvent(move, KeyboardOnly()));
    EXPECT_EQ(_mouse->GetX(), Mouse::RESET_X);
    EXPECT_FALSE(ApplyInputEvent(Key(ZXKEY_SPACE, true), MouseOnly()));
    EXPECT_FALSE(KeyDown(0x7FFE, 0));
}

// ===========================================================================
// General Sound host stimuli (a machine that fits the card)
// ===========================================================================

class TTDInputApplyGS_Test : public ::testing::Test
{
protected:
    // Keep the General Sound card the ATM710 config fits (GSType=Z80)
    SoundCardScope _soundCards{TestSound::GeneralSound};

    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("ATM710", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(Card(), nullptr) << "ATM710 must fit a GS card by default";
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EmulatorTestHelper::CleanupEmulator(_emulator);
            _emulator = nullptr;
        }
    }

    GeneralSoundCard* Card() const { return _context->pSoundManager->getGeneralSound(); }

    static TTDInputEvent GS(TTDInputKind kind, uint8_t value = 0)
    {
        TTDInputEvent ev = Event(kind);
        ev.value = value;
        return ev;
    }
};

/// Each kind reaches the card through the host-port contract
TEST_F(TTDInputApplyGS_Test, EachKindDrivesTheCard)
{
    GeneralSoundCard* gs = Card();
    const TTDInputDevices devices = InputDevicesOf(_context);
    ASSERT_EQ(devices.generalSound, gs);
    const uint64_t nmisBefore = gs->getActivityCounters().nmisAccepted;

    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSCommand, 0x5A), devices));
    EXPECT_EQ(gs->getCommandFromHost(), 0x5A);
    EXPECT_NE(gs->getStatusRaw() & 0x01, 0) << "OUT #BB sets the command flag";

    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSData, 0xA5), devices));
    EXPECT_EQ(gs->getDataFromHost(), 0xA5);
    EXPECT_NE(gs->getStatusRaw() & 0x80, 0) << "OUT #B3 sets the data flag";

    // #33 bit 7: the card restarts, the host mailbox (external flip-flops) survives
    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSResetCard), devices));
    EXPECT_EQ(gs->getCPUReg(GSCpuRegister::PC), 0);
    EXPECT_NE(gs->getStatusRaw() & 0x01, 0) << "reset_card must keep the pending command";

    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSNmi), devices));
    _emulator->RunNFrames(1);  // the NMI is taken by the card's next instruction
    EXPECT_GT(gs->getActivityCounters().nmisAccepted, nmisBefore);

    // Power-on reset clears the mailbox too
    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSCommand, 0x11), devices));
    ASSERT_TRUE(ApplyInputEvent(GS(TTDInputKind::GSReset), devices));
    EXPECT_EQ(gs->getStatusRaw() & 0x81, 0) << "full reset must clear the command and data flags";
}
