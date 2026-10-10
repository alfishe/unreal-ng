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
#include "common/uuid.h"
#include "emulator/emulatorcontext.h"
#include "emulator/notifications.h"
#include "emulator/state/devicestate.h"

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
    EXPECT_TRUE(TestWait::ForAtLeast(pairs, 3));  // EXPECT: a live thread must reach its join

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
    EXPECT_TRUE(TestWait::For([&]() { return _emulator->GetArbiter().GetSnapshot().queue.size() == 1; }));
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

namespace
{
/// A direct run that stays in progress until released: RunUntilCondition whose predicate (called on the stepping
/// thread, inside the run's hold) blocks at its first call. The thread is joined on every path, an early test failure
/// included (a joinable std::thread destroyed is std::terminate)
class GatedDirectRun
{
public:
    explicit GatedDirectRun(Emulator* emulator)
    {
        _thread = std::thread([this, emulator]() {
            emulator->RunUntilCondition(
                [this](const Z80State&) {
                    _entered = true;
                    TestWait::For([this]() { return _released.load(); }, std::chrono::seconds(5));
                    return true;
                },
                1000000, true);
        });
    }
    ~GatedDirectRun() { Release(); }
    bool WaitEntered() { return TestWait::For([this]() { return _entered.load(); }); }
    void Release()
    {
        _released = true;
        if (_thread.joinable())
            _thread.join();
    }

private:
    std::atomic<bool> _entered{false};
    std::atomic<bool> _released{false};
    std::thread _thread;
};
}  // namespace

/// X2: a direct run is a driving command. While it runs on one thread, a command from another thread waits for it
/// (before, a silent pause was granted at once and mutated the machine mid-run)
TEST_F(ExecutionArbiterMachine_Test, ADirectRunKeepsEveryOtherThreadOut)
{
    _emulator->Pause();
    GatedDirectRun run(_emulator);
    ASSERT_TRUE(run.WaitEntered());
    const auto snapshot = _emulator->GetArbiter().GetSnapshot();
    EXPECT_TRUE(snapshot.serving.has_value() && snapshot.servingDrivesZ80) << "the run holds the CPU as a driving command";

    // Another thread's command (a media insert, a TTD journal switch) while the run is in progress
    std::atomic<bool> commandRan{false};
    std::thread other([&]() {
        _emulator->GetArbiter().Execute(execution::Requester{execution::Origin::Ttd, {}}, "ttd.journal",
                                        [&](execution::CommandContext&) { commandRan = true; });
    });
    EXPECT_FALSE(TestWait::For([&]() { return commandRan.load(); }, std::chrono::milliseconds(20)))
        << "the command was served during the run";
    run.Release();
    other.join();
    EXPECT_TRUE(commandRan.load()) << "served after the run";
    EXPECT_TRUE(_emulator->IsPaused()) << "stepping leaves the machine paused";
}

TEST_F(ExecutionArbiterMachine_Test, AResumeDuringADirectRunAppliesAfterIt)
{
    _emulator->Pause();
    GatedDirectRun run(_emulator);
    ASSERT_TRUE(run.WaitEntered());
    const uint64_t frame = Frame(_emulator);
    execution::Ticket resume = _emulator->GetArbiter().SubmitIntent(
        execution::Intent::Run, execution::Requester{execution::Origin::Gui, {}});
    EXPECT_FALSE(TestWait::For([&]() { return resume.Done(); }, std::chrono::milliseconds(20)))
        << "the Resume applied during the run";
    EXPECT_EQ(Frame(_emulator), frame) << "the main loop ran during the run";
    run.Release();
    EXPECT_EQ(resume.Wait(std::chrono::seconds(1)).outcome, execution::Outcome::Applied);
    EXPECT_FALSE(_emulator->IsPaused()) << "the Resume submitted during the run applies after it";
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
    // Removed on every path: an early ASSERT must not leave an observer that captures this test's locals
    struct ObserverGuard
    {
        MessageCenter& center;
        uint64_t id;
        ~ObserverGuard() { center.RemoveObserverById(NC_EMULATOR_STATE_CHANGE, id); }
    } observerGuard{messageCenter, observer};

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
}

namespace
{
const execution::Requester kGui{execution::Origin::Gui, {}};
const execution::Requester kDezog{execution::Origin::DeZog, {}};
const execution::Requester kTtd{execution::Origin::Ttd, {}};
}  // namespace

/// X4 (owner decision 2026-10-09): DeZog's session close and the TTD resume verb lift only a pause they set themselves.
/// A pause the user asked for stays; theirs is lifted
TEST_F(ExecutionArbiterMachine_Test, OwnerOnlyResumesLiftOnlyTheirOwnPause)
{
    for (const execution::Requester& owner : {kDezog, kTtd})
    {
        // The user's pause: the surface's close / resume leaves it
        ASSERT_EQ(_emulator->Pause(kGui).outcome, execution::Outcome::Applied);
        EXPECT_EQ(_emulator->Resume(owner, true).outcome, execution::Outcome::Refused)
            << execution::OriginName(owner.origin);
        EXPECT_TRUE(_emulator->IsPaused());
        EXPECT_EQ(_emulator->GetArbiter().GetSnapshot().intentSetBy.origin, execution::Origin::Gui);
        ASSERT_EQ(_emulator->Resume(kGui).outcome, execution::Outcome::Applied);

        // Its own pause: lifted. A second pause by the user on top does not take it over
        ASSERT_EQ(_emulator->Pause(owner).outcome, execution::Outcome::Applied);
        EXPECT_EQ(_emulator->Pause(kGui).outcome, execution::Outcome::Applied);
        EXPECT_EQ(_emulator->Resume(owner, true).outcome, execution::Outcome::Applied)
            << execution::OriginName(owner.origin);
        EXPECT_FALSE(_emulator->IsPaused());
    }
}

/// X4: the run-control claim (GDB, TDD 3.3) is enforced by the arbiter for every surface: another surface's run is
/// refused while it is held, its pause is not; the holder runs; after the release everybody does
TEST_F(ExecutionArbiterMachine_Test, TheRunControlClaimRefusesOtherSurfacesRuns)
{
    EmulatorContext* context = _emulator->GetContext();
    const unreal::UUID gdbSession = unreal::UUID::Generate();
    const execution::Requester gdb{execution::Origin::Gdb, gdbSession.toString()};
    ASSERT_TRUE(context->TakeRunControl(gdbSession, "gdb"));

    EXPECT_EQ(_emulator->Pause(kGui).outcome, execution::Outcome::Applied) << "a pause is always allowed";
    const execution::Result refused = _emulator->Resume(kGui);
    EXPECT_EQ(refused.outcome, execution::Outcome::Refused);
    EXPECT_NE(refused.detail.find("claim"), std::string::npos) << refused.detail;
    EXPECT_TRUE(_emulator->IsPaused());
    EXPECT_EQ(_emulator->Resume(gdb).outcome, execution::Outcome::Applied) << "the holder runs";
    EXPECT_FALSE(_emulator->IsPaused());

    ASSERT_EQ(_emulator->Pause(gdb).outcome, execution::Outcome::Applied);
    context->ReleaseRunControl(gdbSession);
    EXPECT_EQ(_emulator->Resume(kGui).outcome, execution::Outcome::Applied) << "released: everybody runs";
}

/// X5: the arbiter's report (DeviceState::Execution) - the one every surface renders (WebAPI /state/execution, MCP
/// `execution`, CLI `state execution`, Lua / Python execution_state())
TEST_F(ExecutionArbiterMachine_Test, TheExecutionReportShowsTheIntentItsOwnerAndTheHolder)
{
    ASSERT_EQ(_emulator->Pause(execution::Requester{execution::Origin::Cli, {}}, "user").outcome,
              execution::Outcome::Applied);
    StateNode report = DeviceState::Execution(_emulator->GetContext());
    ASSERT_TRUE(report.find("available") && report.find("available")->b);
    EXPECT_EQ(report.find("phase")->s, "running");
    EXPECT_EQ(report.find("intent")->s, "pause");
    EXPECT_TRUE(report.find("paused")->b);
    EXPECT_EQ(report.find("intent_set_by")->find("origin")->s, "cli");
    EXPECT_EQ(report.find("intent_reason")->s, "user");
    EXPECT_TRUE(report.find("parked")->b);
    EXPECT_EQ(report.find("serving")->kind, StateNode::Kind::Null);
    EXPECT_EQ(report.find("driver_violations")->i, 0);

    // A command holding the CPU shows as `serving`, with its tag and origin
    {
        Emulator::CpuHold hold(*_emulator, "report test");
        report = DeviceState::Execution(_emulator->GetContext());
        const StateNode* serving = report.find("serving");
        ASSERT_TRUE(serving && serving->isObject());
        EXPECT_EQ(serving->find("tag")->s, "report test");
        EXPECT_FALSE(serving->find("drives_z80")->b);
    }
    _emulator->Resume(execution::Requester{execution::Origin::Cli, {}});
}
