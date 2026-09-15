/// @file debugmousemanager_test.cpp
/// @brief Kempston Mouse injection funnel (automation-interfaces §5.1-§5.4) and the input
///        journal / replay guard fixes it shares with the keyboard.
///
/// Real Emulator instance (DebugManager, Mouse, TimeTravelManager wired as in the app),
/// no running CPU unless a test drives frames explicitly.

#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "_helpers/testwaithelper.h"
#include "base/featuremanager.h"
#include "debugger/debugmanager.h"
#include "debugger/keyboard/debugkeyboardmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "debugger/ttd/timetravelmanager.h"
#include "debugger/ttd/ttdinputjournal.h"
#include "emulator/cpu/z80.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/io/keyboard/keyboard.h"
#include "emulator/io/mouse/mouse.h"
#include "emulator/memory/memory.h"

using ttd::TTDInputKind;

class DebugMouseManager_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;
    EmulatorContext* _context = nullptr;
    DebugMouseManager* _manager = nullptr;
    Mouse* _mouse = nullptr;
    ttd::TimeTravelManager* _ttd = nullptr;

    void SetUp() override
    {
        _emulator = new Emulator(LoggerLevel::LogError);
        ASSERT_TRUE(_emulator->Init());
        _context = _emulator->GetContext();
        ASSERT_NE(_context, nullptr);
        ASSERT_NE(_context->pDebugManager, nullptr);
        _manager = _context->pDebugManager->GetMouseManager();
        ASSERT_NE(_manager, nullptr);
        _mouse = _context->pMouse;
        ASSERT_NE(_mouse, nullptr);
        _ttd = _context->pTimeTravelManager;
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

    bool EnableRecording()
    {
        FeatureManager* fm = _emulator->GetFeatureManager();
        if (!_ttd || !fm)
            return false;
        fm->setFeature(Features::kDebugMode, true);
        fm->setFeature(Features::kTimeTravel, true);
        _context->pMemory->UpdateFeatureCache();
        return _ttd->StartRecording();
    }

    /// Key down in the matrix: row port (#FBFE Q..T, #FDFE A..G) and bit (0 = first key of the row)
    bool KeyDown(uint16_t rowPort, int bit) const
    {
        return (_context->pKeyboard->HandlePortIn(rowPort) & (1 << bit)) == 0;
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

TEST_F(DebugMouseManager_Test, ResolveButtonName_AllNamesAndAliases)
{
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("left"), MouseButton::Left);
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("L"), MouseButton::Left);
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("Right"), MouseButton::Right);
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("r"), MouseButton::Right);
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("MIDDLE"), MouseButton::Middle);
    EXPECT_EQ(DebugMouseManager::ResolveButtonName("m"), MouseButton::Middle);
    EXPECT_FALSE(DebugMouseManager::ResolveButtonName("").has_value());
    EXPECT_FALSE(DebugMouseManager::ResolveButtonName("foo").has_value());
    EXPECT_FALSE(DebugMouseManager::ResolveButtonName("lef").has_value());
}

/// endregion </Names>

/// region <Immediate operations>

TEST_F(DebugMouseManager_Test, Move_UpdatesCountersSynchronously)
{
    MouseInjectResult result = _manager->Move(10, -5);
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(_mouse->GetX(), 41) << "applied before the call returns - no wait";
    EXPECT_EQ(_mouse->GetY(), 80);
}

TEST_F(DebugMouseManager_Test, Move_WrapsAt8Bits)
{
    ASSERT_TRUE(_manager->Move(127, 0).ok());
    ASSERT_TRUE(_manager->Move(127, 0).ok());
    EXPECT_EQ(_mouse->GetX(), (31 + 254) % 256);
}

TEST_F(DebugMouseManager_Test, Move_RejectsOutOfRangeAndZero)
{
    MouseInjectResult result = _manager->Move(128, 0);
    EXPECT_EQ(result.status, MouseInjectStatus::InvalidArgument);
    EXPECT_NE(result.message.find("dx=128 out of range -127..127"), std::string::npos) << result.message;

    EXPECT_EQ(_manager->Move(0, -128).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Move(0, 0).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_mouse->GetX(), Mouse::RESET_X) << "rejected calls leave the counters untouched";
    EXPECT_EQ(_mouse->GetY(), Mouse::RESET_Y);
}

TEST_F(DebugMouseManager_Test, PressRelease_ActiveLowMask)
{
    ASSERT_TRUE(_manager->PressButton(MouseButton::Left).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFE);
    ASSERT_TRUE(_manager->PressButton(MouseButton::Middle).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFA);
    ASSERT_TRUE(_manager->ReleaseButton(MouseButton::Left).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFB);
}

TEST_F(DebugMouseManager_Test, SetPressedButtons_ExactSet)
{
    ASSERT_TRUE(_manager->PressButton(MouseButton::Left).ok());
    ASSERT_TRUE(_manager->SetPressedButtons(static_cast<uint8_t>(MouseButton::Right)).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFD);
    ASSERT_TRUE(_manager->ReleaseAllButtons().ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFF);
    EXPECT_EQ(_manager->SetPressedButtons(0x08).status, MouseInjectStatus::InvalidArgument);
}

TEST_F(DebugMouseManager_Test, Wheel_NibbleWrapAndRange)
{
    ASSERT_TRUE(_manager->Wheel(2).ok());
    EXPECT_EQ(_mouse->GetWheel(), 2);
    ASSERT_TRUE(_manager->Wheel(-3).ok());
    EXPECT_EQ(_mouse->GetWheel(), 15);
    EXPECT_EQ(_manager->Wheel(8).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Wheel(0).status, MouseInjectStatus::InvalidArgument);
}

TEST_F(DebugMouseManager_Test, Wheel_WarnsWhenNoWheelFitted)
{
    _mouse->SetWheelEnabled(false);
    MouseInjectResult result = _manager->Wheel(1);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.warning.empty()) << "the guest cannot see the wheel counter";

    _mouse->SetWheelEnabled(true);
    EXPECT_TRUE(_manager->Wheel(1).warning.empty());
}

TEST_F(DebugMouseManager_Test, SetCounters_WritesRawAndValidates)
{
    ASSERT_TRUE(_manager->SetCounters(40, 40).ok());
    EXPECT_EQ(_mouse->GetX(), 40);
    EXPECT_EQ(_mouse->GetY(), 40);
    EXPECT_EQ(_manager->SetCounters(256, 0).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->SetCounters(0, -1).status, MouseInjectStatus::InvalidArgument);
}

TEST_F(DebugMouseManager_Test, Absent_AcceptsAndWarns)
{
    _mouse->SetPresent(false);
    MouseInjectResult result = _manager->Move(1, 0);
    ASSERT_TRUE(result.ok());
    EXPECT_FALSE(result.warning.empty());
    MouseStateSnapshot state = _manager->GetState();
    EXPECT_FALSE(state.present);
    EXPECT_EQ(state.x, 32) << "counters still change";
    EXPECT_EQ(state.portX, 0xFF) << "guest reads nothing from an absent device";
}

TEST_F(DebugMouseManager_Test, GetState_PortBytesMatchReadRegister)
{
    _mouse->SetWheelEnabled(true);
    ASSERT_TRUE(_manager->PressButton(MouseButton::Left).ok());
    ASSERT_TRUE(_manager->Wheel(2).ok());
    MouseStateSnapshot state = _manager->GetState();
    EXPECT_TRUE(state.available);
    EXPECT_EQ(state.portButtons, 0x2E);
    EXPECT_EQ(state.portX, 31);
    EXPECT_EQ(state.portY, 85);
    EXPECT_TRUE(state.IsPressed(MouseButton::Left));
    EXPECT_FALSE(state.IsPressed(MouseButton::Right));

    _mouse->SetWheelEnabled(false);
    EXPECT_EQ(_manager->GetState().portButtons, 0xFE) << "no wheel: D7-D3 read 1";
}

/// endregion </Immediate operations>

/// region <Click>

TEST_F(DebugMouseManager_Test, Click_ReleasesAfterNFrames)
{
    ASSERT_TRUE(_manager->Click(MouseButton::Left, 2).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFE);
    EXPECT_TRUE(_manager->IsClickPending());

    _manager->OnFrame();
    EXPECT_EQ(_mouse->GetButtons(), 0xFE) << "held for the 1st frame";
    _manager->OnFrame();
    EXPECT_EQ(_mouse->GetButtons(), 0xFF) << "released at the end of the 2nd frame";
    EXPECT_FALSE(_manager->IsClickPending());
}

TEST_F(DebugMouseManager_Test, Click_HeldExactlyNFramesUnderRunNFrames)
{
    ASSERT_TRUE(_manager->Click(MouseButton::Right, 2).ok());
    _emulator->RunNFrames(1);
    EXPECT_EQ(_mouse->GetButtons() & 0x02, 0) << "MainLoop::OnFrameEnd pumps the release - still held after frame 1";
    _emulator->RunNFrames(1);
    EXPECT_EQ(_mouse->GetButtons() & 0x02, 0x02) << "released after frame 2";
}

TEST_F(DebugMouseManager_Test, Click_FrameRange)
{
    EXPECT_EQ(_manager->Click(MouseButton::Left, 0).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_manager->Click(MouseButton::Left, 70000).status, MouseInjectStatus::InvalidArgument);
    EXPECT_EQ(_mouse->GetButtons(), 0xFF);
}

TEST_F(DebugMouseManager_Test, Click_ExplicitReleaseCancelsPending)
{
    ASSERT_TRUE(_manager->Click(MouseButton::Left, 5).ok());
    ASSERT_TRUE(_manager->ReleaseButton(MouseButton::Left).ok());
    ASSERT_TRUE(_manager->PressButton(MouseButton::Left).ok());
    for (int i = 0; i < 5; i++)
        _manager->OnFrame();
    EXPECT_EQ(_mouse->GetButtons(), 0xFE) << "the explicit press wins over the cancelled click release";
}

TEST_F(DebugMouseManager_Test, Click_NewClickReplacesPending)
{
    ASSERT_TRUE(_manager->Click(MouseButton::Left, 5).ok());
    ASSERT_TRUE(_manager->Click(MouseButton::Right, 1).ok());
    EXPECT_EQ(_mouse->GetButtons(), 0xFD) << "left released, right pressed";
    _manager->OnFrame();
    EXPECT_EQ(_mouse->GetButtons(), 0xFF);
}

/// endregion </Click>

/// region <Replay guard and journal>

TEST_F(DebugMouseManager_Test, ReplayActive_RefusesAndLeavesStateUntouched)
{
    _context->ttdReplayActive = true;
    EXPECT_EQ(_manager->Move(1, 1).status, MouseInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->PressButton(MouseButton::Left).status, MouseInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->Click(MouseButton::Left).status, MouseInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->Wheel(1).status, MouseInjectStatus::ReplayActive);
    EXPECT_EQ(_manager->SetCounters(1, 1).status, MouseInjectStatus::ReplayActive);
    _manager->ApplyHostMove(5, 5);
    _manager->ApplyHostButtons(0x00);
    _context->ttdReplayActive = false;

    EXPECT_EQ(_mouse->GetX(), Mouse::RESET_X);
    EXPECT_EQ(_mouse->GetY(), Mouse::RESET_Y);
    EXPECT_EQ(_mouse->GetButtons(), 0xFF);
}

TEST_F(DebugMouseManager_Test, Recording_JournalsEveryMouseMutationBeforeApplying)
{
    ASSERT_TRUE(EnableRecording());

    ASSERT_TRUE(_manager->Move(7, -2).ok());
    ASSERT_TRUE(_manager->Wheel(3).ok());
    ASSERT_TRUE(_manager->SetCounters(100, 120).ok());
    ASSERT_TRUE(_manager->Click(MouseButton::Middle, 1).ok());
    _manager->OnFrame();  // timed release goes through the journalled path

    auto moves = JournalOfKind(TTDInputKind::MouseMove);
    ASSERT_EQ(moves.size(), 1u);
    EXPECT_EQ(moves[0].dx, 7);
    EXPECT_EQ(moves[0].dy, -2);

    auto wheels = JournalOfKind(TTDInputKind::MouseWheel);
    ASSERT_EQ(wheels.size(), 1u);
    EXPECT_EQ(wheels[0].wheelSteps, 3);

    auto counters = JournalOfKind(TTDInputKind::MouseCounters);
    ASSERT_EQ(counters.size(), 1u);
    EXPECT_EQ(counters[0].dx, 100);
    EXPECT_EQ(counters[0].dy, 120);

    auto buttons = JournalOfKind(TTDInputKind::MouseButtons);
    ASSERT_EQ(buttons.size(), 2u) << "click press + timed release";
    EXPECT_EQ(buttons[0].buttonMask, 0xFB);
    EXPECT_EQ(buttons[1].buttonMask, 0xFF);
}

TEST_F(DebugMouseManager_Test, HostMessageCenterInput_JournalledWhileRecording)
{
    ASSERT_TRUE(EnableRecording());
    const std::string id = _emulator->GetId();

    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_MOVE, MouseEvent::Move(4, 1, id));
    ASSERT_TRUE(TestWait::For([&] { return _mouse->GetX() == Mouse::RESET_X + 4; }));
    MessageCenter::DefaultMessageCenter().Post(MC_MOUSE_BUTTON, MouseEvent::Buttons(0xFE, id));
    ASSERT_TRUE(TestWait::For([&] { return _mouse->GetButtons() == 0xFE; }));

    auto moves = JournalOfKind(TTDInputKind::MouseMove);
    ASSERT_EQ(moves.size(), 1u) << "host input goes through the funnel and is journalled";
    EXPECT_EQ(moves[0].dx, 4);
    EXPECT_EQ(JournalOfKind(TTDInputKind::MouseButtons).size(), 1u);
}

TEST_F(DebugMouseManager_Test, JournalInject_AppliesMouseKinds)
{
    ttd::TTDInputJournal journal;
    auto make = [](TTDInputKind kind) {
        ttd::TTDInputEvent ev;
        ev.time = {3, 500};
        ev.kind = kind;
        return ev;
    };
    ttd::TTDInputEvent move = make(TTDInputKind::MouseMove);
    move.dx = -5;
    move.dy = 9;
    ttd::TTDInputEvent buttons = make(TTDInputKind::MouseButtons);
    buttons.buttonMask = 0xFD;
    ttd::TTDInputEvent wheel = make(TTDInputKind::MouseWheel);
    wheel.wheelSteps = -1;
    ttd::TTDInputEvent later = make(TTDInputKind::MouseCounters);
    later.time = {3, 900};
    later.dx = 1;
    later.dy = 2;
    journal.Record(move);
    journal.Record(buttons);
    journal.Record(wheel);
    journal.Record(later);

    EXPECT_EQ(journal.InjectDueEvents(nullptr, nullptr, {3, 500}), 0u) << "no devices - nothing injected";
    EXPECT_EQ(journal.InjectDueEvents(_context->pKeyboard, _mouse, {3, 500}), 3u);
    EXPECT_EQ(_mouse->GetX(), Mouse::RESET_X - 5);
    EXPECT_EQ(_mouse->GetY(), Mouse::RESET_Y + 9);
    EXPECT_EQ(_mouse->GetButtons(), 0xFD);
    EXPECT_EQ(_mouse->GetWheel(), 15);

    EXPECT_EQ(journal.InjectDueEvents(_context->pKeyboard, _mouse, {3, 900}), 1u);
    EXPECT_EQ(_mouse->GetX(), 1);
    EXPECT_EQ(_mouse->GetY(), 2);
}

/// endregion </Replay guard and journal>

/// region <Device: concurrency and TTD state>

TEST_F(DebugMouseManager_Test, Move_ConcurrentWritersLoseNothing)
{
    constexpr int kPerThread = 100000;
    std::thread a([&] {
        for (int i = 0; i < kPerThread; i++)
            _mouse->Move(1, 0);
    });
    std::thread b([&] {
        for (int i = 0; i < kPerThread; i++)
            _mouse->Move(1, 0);
    });
    a.join();
    b.join();
    EXPECT_EQ(_mouse->GetX(), (Mouse::RESET_X + 2 * kPerThread) % 256);
}

TEST_F(DebugMouseManager_Test, TTDBlob_RoundTripAndHash)
{
    ASSERT_EQ(_mouse->TTDStateSize(), 8u);
    _mouse->SetCounters(10, 200);
    _mouse->SetButtons(0xFA);
    _mouse->SetWheel(6);

    std::vector<uint8_t> blob(_mouse->TTDStateSize());
    _mouse->TTDSaveState(blob.data());
    const uint64_t hash = _mouse->TTDHashState();

    _mouse->Reset();
    EXPECT_NE(_mouse->TTDHashState(), hash) << "state participates in divergence detection";

    _mouse->TTDLoadState(blob.data());
    EXPECT_EQ(_mouse->GetX(), 10);
    EXPECT_EQ(_mouse->GetY(), 200);
    EXPECT_EQ(_mouse->GetButtons(), 0xFA);
    EXPECT_EQ(_mouse->GetWheel(), 6);
    EXPECT_EQ(_mouse->TTDHashState(), hash);
    EXPECT_EQ(_mouse->TTDPeripheralId(), ttd::PeripheralId::KempstonMouse);
}

TEST_F(DebugMouseManager_Test, TTDSeek_RestoresMouseStateFromCheckpoint)
{
    ASSERT_TRUE(EnableRecording());
    _emulator->RunNFrames(2, true);
    ASSERT_TRUE(_manager->SetCounters(50, 60).ok());
    _emulator->RunNFrames(2, true);  // checkpoints at frames 3-4 carry X=50
    ASSERT_TRUE(_manager->Move(20, 0).ok());
    _emulator->RunNFrames(2, true);
    _ttd->StopRecording();
    ASSERT_EQ(_mouse->GetX(), 70);

    ASSERT_TRUE(_ttd->SeekTo({4, 0}));
    EXPECT_EQ(_mouse->GetX(), 50) << "the mouse is a registered core peripheral: checkpoints restore it";
    EXPECT_EQ(_mouse->GetY(), 60);
}

TEST_F(DebugMouseManager_Test, MachineResetKeepsMouseState)
{
    // RESET does not reach the interface: counters are power-on only (MiSTer mouse.v resets on
    // cold_reset), and the buttons mirror the physical mouse
    ASSERT_TRUE(_manager->SetCounters(1, 2).ok());
    ASSERT_TRUE(_manager->PressButton(MouseButton::Left).ok());
    _emulator->Reset();
    EXPECT_EQ(_mouse->GetX(), 1);
    EXPECT_EQ(_mouse->GetY(), 2);
    EXPECT_EQ(_mouse->GetButtons(), 0xFE);
}

TEST_F(DebugMouseManager_Test, FeatureFlagControlsPresence)
{
    FeatureManager* fm = _emulator->GetFeatureManager();
    ASSERT_NE(fm, nullptr);
    fm->setFeature(Features::kKempstonMouse, false);
    EXPECT_FALSE(_mouse->IsPresent());
    fm->setFeature(Features::kKempstonMouse, true);
    EXPECT_TRUE(_mouse->IsPresent());
}

/// endregion </Device>

/// region <Keyboard journal fixes>

TEST_F(DebugMouseManager_Test, KeyboardTap_TimedReleaseIsJournalled)
{
    ASSERT_TRUE(EnableRecording());
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    km->TapKey(ZXKEY_Q, 1);
    for (int frame = 0; frame < 4; frame++)
        km->OnFrame();

    auto keys = JournalOfKind(TTDInputKind::Key);
    ASSERT_EQ(keys.size(), 2u) << "tap press and its timed release are both journalled";
    EXPECT_EQ(keys[0].key, static_cast<uint8_t>(ZXKEY_Q));
    EXPECT_TRUE(keys[0].pressed);
    EXPECT_FALSE(keys[1].pressed);
}

TEST_F(DebugMouseManager_Test, KeyboardTap_RefusedDuringReplay)
{
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    _context->ttdReplayActive = true;
    km->TapKey(ZXKEY_Q, 1);
    const bool pressedDuringReplay = KeyDown(0xFBFE, 0);
    km->AbortSequence();
    _context->ttdReplayActive = false;

    EXPECT_FALSE(pressedDuringReplay) << "timed keyboard ops must not mutate the matrix during replay";
}

TEST_F(DebugMouseManager_Test, KeyboardReleaseAll_JournalsMatrixReset)
{
    ASSERT_TRUE(EnableRecording());
    DebugKeyboardManager* km = _context->pDebugManager->GetKeyboardManager();
    ASSERT_NE(km, nullptr);

    km->PressKey(ZXKEY_A);
    km->ReleaseAllKeys();
    EXPECT_EQ(JournalOfKind(TTDInputKind::KeyboardReset).size(), 1u);

    // Replaying the reset releases a key pressed from outside the manager too
    _context->pKeyboard->PressKey(ZXKEY_S);
    ttd::TTDInputJournal journal;
    ttd::TTDInputEvent reset;
    reset.time = {1, 1};
    reset.kind = TTDInputKind::KeyboardReset;
    journal.Record(reset);
    EXPECT_EQ(journal.InjectDueEvents(_context->pKeyboard, nullptr, {1, 1}), 1u);
    EXPECT_FALSE(KeyDown(0xFDFE, 1));
}

TEST_F(DebugMouseManager_Test, HostKeystrokes_JournalledWhileRecordingAndSuppressedDuringReplay)
{
    ASSERT_TRUE(EnableRecording());
    const std::string id = std::string(_emulator->GetUUID());
    MessageCenter& mc = MessageCenter::DefaultMessageCenter();

    mc.Post(MC_KEY_PRESSED, new KeyboardEvent(ZXKEY_W, KEY_PRESSED, id));
    ASSERT_TRUE(TestWait::For([&] { return KeyDown(0xFBFE, 1); }));
    mc.Post(MC_KEY_RELEASED, new KeyboardEvent(ZXKEY_W, KEY_RELEASED, id));
    ASSERT_TRUE(TestWait::For([&] { return !KeyDown(0xFBFE, 1); }));

    auto keys = JournalOfKind(TTDInputKind::Key);
    ASSERT_EQ(keys.size(), 2u) << "desktop keystrokes are journalled like automation input";
    EXPECT_TRUE(keys[0].pressed);
    EXPECT_FALSE(keys[1].pressed);

    _context->ttdReplayActive = true;
    mc.Post(MC_KEY_PRESSED, new KeyboardEvent(ZXKEY_E, KEY_PRESSED, id));
    // Marker: a mouse event on the same MessageCenter worker proves the key event was dispatched
    _context->ttdReplayActive = true;
    _mouse->SetCounters(0, 0);
    mc.Post(MC_MOUSE_MOVE, MouseEvent::Move(1, 0, id));
    const bool markerApplied = TestWait::For([&] { return _mouse->GetX() == 1; }, std::chrono::milliseconds(300));
    _context->ttdReplayActive = false;

    EXPECT_FALSE(markerApplied) << "host mouse input is suppressed during replay too";
    EXPECT_FALSE(KeyDown(0xFBFE, 2)) << "host keystroke must not reach the matrix during replay";
}

/// endregion </Keyboard journal fixes>
