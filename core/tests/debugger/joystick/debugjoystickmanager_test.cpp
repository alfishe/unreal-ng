/// @file debugjoystickmanager_test.cpp
/// @brief Kempston joystick injection funnel (joystick TDD §4, §6; JOY-6 manager half, JOY-9, JOY-10, JOY-11).
///
/// A real ZX-Evo (ATM3) instance: it is a machine whose decoder answers #1F and that has a PS/2
/// controller, so host keys, the journal and the blob are all live. Creating the machine costs a
/// few tens of milliseconds, which is the price of testing the wiring rather than a bare context.

#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/joystick/debugjoystickmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/ttd/timetravelcontroller.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/joystick/joystick.h"
#include "gtest/gtest.h"
#include "stdafx.h"

using ttd::TTDInputKind;

class DebugJoystickManager_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    DebugJoystickManager* _manager = nullptr;
    Joystick* _joystick = nullptr;
    ttd::TimeTravelController* _ttd = nullptr;

    struct Sample
    {
        ttd::TTDTimePoint at;
        uint8_t state = 0;
    };

    void SetUp() override
    {
        Init("ATM3");
    }

    void Init(const char* model)
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator(model, LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _context = _emulator->GetContext();
        ASSERT_NE(_context->pDebugManager, nullptr);
        _manager = _context->pDebugManager->GetJoystickManager();
        ASSERT_NE(_manager, nullptr);
        _joystick = _context->pJoystick;
        ASSERT_NE(_joystick, nullptr);
        _ttd = _context->pTimeTravelController;
    }

    void TearDown() override
    {
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    bool EnableRecording()
    {
        if (!_ttd)
            return false;
        _emulator->GetFeatureManager()->setFeature(Features::kTimeTravel, true);
        return _ttd->StartRecording();
    }

    Sample Take() const
    {
        Z80* z80 = _context->pCore->GetZ80();
        Sample sample;
        sample.at = {_context->emulatorState.frame_counter, _context->emulatorState.TtdTInFrame(z80->t)};
        sample.state = _joystick->State();
        return sample;
    }

    std::vector<ttd::TTDInputEvent> JournalOfKind(TTDInputKind kind) const
    {
        std::vector<ttd::TTDInputEvent> result;
        for (const auto& ev : _ttd->GetInputJournal().Events())
            if (ev.kind == kind)
                result.push_back(ev);
        return result;
    }
};

/// region <Names>

TEST_F(DebugJoystickManager_Test, ResolveButtonNames_SingleAndLists)
{
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("up"), Joystick::kUp);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("FIRE"), Joystick::kFire);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("up+fire"), Joystick::kUp | Joystick::kFire);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("left,b5"), Joystick::kLeft | 0x20);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames(""), 0);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("up+"), 0);
    EXPECT_EQ(DebugJoystickManager::ResolveButtonNames("up,jump"), 0) << "one bad name rejects the list";
    EXPECT_EQ(DebugJoystickManager::GetAllButtonNames().size(), 8u);
}

/// endregion </Names>

/// region <Immediate operations (JOY-9)>

TEST_F(DebugJoystickManager_Test, PressRelease_ApplyBeforeTheCallReturns)
{
    JoystickInjectResult result = _manager->Press("up");
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(_joystick->State(), Joystick::kUp) << "applied before the call returns - no wait";
    EXPECT_TRUE(result.warning.empty()) << "the ZX-Evo decodes #1F: " << result.warning;

    ASSERT_TRUE(_manager->Press("fire+left").ok());
    EXPECT_EQ(_joystick->State(), Joystick::kUp | Joystick::kFire | Joystick::kLeft);

    ASSERT_TRUE(_manager->Release("up").ok());
    EXPECT_EQ(_joystick->State(), Joystick::kFire | Joystick::kLeft);
}

TEST_F(DebugJoystickManager_Test, PressMaskReleaseMaskSetState)
{
    ASSERT_TRUE(_manager->PressMask(0x81).ok());
    EXPECT_EQ(_joystick->State(), 0x81);
    ASSERT_TRUE(_manager->ReleaseMask(0x01).ok());
    EXPECT_EQ(_joystick->State(), 0x80);
    ASSERT_TRUE(_manager->SetState(0xE3).ok());
    EXPECT_EQ(_joystick->State(), 0xE3) << "the whole byte, D5..D7 included";
}

TEST_F(DebugJoystickManager_Test, Validation_UnknownNamesAndEmptyMasks)
{
    JoystickInjectResult result = _manager->Press("jump");
    EXPECT_EQ(result.status, JoystickInjectStatus::InvalidArgument);
    EXPECT_NE(result.message.find("unknown joystick button 'jump'"), std::string::npos) << result.message;

    EXPECT_EQ(_manager->Press("").status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Release("up,nope").status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->PressMask(0).status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->ReleaseMask(0).status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Tap("jump").status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Tap("up", 0).status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Tap("up", DebugJoystickManager::MAX_TAP_FRAMES + 1).status,
              JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_joystick->State(), 0x00) << "rejected calls leave the state untouched";
}

TEST_F(DebugJoystickManager_Test, ReleaseAll_ClearsEverythingAndCancelsATap)
{
    ASSERT_TRUE(_manager->SetState(0xFF).ok());
    ASSERT_TRUE(_manager->Tap("up", 5).ok());
    EXPECT_TRUE(_manager->IsTapPending());

    ASSERT_TRUE(_manager->ReleaseAll().ok());
    EXPECT_EQ(_joystick->State(), 0x00);
    EXPECT_FALSE(_manager->IsTapPending());
}

TEST_F(DebugJoystickManager_Test, SetStateChecked_RangeIsValidatedForEverySurface)
{
    // The automation surfaces parse their own number types and hand the raw integer over,
    // so the range message is the same on every surface (JOY-13)
    ASSERT_TRUE(_manager->SetStateChecked(0x15).ok());
    EXPECT_EQ(_joystick->State(), 0x15);

    JoystickInjectResult high = _manager->SetStateChecked(256);
    EXPECT_EQ(high.status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(high.message, "state=256 out of range 0..255");
    JoystickInjectResult low = _manager->SetStateChecked(-1);
    EXPECT_EQ(low.status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(low.message, "state=-1 out of range 0..255");
    EXPECT_EQ(_joystick->State(), 0x15) << "a rejected value leaves the state untouched";
}

TEST_F(DebugJoystickManager_Test, TapChecked_RangeIsValidatedForEverySurface)
{
    ASSERT_TRUE(_manager->TapChecked("fire", 3).ok());
    EXPECT_TRUE(_manager->IsTapPending());
    ASSERT_TRUE(_manager->ReleaseAll().ok());

    JoystickInjectResult negative = _manager->TapChecked("fire", -4);
    EXPECT_EQ(negative.status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(negative.message, "frames=-4 out of range 1..65535");
    JoystickInjectResult huge = _manager->TapChecked("fire", 5000000000LL);
    EXPECT_EQ(huge.status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(huge.message, "frames=5000000000 out of range 1..65535") << "no wrap to a small unsigned value";
    EXPECT_EQ(_manager->TapChecked("jump", 2).status, JoystickInjectStatus::InvalidArgument);
    EXPECT_EQ(_joystick->State(), 0x00);
}

/// endregion </Immediate operations (JOY-9)>

/// region <Timed tap (JOY-9)>

TEST_F(DebugJoystickManager_Test, Tap_ReleasesAfterNFramesInOnFrame)
{
    ASSERT_TRUE(_manager->Press("left").ok());
    ASSERT_TRUE(_manager->Tap("fire", 3).ok());
    EXPECT_EQ(_joystick->State(), Joystick::kLeft | Joystick::kFire);

    JoystickStateSnapshot state = _manager->GetState();
    EXPECT_EQ(state.pendingTapMask, Joystick::kFire);
    EXPECT_EQ(state.pendingTapFramesLeft, 3);

    _manager->OnFrame();
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), Joystick::kLeft | Joystick::kFire) << "still held after 2 of 3 frames";
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), Joystick::kLeft) << "only the tapped button is released";
    EXPECT_FALSE(_manager->IsTapPending());
}

TEST_F(DebugJoystickManager_Test, Tap_DefaultIsTwoFrames)
{
    ASSERT_TRUE(_manager->Tap("fire").ok());
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), Joystick::kFire);
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), 0x00);
}

TEST_F(DebugJoystickManager_Test, Tap_NewTapReplacesThePendingOne)
{
    ASSERT_TRUE(_manager->Tap("up", 5).ok());
    ASSERT_TRUE(_manager->Tap("fire", 1).ok());
    EXPECT_EQ(_joystick->State(), Joystick::kFire) << "the first tap's button is released at once";
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), 0x00);
}

TEST_F(DebugJoystickManager_Test, Tap_ExplicitPressOfTheSameButtonWins)
{
    ASSERT_TRUE(_manager->Tap("fire", 1).ok());
    ASSERT_TRUE(_manager->Press("fire").ok());
    _manager->OnFrame();
    EXPECT_EQ(_joystick->State(), Joystick::kFire) << "the explicit press cancelled the pending release";
    EXPECT_FALSE(_manager->IsTapPending());
}

TEST_F(DebugJoystickManager_Test, AbortTap_ReleasesNow)
{
    ASSERT_TRUE(_manager->Tap("up+fire", 9).ok());
    _manager->AbortTap();
    EXPECT_EQ(_joystick->State(), 0x00);
    EXPECT_FALSE(_manager->IsTapPending());
}

/// endregion </Timed tap (JOY-9)>

/// region <Snapshot and fitting (JOY-6)>

TEST_F(DebugJoystickManager_Test, GetState_ReportsDeviceKeysAndPort)
{
    ASSERT_TRUE(_manager->Press("up+fire").ok());
    JoystickStateSnapshot state = _manager->GetState();
    EXPECT_TRUE(state.available);
    EXPECT_TRUE(state.present);
    EXPECT_TRUE(state.wired);
    EXPECT_EQ(state.state, 0x18);
    EXPECT_EQ(state.portValue, 0x18);
    ASSERT_EQ(state.buttons.size(), 2u);
    EXPECT_EQ(state.buttons[0], "up");
    EXPECT_EQ(state.buttons[1], "fire");
    EXPECT_EQ(state.keys, "right:kp_6,left:kp_4,down:kp_2,up:kp_8,fire:kp_0");
}

/// JOY-6: not fitted - the snapshot says so, the port reads 0x00, the manager still accepts the input but warns
TEST_F(DebugJoystickManager_Test, NotFitted_ReportsAbsentAndWarns)
{
    _emulator->GetFeatureManager()->setFeature(Features::kKempstonJoystick, false);

    JoystickInjectResult result = _manager->Press("up");
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_NE(result.warning.find("not present"), std::string::npos) << result.warning;

    JoystickStateSnapshot state = _manager->GetState();
    EXPECT_TRUE(state.available);
    EXPECT_FALSE(state.present);
    EXPECT_EQ(state.state, Joystick::kUp);
    EXPECT_EQ(state.portValue, 0x00);
}

/// A machine whose decoder has no #1F joystick: accepted, with a warning that the guest cannot see it
TEST(DebugJoystickManagerUnwired_Test, MachineWithoutTheArmWarns)
{
    // The 48K decoder has no #1F arm (the Pentagon family and Profi have one since 2026-10-01)
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("48K", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    DebugJoystickManager* manager = emulator->GetContext()->pDebugManager->GetJoystickManager();
    ASSERT_NE(manager, nullptr);

    JoystickInjectResult result = manager->Press("fire");
    EXPECT_TRUE(result.ok());
    EXPECT_NE(result.warning.find("does not decode"), std::string::npos) << result.warning;
    EXPECT_FALSE(manager->GetState().wired);

    EmulatorTestHelper::CleanupEmulator(emulator);
}

/// endregion </Snapshot and fitting (JOY-6)>

/// region <Replay guard and journal (JOY-10, JOY-11)>

TEST_F(DebugJoystickManager_Test, ReplayActive_RefusesAndLeavesStateUntouched)
{
    ASSERT_TRUE(_manager->Press("left").ok());
    _context->ttdReplayActive = true;
    EXPECT_EQ(_manager->Press("up").status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->Release("left").status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->PressMask(0x10).status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->ReleaseMask(0x02).status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->SetState(0xFF).status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->ReleaseAll().status, JoystickInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->Tap("fire").status, JoystickInjectStatus::ReplayActive);
    _context->ttdReplayActive = false;

    EXPECT_EQ(_joystick->State(), Joystick::kLeft) << "live input must not mutate the device during replay";
    EXPECT_FALSE(_manager->IsTapPending());
}

TEST_F(DebugJoystickManager_Test, ReplayActive_TimedReleaseDoesNotTouchTheDevice)
{
    ASSERT_TRUE(_manager->Tap("fire", 1).ok());
    _context->ttdReplayActive = true;
    _manager->OnFrame();
    _context->ttdReplayActive = false;
    EXPECT_EQ(_joystick->State(), Joystick::kFire) << "the recorded release drives the machine, not the timer";
}

TEST_F(DebugJoystickManager_Test, NoDevice_ReportsNoDevice)
{
    Joystick* saved = _context->pJoystick;
    _context->pJoystick = nullptr;
    EXPECT_EQ(_manager->Press("up").status, JoystickInjectStatus::NoDevice);
    EXPECT_FALSE(_manager->GetState().available);
    _context->pJoystick = saved;
}

TEST_F(DebugJoystickManager_Test, Recording_JournalsEveryMutationWithTheWholeState)
{
    ASSERT_TRUE(EnableRecording());

    ASSERT_TRUE(_manager->Press("up").ok());
    ASSERT_TRUE(_manager->Press("fire").ok());
    ASSERT_TRUE(_manager->Release("up").ok());
    ASSERT_TRUE(_manager->SetState(0xA0).ok());
    ASSERT_TRUE(_manager->Tap("right", 1).ok());
    _manager->OnFrame();  // the timed release goes through the journaled path

    auto events = JournalOfKind(TTDInputKind::Joystick);
    ASSERT_EQ(events.size(), 6u);
    const uint8_t expected[] = {0x08, 0x18, 0x10, 0xA0, 0xA1, 0xA0};
    for (size_t i = 0; i < events.size(); i++)
        EXPECT_EQ(events[i].buttonMask, expected[i]) << "event " << i;
}

/// JOY-11: record a session mixing host-key input (journaled as PcKey) and automation input (journaled as
/// Joystick), seek to every sampled point back and forth: the same state at each one. No key event is
/// journaled twice
TEST_F(DebugJoystickManager_Test, RecordSeekReplay_KeyDrivenAndAutomationInput)
{
    ASSERT_TRUE(EnableRecording());
    DebugKeyboardManager* keys = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(keys, nullptr);

    std::vector<Sample> samples;
    // Each step injects after a few T-states, so that no event shares the time point of the previous sample
    auto step = [&]() {
        _emulator->RunNFrames(1);
        samples.push_back(Take());
        _emulator->RunTStates(100);
    };

    step();                                     // idle
    keys->PressKey("kp_4");                     // host key: left
    step();
    ASSERT_TRUE(_manager->Press("fire").ok());  // automation: left + fire
    step();
    ASSERT_TRUE(_manager->Tap("up", 1).ok());   // left + fire + up, sampled inside the frame ...
    _emulator->RunTStates(100);
    samples.push_back(Take());
    step();                                     // ... the frame pump (MainLoop::CompleteFrame) releases the tap
    keys->ReleaseKey("kp_4");                   // host key up: fire
    step();
    ASSERT_TRUE(_manager->SetState(0xE0).ok()); // raw write, D5..D7
    step();
    step();                                     // nothing changes
    _ttd->StopRecording();

    const uint8_t expected[] = {0x00, Joystick::kLeft, Joystick::kLeft | Joystick::kFire,
                                Joystick::kLeft | Joystick::kFire | Joystick::kUp,
                                Joystick::kLeft | Joystick::kFire, Joystick::kFire, 0xE0, 0xE0};
    ASSERT_EQ(samples.size(), sizeof(expected));
    for (size_t i = 0; i < samples.size(); i++)
        EXPECT_EQ(samples[i].state, expected[i]) << "live state at step " << i;

    // The key is journaled once, as the physical key; the joystick event is the automation path only
    size_t pcEvents = 0;
    for (const auto& ev : _ttd->GetInputJournal().Events())
    {
        if (ev.kind == TTDInputKind::PcKey)
            pcEvents++;
    }
    EXPECT_EQ(pcEvents, 2u) << "kp_4 down and up, once each";
    EXPECT_EQ(JournalOfKind(TTDInputKind::Joystick).size(), 4u) << "press fire, tap press, tap release, set";

    for (size_t i = samples.size(); i-- > 0;)
    {
        ASSERT_TRUE(_ttd->SeekTo(samples[i].at)) << "backward to " << i;
        EXPECT_EQ(_joystick->State(), samples[i].state) << "after seeking back to step " << i;
    }
    for (size_t i = 0; i < samples.size(); i++)
    {
        ASSERT_TRUE(_ttd->SeekTo(samples[i].at)) << "forward to " << i;
        EXPECT_EQ(_joystick->State(), samples[i].state) << "after seeking forward to step " << i;
    }
}

/// endregion </Replay guard and journal (JOY-10, JOY-11)>
