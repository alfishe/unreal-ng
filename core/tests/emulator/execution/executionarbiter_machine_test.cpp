// The execution arbiter wired into a running emulator (docs/inprogress/2026-10-09-execution-arbiter/design.md §1,
// §6 X1): the compat silent pair keeps a pause somebody else asked for, and no second thread drives the Z80

#include "stdafx.h"
#include "pch.h"

#include <atomic>
#include <optional>
#include <thread>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "3rdparty/message-center/messagecenter.h"
#include "emulator/emulator.h"
#include "common/uuid.h"
#include "emulator/emulatorcontext.h"
#include "emulator/execution/executionarbiter.h"
#include "emulator/execution/scriptsession.h"
#include "emulator/cpu/core.h"
#include "emulator/cpu/z80.h"
#include "emulator/memory/memory.h"
#include "emulator/notifications.h"
#include "emulator/state/devicestate.h"

using namespace std::chrono_literals;

namespace
{

uint64_t Frame(Emulator* emulator)
{
    return emulator->GetContext()->emulatorState.frame_counter;
}

/// Pause() waits briefly (500 ms) for the pause to apply; on a starved machine (a test run under heavy load) it can
/// return before, and the loop then runs a frame more. A test that counts frames from here waits until the pause is
/// in effect: the intent is Pause and the loop parked
bool PausedForCounting(Emulator* emulator)
{
    emulator->Pause();
    return TestWait::For([&]() {
        const execution::Snapshot s = emulator->GetArbiter().GetSnapshot();
        return s.intent == execution::Intent::Pause && s.loopParked;
    });
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

/// The bug that led to the arbiter, without its GUI: one thread holds the CPU briefly, again and again (the GUI's
/// write-journal switch on adoption; a silent Pause(false) / Resume(false) pair before the arbiter), another pauses and
/// steps frames (automation's pause + run_frames). Before, a silent resume landing inside the step unparked the main
/// loop: it ran frames alongside RunNFrames (a SPRINTER boot then ended anywhere). Now the silent pair is a hold of the
/// CPU that never touches the pause
// 20 rounds: on master (before the arbiter) this failed 10 runs out of 10; a run here takes about 50 ms
TEST_F(ExecutionArbiterMachine_Test, CpuHoldsOnAnotherThreadNeverResumeAStep)
{
    std::atomic<bool> stop{false};
    std::atomic<int> pairs{0};
    std::thread journalSwitch([&]() {
        while (!stop.load())
        {
            {
                Emulator::CpuHold hold(*_emulator, "ttd.journal");  // what the silent pair became
                std::this_thread::yield();
            }
            pairs.fetch_add(1);
        }
    });
    EXPECT_TRUE(TestWait::ForAtLeast(pairs, 3));  // EXPECT: a live thread must reach its join

    for (int round = 0; round < 20; ++round)
    {
        EXPECT_TRUE(PausedForCounting(_emulator)) << "round " << round;
        const uint64_t before = Frame(_emulator);
        _emulator->RunNFrames(4);
        EXPECT_EQ(Frame(_emulator), before + 4) << "round " << round << ": the main loop ran frames during the step";
        EXPECT_TRUE(_emulator->IsPaused()) << "round " << round << ": a silent resume lifted the pause";
        _emulator->Resume();
    }
    stop = true;
    journalSwitch.join();
}

/// A pause asked for while another thread holds the CPU is queued, not lost: it applies when the hold ends
TEST_F(ExecutionArbiterMachine_Test, APauseDuringACpuHoldSurvivesItsEnd)
{
    ASSERT_FALSE(_emulator->IsPaused());
    std::optional<Emulator::CpuHold> hold;
    hold.emplace(*_emulator, "silent work");
    EXPECT_FALSE(_emulator->IsPaused()) << "a hold is not a pause: it is invisible";
    EXPECT_TRUE(_emulator->IsEmulationParked());

    std::atomic<bool> paused{false};
    std::thread automation([&]() {
        _emulator->Pause();
        paused = true;
    });
    EXPECT_TRUE(TestWait::For([&]() { return _emulator->GetArbiter().GetSnapshot().queue.size() == 1; }));
    hold.reset();
    automation.join();
    EXPECT_TRUE(_emulator->IsPaused());
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(_emulator->WaitForPauseConfirmation(1000));
    EXPECT_EQ(Frame(_emulator), frame) << "parked";
    _emulator->Resume();
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; }));
}

/// A hold inside a hold, and a step inside a hold, leave the intent alone
TEST_F(ExecutionArbiterMachine_Test, StepsAndNestedHoldsInsideACpuHoldKeepTheMachineRunning)
{
    uint64_t before = 0;
    {
        Emulator::CpuHold outer(*_emulator, "outer");
        Emulator::CpuHold inner(*_emulator, "inner");
        before = Frame(_emulator);
        _emulator->RunNFrames(1);
        EXPECT_EQ(Frame(_emulator), before + 1);
    }
    EXPECT_FALSE(_emulator->IsPaused()) << "the step inside the hold did not leave a pause behind";
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > before + 2; })) << "running again";
}

/// Stop() while this thread holds the CPU (ModelSwitch stops the old machine inside its hold): the loop exits, the hold
/// ends after the stop, and the machine starts again cleanly
TEST_F(ExecutionArbiterMachine_Test, StopWhileHoldingTheCpu)
{
    {
        Emulator::CpuHold hold(*_emulator, "model switch");
        _emulator->Stop();
    }
    EXPECT_FALSE(_emulator->GetArbiter().GetSnapshot().serving.has_value());
    _emulator->StartAsync();
    const uint64_t frame = Frame(_emulator);
    EXPECT_TRUE(TestWait::For([&]() { return Frame(_emulator) > frame; }));
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

/// X7 (owner decisions 2026-10-09): the user takes control back from an automation session. Its long direct run ends
/// at once (aborted, the machine paused), its owner is refused from now on, the same client cannot take control again
/// during its cooldown unless the user allows it, another client can
TEST_F(ExecutionArbiterMachine_Test, TakingControlBackEndsTheSessionAndItsRun)
{
    EmulatorContext* context = _emulator->GetContext();
    const unreal::UUID session = unreal::UUID::Generate();
    ASSERT_TRUE(context->TakeRunControl(session, "mcp", nullptr, "agent", std::chrono::milliseconds(0)));
    EXPECT_EQ(context->GetRunControlState().client, "agent");

    // A run far longer than the test: only the take-back ends it
    std::atomic<bool> returned{false};
    uint64_t framesBefore = Frame(_emulator);
    std::thread automation([&]() {
        _emulator->RunNFrames(1000000);
        returned = true;
    });
    // Join on every path (a joinable thread destroyed is std::terminate)
    struct Joiner
    {
        std::thread& t;
        EmulatorContext* c;
        ~Joiner()
        {
            if (t.joinable())
            {
                c->RevokeRunControl("test cleanup");
                t.join();
            }
        }
    } joiner{automation, context};
    EXPECT_TRUE(TestWait::For([&]() {
        const auto s = _emulator->GetArbiter().GetSnapshot();
        return s.serving.has_value() && s.servingDrivesZ80;
    }));

    EXPECT_TRUE(context->RevokeRunControl("gui"));
    EXPECT_TRUE(TestWait::For([&]() { return returned.load(); })) << "the run went on after the take-back";
    automation.join();
    EXPECT_TRUE(_emulator->LastDirectRunAborted());
    EXPECT_LT(Frame(_emulator) - framesBefore, 1000000u);
    EXPECT_TRUE(_emulator->IsPaused()) << "the aborted run leaves the machine paused";
    EXPECT_FALSE(context->IsRunControlClaimed());
    EXPECT_TRUE(context->IsRunControlRevoked(session)) << "its requests answer revoked_by_user";

    // The same client does not take control back by itself; another client can
    std::string reason;
    EXPECT_FALSE(context->TakeRunControl(unreal::UUID::Generate(), "mcp", &reason, "agent", std::chrono::milliseconds(0)));
    EXPECT_NE(reason.find("revoked_cooldown"), std::string::npos) << reason;
    ASSERT_EQ(context->GetRunControlState().cooldowns.size(), 1u);
    EXPECT_EQ(context->GetRunControlState().cooldowns[0].first, "mcp/agent");
    const unreal::UUID other = unreal::UUID::Generate();
    EXPECT_TRUE(context->TakeRunControl(other, "webapi", nullptr, "script", std::chrono::milliseconds(0)));
    context->ReleaseRunControl(other);

    // The user allows it again
    context->AllowRunControlRetake("mcp/agent");
    const unreal::UUID again = unreal::UUID::Generate();
    EXPECT_TRUE(context->TakeRunControl(again, "mcp", nullptr, "agent", std::chrono::milliseconds(0)));
    EXPECT_FALSE(context->IsRunControlRevoked(again));
    context->ReleaseRunControl(again);

    // A direct run with no take-back is not aborted
    _emulator->RunNFrames(1);
    EXPECT_FALSE(_emulator->LastDirectRunAborted());
}

/// X7: an HTTP session ends by itself after its idle lease; every change is announced (NC_RUN_CONTROL_CHANGED)
TEST_F(ExecutionArbiterMachine_Test, AnIdleSessionExpiresAndEveryChangeIsAnnounced)
{
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    std::mutex eventsMutex;
    std::vector<std::string> events;
    const std::string id = _emulator->GetId();
    const uint64_t observer = messageCenter.AddObserver(NC_RUN_CONTROL_CHANGED, [&](int, Message* message) {
        auto* payload = message ? dynamic_cast<RunControlChangedPayload*>(message->obj) : nullptr;
        if (payload && payload->emulatorId.toString() == id)
        {
            std::lock_guard<std::mutex> lock(eventsMutex);
            events.push_back(payload->event + ":" + payload->surfaceLabel + "/" + payload->client);
        }
    });
    struct ObserverGuard
    {
        MessageCenter& center;
        uint64_t id;
        ~ObserverGuard() { center.RemoveObserverById(NC_RUN_CONTROL_CHANGED, id); }
    } observerGuard{messageCenter, observer};
    auto count = [&]() {
        std::lock_guard<std::mutex> lock(eventsMutex);
        return events.size();
    };

    EmulatorContext* context = _emulator->GetContext();
    const unreal::UUID session = unreal::UUID::Generate();
    ASSERT_TRUE(context->TakeRunControl(session, "webapi", nullptr, "bot", std::chrono::milliseconds(20)));
    context->TouchRunControl(session);
    EXPECT_TRUE(context->IsRunControlClaimed());
    EXPECT_TRUE(TestWait::For([&]() { return !context->IsRunControlClaimed(); })) << "the idle lease did not end it";
    EXPECT_FALSE(context->IsRunControlRevoked(session)) << "an expiry is not a take-back";

    const unreal::UUID next = unreal::UUID::Generate();
    ASSERT_TRUE(context->TakeRunControl(next, "cli", nullptr, "", std::chrono::milliseconds(0)));
    context->ReleaseRunControl(next);
    ASSERT_TRUE(TestWait::For([&]() { return count() >= 4; }));
    std::lock_guard<std::mutex> lock(eventsMutex);
    const std::vector<std::string> expected{"taken:webapi/bot", "expired:webapi/bot", "taken:cli/", "released:cli/"};
    EXPECT_EQ(events, expected);
}

/// X7, the script surfaces (Python / Lua, execution::script): a script's session holds control; after the user takes
/// it back, the script's next control call throws ControlRevoked (it stops, it does not retry) and a re-take by the
/// same script is refused during the cooldown
TEST_F(ExecutionArbiterMachine_Test, AScriptSessionStopsWithControlRevoked)
{
    std::string reason;
    ASSERT_TRUE(execution::script::TakeControl(*_emulator, execution::Origin::Python, "my script", reason)) << reason;
    const execution::Requester me = execution::script::RequesterOf(*_emulator, execution::Origin::Python);
    EXPECT_FALSE(me.client.empty()) << "the session's requests carry it";
    EXPECT_NO_THROW(execution::script::Check(*_emulator, execution::Origin::Python));
    EXPECT_EQ(_emulator->Pause(me).outcome, execution::Outcome::Applied);
    EXPECT_EQ(_emulator->Resume(execution::Requester{execution::Origin::Gui, {}}).outcome, execution::Outcome::Refused)
        << "another surface's run is refused while the script holds control";
    EXPECT_EQ(_emulator->Resume(me).outcome, execution::Outcome::Applied);

    ASSERT_TRUE(_emulator->GetContext()->RevokeRunControl("gui"));
    EXPECT_THROW(execution::script::Check(*_emulator, execution::Origin::Python), execution::script::ControlRevoked);
    EXPECT_NO_THROW(execution::script::Check(*_emulator, execution::Origin::Python)) << "thrown once: forgotten";
    EXPECT_FALSE(execution::script::TakeControl(*_emulator, execution::Origin::Python, "my script", reason));
    EXPECT_NE(reason.find("revoked_cooldown"), std::string::npos) << reason;
    _emulator->GetContext()->AllowRunControlRetake();
    EXPECT_TRUE(execution::script::TakeControl(*_emulator, execution::Origin::Python, "my script", reason)) << reason;
    execution::script::ReleaseControl(*_emulator, execution::Origin::Python);
    EXPECT_FALSE(_emulator->GetContext()->IsRunControlClaimed());
}

/// Reading a paused machine's video change log while automation runs frames on it (a direct run records into the
/// log, reallocating as it goes): the readers take the CPU first, so they never read a frame being written. Found by
/// an ASan soak (heap-use-after-free in DeviceState::VideoChanges against Emulator::RunNFrames)
TEST_F(ExecutionArbiterMachine_Test, ReadingTheVideoLogDuringADirectRunNeverReadsAFrameBeingWritten)
{
    _emulator->Pause(execution::Requester{execution::Origin::WebApi, {}}, "test");
    // LD A,0 / loop: OUT (#FE),A / INC A / JR loop - a border write every few T-states: the log grows all frame
    const uint8_t program[] = {0x3E, 0x00, 0xD3, 0xFE, 0x3C, 0x18, 0xFB};
    Memory* memory = _emulator->GetContext()->pMemory;
    for (size_t i = 0; i < sizeof(program); i++)
        memory->DirectWriteToZ80Memory(static_cast<uint16_t>(0x8000 + i), program[i]);
    _emulator->GetContext()->pCore->GetZ80()->pc = 0x8000;

    std::atomic<bool> done{false};
    std::atomic<int> reads{0};
    std::thread reader([&]() {
        while (!done.load())
        {
            const StateNode report = DeviceState::VideoChanges(_emulator->GetContext(), 2);
            EXPECT_NE(report.find("available"), nullptr);
            reads++;
        }
    });
    // Until the reader has read a few times (a starved reader thread may start late), at most 40 frames
    for (int i = 0; i < 40 && (i < 4 || reads.load() < 3); i++)
        _emulator->RunNFrames(1, true);
    done = true;
    reader.join();
    EXPECT_GT(reads.load(), 0);
}
