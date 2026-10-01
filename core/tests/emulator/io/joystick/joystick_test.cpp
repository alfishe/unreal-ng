/// @file joystick_test.cpp
/// @brief Kempston joystick device (joystick TDD §7: JOY-1, JOY-2, JOY-7, JOY-12 and the fitting half of JOY-6).
///
/// The device alone: state byte, host key bindings, TTD blob. The decoders, the manager and the
/// journal have their own test files.

#include <fstream>

#include "_helpers/testpathhelper.h"
#include "base/featuremanager.h"
#include "emulator/config.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "emulator/io/keyboard/pckey.h"
#include "gtest/gtest.h"
#include "stdafx.h"

class Joystick_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _context = new EmulatorContext();
        _joystick = new Joystick(_context);
        _context->pJoystick = _joystick;
    }

    void TearDown() override
    {
        delete _joystick;
        delete _context;
    }

    EmulatorContext* _context = nullptr;
    Joystick* _joystick = nullptr;
};

/// JOY-2: power-on state is idle 0x00 (the Evo board, not the open-bus 1-padding of other emulators)
TEST_F(Joystick_Test, PowerOnStateIsZeroAndFitted)
{
    EXPECT_EQ(_joystick->State(), 0x00);
    EXPECT_EQ(_joystick->Read(), 0x00);
    EXPECT_TRUE(_joystick->IsPresent());
}

/// JOY-1: press / release / set / read, active high, D5..D7 kept as written
TEST_F(Joystick_Test, PressReleaseSetRead)
{
    _joystick->Press(Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(_joystick->Read(), 0x18);

    _joystick->Press(Joystick::kRight);
    EXPECT_EQ(_joystick->Read(), 0x19);

    _joystick->Release(Joystick::kUp);
    EXPECT_EQ(_joystick->Read(), 0x11);

    _joystick->SetState(0xE5);  // raw: D5..D7 are driven by a Sega pad or an extended interface
    EXPECT_EQ(_joystick->Read(), 0xE5);
    EXPECT_EQ(_joystick->State(), 0xE5);

    _joystick->Release(Joystick::kRight | Joystick::kDown);
    EXPECT_EQ(_joystick->Read(), 0xE0) << "release clears only the named bits";
}

TEST_F(Joystick_Test, ButtonMasksMatchTheAvrRegister)
{
    // joystick.h: D0 right, D1 left, D2 down, D3 up, D4 fire
    EXPECT_EQ(Joystick::kRight, 0x01);
    EXPECT_EQ(Joystick::kLeft, 0x02);
    EXPECT_EQ(Joystick::kDown, 0x04);
    EXPECT_EQ(Joystick::kUp, 0x08);
    EXPECT_EQ(Joystick::kFire, 0x10);
}

TEST_F(Joystick_Test, ResetReleasesButtonsAndKeepsFittingAndKeys)
{
    _joystick->SetState(0xFF);
    _joystick->SetPresent(false);
    _joystick->SetBindings("fire:z");

    _joystick->Reset();

    EXPECT_EQ(_joystick->State(), 0x00);
    EXPECT_FALSE(_joystick->IsPresent()) << "fitting is configuration, not state";
    EXPECT_EQ(_joystick->BindingsSpec(), "fire:z");
}

/// JOY-6 (device half): not fitted answers 0x00 whatever the state, and no key is bound to it
TEST_F(Joystick_Test, NotFittedReadsZeroAndIgnoresKeys)
{
    _joystick->SetState(0x1F);
    _joystick->SetPresent(false);
    EXPECT_EQ(_joystick->Read(), 0x00);
    EXPECT_EQ(_joystick->State(), 0x1F) << "the byte itself is untouched";

    _joystick->SetState(0);
    EXPECT_FALSE(_joystick->WantsKey(PcKey::Keypad8));
    EXPECT_FALSE(_joystick->OnPcKey(PcKey::Keypad8, true));
    EXPECT_EQ(_joystick->State(), 0x00);
}

/// JOY-7: the default bindings are the keypad cross with 0 as fire
TEST_F(Joystick_Test, DefaultBindings)
{
    EXPECT_EQ(_joystick->BindingsSpec(), "right:kp_6,left:kp_4,down:kp_2,up:kp_8,fire:kp_0");

    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad8, true));
    EXPECT_EQ(_joystick->Read(), Joystick::kUp);
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad6, true));
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad0, true));
    EXPECT_EQ(_joystick->Read(), Joystick::kUp | Joystick::kRight | Joystick::kFire);

    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad8, false));
    EXPECT_EQ(_joystick->Read(), Joystick::kRight | Joystick::kFire);

    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad4, true));
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad2, true));
    EXPECT_EQ(_joystick->Read(), 0x1F & ~Joystick::kUp);
}

TEST_F(Joystick_Test, UnboundAndUnknownKeysAreLeftAlone)
{
    _joystick->Press(Joystick::kLeft);

    EXPECT_FALSE(_joystick->WantsKey(PcKey::A));
    EXPECT_FALSE(_joystick->OnPcKey(PcKey::A, true)) << "a bound-nowhere key is not ours";
    EXPECT_FALSE(_joystick->OnPcKey(PcKey::Keypad5, true));
    EXPECT_FALSE(_joystick->OnPcKey(PcKey::None, true));
    EXPECT_EQ(_joystick->State(), Joystick::kLeft);
}

TEST_F(Joystick_Test, CustomBindingsReplaceTheDefaults)
{
    EXPECT_TRUE(_joystick->SetBindings("up:q, down:a ,left:o,right:p,fire:space,b5:kp8"));

    EXPECT_FALSE(_joystick->OnPcKey(PcKey::Keypad6, true)) << "the old keypad binding is gone";
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Q, true));
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Space, true));
    EXPECT_EQ(_joystick->Read(), Joystick::kUp | Joystick::kFire);

    // The short keypad spelling and pckey's own spelling name the same key; D5..D7 are bindable
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad8, true));
    EXPECT_EQ(_joystick->Read(), Joystick::kUp | Joystick::kFire | 0x20);
    EXPECT_TRUE(_joystick->SetBindings("b7:kp_9"));
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::Keypad9, true));
    EXPECT_NE(_joystick->Read() & 0x80, 0);
}

TEST_F(Joystick_Test, OneKeyMayDriveSeveralButtons)
{
    EXPECT_TRUE(_joystick->SetBindings("up:x,fire:x"));
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::X, true));
    EXPECT_EQ(_joystick->Read(), Joystick::kUp | Joystick::kFire);
    EXPECT_TRUE(_joystick->OnPcKey(PcKey::X, false));
    EXPECT_EQ(_joystick->Read(), 0x00);
}

TEST_F(Joystick_Test, EmptyBindingsDisableTheHostKeys)
{
    EXPECT_TRUE(_joystick->SetBindings(""));
    EXPECT_EQ(_joystick->BindingsSpec(), "");
    EXPECT_FALSE(_joystick->WantsKey(PcKey::Keypad8));
    EXPECT_FALSE(_joystick->OnPcKey(PcKey::Keypad8, true));
    EXPECT_EQ(_joystick->Read(), 0x00);
}

TEST_F(Joystick_Test, BadEntriesAreSkippedTheRestIsApplied)
{
    EXPECT_FALSE(_joystick->SetBindings("up:q,jump:w,fire:nosuchkey,left,down:a"));
    EXPECT_EQ(_joystick->BindingsSpec(), "down:a,up:q");
}

TEST_F(Joystick_Test, ReleaseBoundKeysDropsOnlyKeyBoundButtons)
{
    _joystick->SetBindings("up:q,fire:space");
    _joystick->SetState(Joystick::kUp | Joystick::kFire | Joystick::kLeft | 0x80);
    _joystick->ReleaseBoundKeys();
    EXPECT_EQ(_joystick->State(), Joystick::kLeft | 0x80);
}

TEST_F(Joystick_Test, ButtonNames)
{
    EXPECT_EQ(Joystick::MaskFromName("UP"), Joystick::kUp);
    EXPECT_EQ(Joystick::MaskFromName("fire"), Joystick::kFire);
    EXPECT_EQ(Joystick::MaskFromName("b7"), 0x80);
    EXPECT_EQ(Joystick::MaskFromName("fire2"), 0);
    EXPECT_EQ(Joystick::MaskFromName(""), 0);
    EXPECT_EQ(Joystick::NameOfBit(Joystick::kLeft), "left");
    EXPECT_EQ(Joystick::NameOfBit(0x40), "b6");
    EXPECT_EQ(Joystick::NameOfBit(0x03), "") << "not exactly one bit";
}

/// JOY-12: the blob is a version byte and the state byte; the state comes back, the hash follows it
TEST_F(Joystick_Test, TTDBlobRoundTripAndHash)
{
    ASSERT_EQ(_joystick->TTDStateSize(), 2u);
    EXPECT_EQ(_joystick->TTDPeripheralId(), ttd::PeripheralId::KempstonJoystick);
    EXPECT_EQ(_joystick->TTDDeviceName(), "KempstonJoystick");

    _joystick->SetState(0xA9);
    uint8_t blob[2] = {};
    _joystick->TTDSaveState(blob);
    EXPECT_EQ(blob[0], 1) << "version";
    EXPECT_EQ(blob[1], 0xA9);
    const uint64_t hash = _joystick->TTDHashState();

    _joystick->Reset();
    EXPECT_NE(_joystick->TTDHashState(), hash) << "the state takes part in divergence detection";

    _joystick->TTDLoadState(blob);
    EXPECT_EQ(_joystick->State(), 0xA9);
    EXPECT_EQ(_joystick->TTDHashState(), hash);
}

TEST_F(Joystick_Test, TTDBlobDoesNotCarryFittingOrBindings)
{
    _joystick->SetBindings("fire:z");
    _joystick->SetPresent(false);
    const uint8_t blob[2] = {1, 0x11};
    _joystick->TTDLoadState(blob);
    EXPECT_EQ(_joystick->State(), 0x11);
    EXPECT_FALSE(_joystick->IsPresent());
    EXPECT_EQ(_joystick->BindingsSpec(), "fire:z");
}

/// region <Config and feature>

class JoystickConfig_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pJoystick, nullptr);
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

    bool LoadInput(const std::string& lines)
    {
        const std::string path = TestPathHelper::GetUniqueTestScratchPath("joystick_config_test.ini");
        {
            std::ofstream file(path, std::ios::binary);
            file << "[INPUT]\n" << lines;
        }
        Config config(_context);
        const bool ok = config.LoadConfigFile(path);
        _context->pJoystick->ApplyConfiguration();
        return ok;
    }
};

TEST_F(JoystickConfig_Test, NoIniKeysMeansFittedWithTheKeypadDefaults)
{
    EXPECT_TRUE(_context->pJoystick->IsPresent());
    EXPECT_EQ(_context->pJoystick->BindingsSpec(), "right:kp_6,left:kp_4,down:kp_2,up:kp_8,fire:kp_0");
}

TEST_F(JoystickConfig_Test, JoystickNoneRemovesDeviceKempstonFitsIt)
{
    ASSERT_TRUE(LoadInput("Joystick=NONE\n"));
    EXPECT_EQ(_context->config.input.joystick, 0);
    EXPECT_FALSE(_context->pJoystick->IsPresent());

    ASSERT_TRUE(LoadInput("Joystick=KEMPSTON\n"));
    EXPECT_TRUE(_context->pJoystick->IsPresent());

    ASSERT_TRUE(LoadInput("Joystick=SOMETHING\n"));
    EXPECT_TRUE(_context->pJoystick->IsPresent()) << "an unknown value falls back to KEMPSTON";
}

TEST_F(JoystickConfig_Test, JoystickKeysOverrideAndEmptyDisables)
{
    ASSERT_TRUE(LoadInput("JoystickKeys=up:w,down:s,left:a,right:d,fire:space\n"));
    EXPECT_EQ(_context->pJoystick->BindingsSpec(), "right:d,left:a,down:s,up:w,fire:space");

    ASSERT_TRUE(LoadInput("JoystickKeys=\n"));
    EXPECT_EQ(_context->pJoystick->BindingsSpec(), "") << "present and empty: the host keys are off";

    ASSERT_TRUE(LoadInput("Mouse=KEMPSTON\n"));
    EXPECT_EQ(_context->pJoystick->BindingsSpec(), "right:kp_6,left:kp_4,down:kp_2,up:kp_8,fire:kp_0")
        << "absent key: the defaults again";
}

TEST_F(JoystickConfig_Test, FeatureFlagControlsPresence)
{
    FeatureManager* fm = _emulator->GetFeatureManager();
    ASSERT_NE(fm, nullptr);
    fm->setFeature(Features::kKempstonJoystick, false);
    EXPECT_FALSE(_context->pJoystick->IsPresent());
    fm->setFeature(Features::kKempstonJoystick, true);
    EXPECT_TRUE(_context->pJoystick->IsPresent());
}

TEST_F(JoystickConfig_Test, MachineResetKeepsHeldButtons)
{
    // The buttons mirror the physical stick: a Z80 reset does not release them (as the mouse counters)
    _context->pJoystick->SetState(0x11);
    _emulator->Reset();
    EXPECT_EQ(_context->pJoystick->State(), 0x11);
}

/// endregion </Config and feature>
