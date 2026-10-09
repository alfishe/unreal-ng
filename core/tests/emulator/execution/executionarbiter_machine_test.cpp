// The execution arbiter wired into a running emulator (docs/inprogress/2026-10-09-execution-arbiter/design.md §1,
// §6 X1): the compat silent pair keeps a pause somebody else asked for, and no second thread drives the Z80

#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <thread>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"

using namespace std::chrono_literals;

namespace
{

uint64_t Frame(Emulator* emulator)
{
    return emulator->GetContext()->emulatorState.frame_counter;
}

class ExecutionArbiterMachine_Test : public ::testing::Test
{
protected:
    Emulator* _emulator = nullptr;

    void SetUp() override
    {
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _emulator->EnableTurboMode();  // the frames only have to advance; nothing here looks at pixels
        _emulator->StartAsync();
        ASSERT_TRUE(TestWait::For([&]() { return Frame(_emulator) > 1; }));
    }

    void TearDown() override
    {
        if (_emulator)
        {
            EXPECT_EQ(_emulator->DriverViolations(), 0u) << "two threads executed the Z80 at once";
            EmulatorTestHelper::CleanupEmulator(_emulator);
        }
    }
};

}  // namespace

/// The bug that led to the arbiter, without its GUI: one thread does silent Pause(false) / Resume(false) pairs (the
/// GUI's write-journal switch on adoption), another pauses and steps frames (automation's pause + run_frames). Before,
/// a silent resume landing inside the step unparked the main loop: it ran frames alongside RunNFrames (a SPRINTER
/// boot then ended anywhere). Now the silent pair is a hold of the CPU that never touches the pause
// 20 rounds: on master (before the arbiter) this failed 10 runs out of 10; a run here takes about 50 ms
TEST_F(ExecutionArbiterMachine_Test, SilentPairsOnAnotherThreadNeverResumeAStep)
{
    std::atomic<bool> stop{false};
    std::atomic<int> pairs{0};
    std::thread journalSwitch([&]() {
        while (!stop.load())
        {
            _emulator->Pause(false);
            std::this_thread::yield();
            _emulator->Resume(false);
            pairs.fetch_add(1);
        }
    });
    ASSERT_TRUE(TestWait::ForAtLeast(pairs, 3));

    for (int round = 0; round < 20; ++round)
    {
        _emulator->Pause();
        const uint64_t before = Frame(_emulator);
        _emulator->RunNFrames(4);
        EXPECT_EQ(Frame(_emulator), before + 4) << "round " << round << ": the main loop ran frames during the step";
        EXPECT_TRUE(_emulator->IsPaused()) << "round " << round << ": a silent resume lifted the pause";
        _emulator->Resume();
    }
    stop = true;
    journalSwitch.join();
}

/// A pause asked for while another thread holds the CPU silently is queued, not lost: it applies when the hold ends
TEST_F(ExecutionArbiterMachine_Test, APauseDuringASilentPauseSurvivesItsResume)
{
    ASSERT_FALSE(_emulator->IsPaused());
    _emulator->Pause(false);
    EXPECT_FALSE(_emulator->IsPaused()) << "a silent pause is not a pause: it is invisible";
    EXPECT_TRUE(_emulator->IsEmulationParked());

    std::atomic<bool> paused{false};
    std::thread automation([&]() {
        _emulator->Pause();
        paused = true;
    });
    ASSERT_TRUE(TestWait::For([&]() { return _emulator->GetArbiter().GetSnapshot().queue.size() == 1; }));
    _emulator->Resume(false);
    automation.join();
    EXPECT_TRUE(_emulator->IsPaused());
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(_emulator->WaitForPauseConfirmation(1000));
    EXPECT_EQ(Frame(_emulator), frame) << "parked";
    _emulator->Resume();
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; }));
}

/// A silent pair inside a silent pair, and a step inside a silent pair, leave the intent alone
TEST_F(ExecutionArbiterMachine_Test, StepsAndNestedPairsInsideASilentPairKeepTheMachineRunning)
{
    _emulator->Pause(false);
    _emulator->Pause(false);
    const uint64_t before = Frame(_emulator);
    _emulator->RunNFrames(1);
    EXPECT_EQ(Frame(_emulator), before + 1);
    _emulator->Resume(false);
    _emulator->Resume(false);
    EXPECT_FALSE(_emulator->IsPaused()) << "the step inside the hold did not leave a pause behind";
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > before + 2; })) << "running again";
}

/// Stop() right after a silent pause (MainWindow's model rebuild) ends the hold; the machine starts again cleanly
TEST_F(ExecutionArbiterMachine_Test, StopEndsAnOpenSilentPause)
{
    _emulator->Pause(false);
    _emulator->Stop();
    EXPECT_FALSE(_emulator->GetArbiter().GetSnapshot().serving.has_value());
    _emulator->StartAsync();
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; }));
    _emulator->Resume(false);  // the pair's end, after the stop: nothing left to end
    EXPECT_FALSE(_emulator->IsPaused());
}

/// X2: a direct run is a driving command. While RunNFrames steps on one thread, a command from another thread waits
/// for it (before, a silent pause was granted at once and mutated the machine mid-run)
TEST_F(ExecutionArbiterMachine_Test, ADirectRunKeepsEveryOtherThreadOut)
{
    _emulator->Pause();
    std::atomic<bool> stepping{false};
    std::atomic<uint64_t> startFrame{0};
    uint64_t framesStepped = 0;
    std::thread automation([&]() {
        startFrame = Frame(_emulator);
        stepping = true;
        _emulator->RunNFrames(30);
        framesStepped = Frame(_emulator) - startFrame.load();
    });
    ASSERT_TRUE(TestWait::For([&]() { return stepping.load(); }));
    ASSERT_TRUE(TestWait::For([&]() {
        const auto snapshot = _emulator->GetArbiter().GetSnapshot();
        return snapshot.serving.has_value() && snapshot.servingDrivesZ80;
    })) << "the run holds the CPU as a driving command";

    // Another thread's command (a media insert, a TTD journal switch) while the run steps: served after it
    bool ranAfterTheRun = false;
    _emulator->GetArbiter().Execute(execution::Requester{execution::Origin::Ttd, {}}, "ttd.journal",
                                    [&](execution::CommandContext&) {
                                        ranAfterTheRun = Frame(_emulator) - startFrame.load() >= 30;
                                    });
    EXPECT_TRUE(ranAfterTheRun) << "the command was served during the run";
    automation.join();
    EXPECT_EQ(framesStepped, 30u) << "the main loop ran frames during the run";
    EXPECT_TRUE(_emulator->IsPaused()) << "stepping leaves the machine paused";
}

TEST_F(ExecutionArbiterMachine_Test, AResumeDuringADirectRunAppliesAfterIt)
{
    _emulator->Pause();
    std::atomic<bool> running{false};
    uint64_t framesStepped = 0;
    std::thread automation([&]() {
        const uint64_t before = Frame(_emulator);
        running = true;
        _emulator->RunNFrames(30);
        framesStepped = Frame(_emulator) - before;
    });
    ASSERT_TRUE(TestWait::For([&]() { return running.load() && _emulator->GetArbiter().GetSnapshot().serving.has_value(); }));
    execution::Ticket resume = _emulator->GetArbiter().SubmitIntent(
        execution::Intent::Run, execution::Requester{execution::Origin::Gui, {}});
    automation.join();
    EXPECT_EQ(framesStepped, 30u);
    EXPECT_EQ(resume.Wait(std::chrono::seconds(1)).outcome, execution::Outcome::Applied);
    EXPECT_FALSE(_emulator->IsPaused()) << "the Resume submitted during the run applies after it";
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; })) << "running again";
}

/// X3: a reset and the media / video operations hold the CPU instead of a Pause() / Resume() bracket. They no longer
/// broadcast Paused / Resumed (a debugger or GDB read them as stops), and they leave the pause as it was
TEST_F(ExecutionArbiterMachine_Test, ResetAndMachineOperationsNeitherPauseNorBroadcast)
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::atomic<int> paused{0};
    std::atomic<int> resumed{0};
    const std::string id = _emulator->GetId();
    const uint64_t observer = messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, [&](int, Message* message) {
        auto* payload = message ? dynamic_cast<EmulatorStateChangePayload*>(message->obj) : nullptr;
        if (!payload || payload->emulatorId.toString() != id)
            return;
        if (payload->_payloadNumber == StatePaused)
            paused.fetch_add(1);
        else if (payload->_payloadNumber == StateResumed)
            resumed.fetch_add(1);
    });

    _emulator->Reset();
    _emulator->SetOverscanMode(true);
    _emulator->SetOverscanMode(false);
    _emulator->RequestNMI();
    EXPECT_FALSE(_emulator->IsPaused()) << "a running machine runs on";
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; }));

    // A sentinel: this pause's broadcast arrives after any the operations above posted
    _emulator->Pause();
    ASSERT_TRUE(TestWait::For([&]() { return paused.load() >= 1; }));
    EXPECT_EQ(paused.load(), 1) << "the operations broadcast a pause";
    EXPECT_EQ(resumed.load(), 0) << "the operations broadcast a resume";

    // A paused machine stays paused through a reset
    _emulator->Reset();
    EXPECT_TRUE(_emulator->IsPaused());
    const uint64_t pausedFrame = Frame(_emulator);
    EXPECT_TRUE(_emulator->WaitForPauseConfirmation(1000));
    EXPECT_EQ(Frame(_emulator), pausedFrame);
    messageCenter.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, observer);
}
