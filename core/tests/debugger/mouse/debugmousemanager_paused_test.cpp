/// @file debugmousemanager_paused_test.cpp
/// @brief Automation mouse input on a paused, running emulator is applied before the call returns.
///
/// The API promises "the change is applied before the response". While the emulation thread runs, live
/// input is queued for its next instruction boundary (TimeTravelController::SubmitLiveInput). A paused machine
/// has no instruction boundary until it resumes, so the queue used to hold the input and a status read right
/// after it showed the old values (the WebAPI's test_api_mouse.py: 8 cases). A parked machine now takes the
/// input on the caller's thread at once (Emulator::RunWhileParked), in order after anything queued before.
/// Over the 50 ms budget (~0.1-0.2 s): an emulator thread is started, run and paused for real.

#include <gtest/gtest.h>

#include <string>

#include "_helpers/testwaithelper.h"
#include "debugger/debugmanager.h"
#include "debugger/mouse/debugmousemanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"

class DebugMouseManagerPaused_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _manager = EmulatorManager::GetInstance();
        ASSERT_NE(_manager, nullptr);
        std::string error;
        _emulator = _manager->CreateEmulatorWithModel("mouse-paused", "PENTAGON", LoggerLevel::LogError, &error);
        ASSERT_TRUE(_emulator) << error;
        _emulator->EnableTurboMode(false);
        _emulator->StartAsync();
        ASSERT_TRUE(TestWait::For([this]() { return _emulator->GetState() == StateRun; }));
        _api = _emulator->GetContext()->pDebugManager->GetMouseManager();
        ASSERT_NE(_api, nullptr);
    }

    void TearDown() override
    {
        if (_emulator)
            _manager->RemoveEmulator(_emulator->GetId());
    }

    void PauseParked()
    {
        _emulator->Pause();
        ASSERT_TRUE(_emulator->WaitForPauseConfirmation(2000));
        ASSERT_TRUE(_emulator->IsEmulationParked());
    }

    EmulatorManager* _manager = nullptr;
    std::shared_ptr<Emulator> _emulator;
    DebugMouseManager* _api = nullptr;
};

TEST_F(DebugMouseManagerPaused_Test, InputOnAPausedMachineIsVisibleBeforeTheCallReturns)
{
    PauseParked();
    const uint64_t frame = _emulator->GetContext()->emulatorState.frame_counter;

    ASSERT_TRUE(_api->SetCounters(100, 100).ok());
    ASSERT_TRUE(_api->ReleaseAllButtons().ok());
    MouseStateSnapshot st = _api->GetState();
    EXPECT_EQ(st.x, 100);
    EXPECT_EQ(st.y, 100);

    ASSERT_TRUE(_api->Move(10, -5).ok());
    st = _api->GetState();
    EXPECT_EQ(st.x, 110) << "applied before Move returned, the machine still paused";
    EXPECT_EQ(st.y, 95);

    ASSERT_TRUE(_api->PressButton(MouseButton::Left).ok());
    EXPECT_TRUE(_api->GetState().IsPressed(MouseButton::Left));
    ASSERT_TRUE(_api->ReleaseButton(MouseButton::Left).ok());
    EXPECT_FALSE(_api->GetState().IsPressed(MouseButton::Left));

    EXPECT_EQ(_emulator->GetContext()->emulatorState.frame_counter, frame) << "nothing ran: the machine stayed paused";
    EXPECT_TRUE(_emulator->IsPaused());

    // Resume applies nothing twice: the counters keep their values while the machine runs on
    _emulator->Resume();
    ASSERT_TRUE(TestWait::For([&]() { return _emulator->GetContext()->emulatorState.frame_counter > frame + 2; }));
    st = _api->GetState();
    EXPECT_EQ(st.x, 110);
    EXPECT_EQ(st.y, 95);
}

TEST_F(DebugMouseManagerPaused_Test, InputOnARunningMachineStillReachesIt)
{
    ASSERT_TRUE(_api->SetCounters(50, 60).ok());
    ASSERT_TRUE(TestWait::For([&]() {
        const MouseStateSnapshot st = _api->GetState();
        return st.x == 50 && st.y == 60;
    })) << "queued for the running emulation thread, applied at its next instruction boundary";
}
