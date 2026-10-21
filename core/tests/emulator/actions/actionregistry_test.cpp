// The core action registry (docs/inprogress/2026-10-18-control-surfaces/architecture.md §3, §4, §11): one actions
// worker per instance runs the executors in order, deltas merge into the last queued job only, the gate of every
// origin applies inside the registry, and a removed instance's queued jobs answer "Not Found"

#include "pch.h"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "common/uuid.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/builtinactions.h"
#include "emulator/emulator.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"

using namespace actions;

namespace
{
/// A gate the test opens: executors that wait on it block their worker
class Gate
{
public:
    void Wait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _entered++;
        _cv.wait(lock, [this] { return _open; });
    }
    void Open()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _open = true;
        _cv.notify_all();
    }
    int Entered()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _entered;
    }

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _open = false;
    int _entered = 0;
};

ActionValue Press(bool pressed = true)
{
    ActionValue value;
    value.pressed = pressed;
    return value;
}

ActionValue Delta(double delta, double rate = 0)
{
    ActionValue value;
    value.kind = ValueKind::Delta;
    value.delta = delta;
    value.rate = rate;
    return value;
}

ActionOrigin Origin(execution::Origin origin, const std::string& identity = "test")
{
    ActionOrigin out;
    out.origin = origin;
    out.identity = identity;
    out.client = identity;
    return out;
}

/// The results a test collects from its done callbacks (they run on the worker)
struct Results
{
    std::mutex mutex;
    std::vector<ActionResult> all;
    std::atomic<int> count{0};

    ActionRegistry::Done Collect()
    {
        return [this](ActionResult result) {
            {
                std::lock_guard<std::mutex> lock(mutex);
                all.push_back(std::move(result));
            }
            count++;
        };
    }
    ActionResult At(size_t i)
    {
        std::lock_guard<std::mutex> lock(mutex);
        return all.at(i);
    }
};
}  // namespace

class ActionRegistry_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        // Not started: an instance the registry resolves, whose state the gates read; nothing here runs the CPU
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();
        _registry = std::make_unique<ActionRegistry>();
    }

    void TearDown() override
    {
        _handles.clear();
        _registry.reset();
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
    }

    void Add(const char* id, ValueKind kind, Executor run, ActionRules rules = {}, bool held = false)
    {
        _handles.push_back(_registry->Register({id, id, "Test", kind, held, false, {}}, std::move(run), std::move(rules)));
    }

    Emulator* _emulator = nullptr;
    std::string _id;
    std::unique_ptr<ActionRegistry> _registry;
    std::vector<RegistrationHandle> _handles;
};

TEST_F(ActionRegistry_Test, AnUnknownActionOrInstanceOrAWrongKindAnswersAtOnceWithoutAWorker)
{
    Add("t.press", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); });
    Results results;
    _registry->Submit("t.nothing", Press(), _id, Origin(execution::Origin::Gui), results.Collect());
    _registry->Submit("t.press", Press(), "no-such-instance", Origin(execution::Origin::Gui), results.Collect());
    _registry->Submit("t.press", Delta(1), _id, Origin(execution::Origin::Gui), results.Collect());
    ASSERT_EQ(results.count.load(), 3) << "answered on the calling thread";
    EXPECT_EQ(results.At(0).error, "Not Found");
    EXPECT_EQ(results.At(1).error, "Not Found");
    EXPECT_EQ(results.At(2).error, "Bad Request");
    EXPECT_EQ(results.At(2).HttpStatus(), 400);
    EXPECT_EQ(_registry->WorkerCount(), 0u);
}

TEST_F(ActionRegistry_Test, TheReleaseOfAPressThatIsNotHeldDoesNothing)
{
    std::atomic<int> runs{0};
    Add("t.press", ValueKind::Press, [&](const ActionCall&) {
        runs++;
        return ActionResult::Success();
    });
    Results results;
    _registry->Submit("t.press", Press(false), _id, Origin(execution::Origin::HostDevice), results.Collect());
    ASSERT_EQ(results.count.load(), 1);
    EXPECT_TRUE(results.At(0).ok);
    EXPECT_EQ(runs.load(), 0);
    EXPECT_EQ(_registry->WorkerCount(), 0u);
}

TEST_F(ActionRegistry_Test, OneWorkerPerInstanceRunsTheJobsInSubmissionOrder)
{
    std::mutex mutex;
    std::vector<int> order;
    std::vector<std::thread::id> threads;
    Add("t.record", ValueKind::Press, [&](const ActionCall& call) {
        std::lock_guard<std::mutex> lock(mutex);
        order.push_back(static_cast<int>(call.value.params.find("n")->i));
        threads.push_back(std::this_thread::get_id());
        return ActionResult::Success();
    });
    Results results;
    for (int i = 0; i < 20; i++)
    {
        ActionValue value = Press();
        value.params["n"] = i;
        _registry->Submit("t.record", std::move(value), _id, Origin(execution::Origin::Gui), results.Collect());
    }
    ASSERT_TRUE(TestWait::ForAtLeast(results.count, 20));
    std::lock_guard<std::mutex> lock(mutex);
    for (int i = 0; i < 20; i++)
        EXPECT_EQ(order[static_cast<size_t>(i)], i);
    for (const std::thread::id& thread : threads)
    {
        EXPECT_EQ(thread, threads.front()) << "one worker";
        EXPECT_NE(thread, std::this_thread::get_id()) << "never the caller's thread";
    }
    EXPECT_EQ(_registry->WorkerCount(), 1u);
}

TEST_F(ActionRegistry_Test, DeltasMergeIntoTheLastQueuedJobOnlyForTheSameCaller)
{
    Gate gate;
    std::mutex mutex;
    std::vector<double> deltas;
    std::vector<double> rates;
    Add("t.block", ValueKind::Press, [&](const ActionCall&) {
        gate.Wait();
        return ActionResult::Success();
    });
    Add("t.press", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); });
    Add("t.jog", ValueKind::Delta, [&](const ActionCall& call) {
        std::lock_guard<std::mutex> lock(mutex);
        deltas.push_back(call.value.delta);
        rates.push_back(call.value.rate);
        StateNode body = StateNode::Object();
        body["delta"] = call.value.delta;
        return ActionResult::Success(std::move(body));
    });
    Results results;
    const ActionOrigin dial = Origin(execution::Origin::HostDevice, "dial");
    _registry->Submit("t.block", Press(), _id, dial, results.Collect());
    ASSERT_TRUE(TestWait::For([&] { return gate.Entered() == 1; })) << "the worker holds the blocking job";
    _registry->Submit("t.jog", Delta(1, 4), _id, dial, results.Collect());
    _registry->Submit("t.jog", Delta(2, 40), _id, dial, results.Collect());
    _registry->Submit("t.jog", Delta(-0.5, 10), _id, dial, results.Collect());
    _registry->Submit("t.jog", Delta(5), _id, Origin(execution::Origin::HostDevice, "other-dial"), results.Collect());
    _registry->Submit("t.press", Press(), _id, dial, results.Collect());  // a press between keeps them apart
    _registry->Submit("t.jog", Delta(7), _id, dial, results.Collect());
    gate.Open();
    ASSERT_TRUE(TestWait::ForAtLeast(results.count, 7)) << "every caller is answered, the merged ones too";
    std::lock_guard<std::mutex> lock(mutex);
    ASSERT_EQ(deltas.size(), 3u);
    EXPECT_DOUBLE_EQ(deltas[0], 2.5);
    EXPECT_DOUBLE_EQ(rates[0], 40) << "the merged job keeps the higher rate";
    EXPECT_DOUBLE_EQ(deltas[1], 5);
    EXPECT_DOUBLE_EQ(deltas[2], 7);
    int merged = 0;
    for (size_t i = 0; i < 7; i++)
    {
        const ActionResult result = results.At(i);  // a copy: the pointer below points into it
        const StateNode* delta = result.body.find("delta");
        if (delta != nullptr && delta->kind == StateNode::Kind::Double && delta->d == 2.5)
            merged++;
    }
    EXPECT_EQ(merged, 3) << "the three merged calls get the merged job's result";
}

TEST_F(ActionRegistry_Test, AnActionThatNeedsAPauseIsRefusedNotNowWhileTheMachineIsNotPaused)
{
    std::atomic<int> runs{0};
    ActionRules rules;
    rules.needsPause = true;
    rules.scope = ActionRules::Fixed(automation_gate::ScopeDebug);
    Add("t.step", ValueKind::Press, [&](const ActionCall&) {
        runs++;
        return ActionResult::Success();
    }, rules);
    ASSERT_FALSE(_emulator->IsPaused());
    const ActionResult result = _registry->Run("t.step", Press(), _id, Origin(execution::Origin::Gui));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.error, "not_now");
    EXPECT_EQ(result.message, "needs a pause");
    EXPECT_EQ(runs.load(), 0);
    EXPECT_EQ(_registry->Unavailable("t.step", _id, Origin(execution::Origin::Gui), Press()), "needs a pause");
}

TEST_F(ActionRegistry_Test, TheGuiAndHostDevicesAreRefusedAChangeWhileAnotherSurfaceHoldsControl)
{
    Add("t.change", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); },
        ActionRules{ActionRules::Fixed(automation_gate::ScopeRun), false, nullptr, "not_now"});
    Add("t.read", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); });
    EmulatorContext* context = _emulator->GetContext();
    const unreal::UUID gdb = unreal::UUID::Generate();
    ASSERT_TRUE(context->TakeRunControl(gdb, "gdb"));

    for (execution::Origin origin : {execution::Origin::Gui, execution::Origin::HostDevice})
    {
        const ActionResult refused = _registry->Run("t.change", Press(), _id, Origin(origin));
        EXPECT_EQ(refused.error, "Run-control held") << execution::OriginName(origin);
        EXPECT_EQ(refused.HttpStatus(), 409);
        EXPECT_TRUE(_registry->Run("t.read", Press(), _id, Origin(origin)).ok) << "changing nothing passes";
    }
    context->ReleaseRunControl(gdb);
    EXPECT_TRUE(_registry->Run("t.change", Press(), _id, Origin(execution::Origin::HostDevice)).ok);
    EXPECT_FALSE(context->IsRunControlClaimed()) << "the user's hands open no session";
}

TEST_F(ActionRegistry_Test, AnAutomationClientGetsTheImplicitSessionAndAnotherIsRefused)
{
    std::mutex mutex;
    std::vector<execution::Requester> requesters;
    Add("t.change", ValueKind::Press, [&](const ActionCall& call) {
        std::lock_guard<std::mutex> lock(mutex);
        requesters.push_back(call.requester);
        return ActionResult::Success();
    }, ActionRules{ActionRules::Fixed(automation_gate::ScopeRun), false, nullptr, "not_now"});
    EmulatorContext* context = _emulator->GetContext();
    ActionOrigin client = Origin(execution::Origin::WebApi, "webapi|127.0.0.1|curl/8");
    client.lease = std::chrono::seconds(15);

    // The list predicts it: free now, and opens nothing
    for (const ActionStatus& status : _registry->Statuses(_id, client))
    {
        if (status.info.id == "t.change")
        {
            EXPECT_TRUE(status.available) << status.reason;
        }
    }
    EXPECT_FALSE(context->IsRunControlClaimed());

    const ActionResult first = _registry->Run("t.change", Press(), _id, client);
    ASSERT_TRUE(first.ok) << first.message;
    EXPECT_TRUE(first.implicitOpened);
    ASSERT_FALSE(first.implicitSession.empty());
    EXPECT_TRUE(context->HasRunControl(unreal::UUID(first.implicitSession)));
    const ActionResult again = _registry->Run("t.change", Press(), _id, client);
    EXPECT_TRUE(again.ok);
    EXPECT_FALSE(again.implicitOpened) << "the same client goes on in its session";
    EXPECT_EQ(again.implicitSession, first.implicitSession);
    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_EQ(requesters.size(), 2u);
        EXPECT_EQ(requesters[0].origin, execution::Origin::WebApi);
        EXPECT_EQ(requesters[0].client, first.implicitSession) << "the arbiter sees the session's holder";
    }

    ActionOrigin other = Origin(execution::Origin::Mcp, "mcp|127.0.0.1|MCP agent");
    const ActionResult refused = _registry->Run("t.change", Press(), _id, other);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(refused.error, "held_by_other");
    // A client carrying the session (X-Unreal-Session) runs as its holder
    other.session = first.implicitSession;
    EXPECT_TRUE(_registry->Run("t.change", Press(), _id, other).ok);
    context->ReleaseRunControl(unreal::UUID(first.implicitSession));
}

TEST_F(ActionRegistry_Test, ACompanionIsCheckedAgainstItsScopes)
{
    companion::CompanionRegistry& companions = companion::CompanionRegistry::Instance();
    const std::string token = companion::CompanionRegistry::GenerateToken();
    ASSERT_EQ(companions.AddFromSpec("actions-test-deck:" + token + ":input"), 1u);
    const std::optional<companion::CompanionInfo> deck = companions.Authenticate(token);
    ASSERT_TRUE(deck.has_value());

    Add("t.run", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); },
        ActionRules{ActionRules::Fixed(automation_gate::ScopeRun), false, nullptr, "not_now"});
    Add("t.input", ValueKind::Press, [](const ActionCall&) { return ActionResult::Success(); },
        ActionRules{ActionRules::Fixed(automation_gate::ScopeInput), false, nullptr, "not_now"});
    const ActionOrigin origin = Origin(execution::Origin::Companion, deck->id);
    const ActionResult forbidden = _registry->Run("t.run", Press(), _id, origin);
    EXPECT_EQ(forbidden.error, "companion_forbidden");
    EXPECT_EQ(forbidden.HttpStatus(), 403);
    ASSERT_NE(forbidden.body.find("required"), nullptr);  // forbidden is a local: the pointer stays valid
    EXPECT_EQ(forbidden.body.find("required")->s, "run");
    EXPECT_TRUE(_registry->Run("t.input", Press(), _id, origin).ok);
    EXPECT_FALSE(_emulator->GetContext()->IsRunControlClaimed()) << "a companion opens no session";
    companions.Revoke(deck->id);
    EXPECT_EQ(_registry->Run("t.input", Press(), _id, origin).error, "companion_unknown");
}

TEST_F(ActionRegistry_Test, DroppingTheInstanceAnswersQueuedJobsNotFoundAndEndsItsWorker)
{
    Gate gate;
    Add("t.block", ValueKind::Press, [&](const ActionCall&) {
        gate.Wait();
        return ActionResult::Success();
    });
    Results results;
    _registry->Submit("t.block", Press(), _id, Origin(execution::Origin::Gui), results.Collect());
    ASSERT_TRUE(TestWait::For([&] { return gate.Entered() == 1; }));
    _registry->Submit("t.block", Press(), _id, Origin(execution::Origin::Gui), results.Collect());
    _registry->Submit("t.block", Press(), _id, Origin(execution::Origin::Gui), results.Collect());
    std::thread dropper([&] { _registry->DropInstance(_id); });
    ASSERT_TRUE(TestWait::ForAtLeast(results.count, 2)) << "the queued ones answer at once";
    EXPECT_EQ(results.At(0).error, "Not Found");
    EXPECT_EQ(results.At(1).error, "Not Found");
    gate.Open();  // the running one finishes
    dropper.join();
    EXPECT_TRUE(TestWait::ForAtLeast(results.count, 3));
    EXPECT_TRUE(results.At(2).ok);
    EXPECT_EQ(_registry->WorkerCount(), 0u);
}

TEST_F(ActionRegistry_Test, AHandleUnregistersOnlyItsOwnRegistration)
{
    RegistrationHandle first = _registry->Register({"t.same", "First", "Test", ValueKind::Press, false, true, {}},
                                                   [](const ActionCall&) { return ActionResult::Success(); });
    RegistrationHandle second = _registry->Register({"t.same", "Second", "Test", ValueKind::Press, false, true, {}},
                                                    [](const ActionCall&) { return ActionResult::Success(); });
    first.Reset();  // replaced already: the newer registration stays
    ASSERT_TRUE(_registry->Find("t.same").has_value());
    EXPECT_EQ(_registry->Find("t.same")->label, "Second");
    second.Reset();
    EXPECT_FALSE(_registry->Find("t.same").has_value());
    EXPECT_TRUE(_registry->List().empty());
}

TEST(ActionRegistryValues_Test, TextValuesParseByKind)
{
    ActionValue value;
    std::string error;
    ASSERT_TRUE(ValueFromText(ValueKind::Press, "", value, error));
    EXPECT_TRUE(value.pressed);
    ASSERT_TRUE(ValueFromText(ValueKind::Press, "release", value, error));
    EXPECT_FALSE(value.pressed);
    EXPECT_FALSE(ValueFromText(ValueKind::Press, "maybe", value, error));
    ASSERT_TRUE(ValueFromText(ValueKind::Delta, "-10", value, error));
    EXPECT_DOUBLE_EQ(value.delta, -10);
    EXPECT_FALSE(ValueFromText(ValueKind::Delta, "ten", value, error));
    ASSERT_TRUE(ValueFromText(ValueKind::Level, "2", value, error));
    EXPECT_DOUBLE_EQ(value.level, 1) << "clamped to 0..1";
    ASSERT_TRUE(ValueFromText(ValueKind::Text, "ttd.timecode", value, error));
    EXPECT_EQ(value.text, "ttd.timecode");
    ValueKind kind = ValueKind::Press;
    EXPECT_TRUE(ValueKindFromName("delta", kind));
    EXPECT_EQ(kind, ValueKind::Delta);
    EXPECT_FALSE(ValueKindFromName("knob", kind));
}

/// The core's catalogue: the actions that were the Qt client's, present headless, with their kinds and rules
TEST(ActionRegistryBuiltins_Test, TheCoreRegistersTheMovedActions)
{
    ActionRegistry& registry = ActionRegistry::Instance();
    for (const char* id : {"run.pause_toggle", "run.step_in", "run.step_over", "run.step_out", "run.frame_step",
                           "run.wait_int", "run.to_scanline", "run.n_scanlines", "run.turbo_hold", "ttd.record_toggle",
                           "ttd.step_back_frame", "ttd.step_forward_frame", "ttd.step_back_instruction",
                           "ttd.step_forward_instruction", "ttd.reverse_step", "ttd.reverse_continue",
                           "ttd.bookmark_add", "ttd.resume_live", "ttd.jog_frames", "ttd.jog_instructions",
                           "mach.reset", "mach.nmi", "mach.turbo_toggle"})
    {
        const std::optional<ActionInfo> info = registry.Find(id);
        ASSERT_TRUE(info.has_value()) << id;
        EXPECT_FALSE(info->gui) << id;
    }
    EXPECT_EQ(registry.Find("ttd.jog_frames")->kind, ValueKind::Delta);
    EXPECT_TRUE(registry.Find("mach.reset")->held);
    EXPECT_TRUE(registry.Find("run.turbo_hold")->held);
    EXPECT_FALSE(registry.Find("dbg.open").has_value()) << "a GUI action: only the Qt client registers it";
}

/// Builtins over a paused machine: the step goes through RunControl on the actions worker, and the reset fires on a
/// release after a 1 s hold only. Boot-bound (a started machine paused): about 30 ms
TEST(ActionRegistryBuiltins_Test, StepInStepsAPausedMachineAndAShortResetDoesNothing)
{
    Emulator* emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(emulator, nullptr);
    emulator->EnableTurboMode();
    emulator->StartAsync();
    ASSERT_TRUE(TestWait::For([&] { return emulator->GetContext()->emulatorState.frame_counter > 1; }));
    ActionRegistry& registry = ActionRegistry::Instance();
    const std::string id = emulator->GetId();
    const ActionOrigin hands = Origin(execution::Origin::HostDevice, "pad-1");

    const ActionResult paused = registry.Run("run.pause_toggle", Press(), id, hands);
    ASSERT_TRUE(paused.ok) << paused.message;
    ASSERT_TRUE(TestWait::For([&] { return emulator->IsPaused(); }));
    const ActionResult step = registry.Run("run.step_in", Press(), id, hands);
    ASSERT_TRUE(step.ok) << step.message;
    ASSERT_NE(step.body.find("pc"), nullptr);
    ASSERT_NE(step.body.find("executed"), nullptr);
    EXPECT_EQ(step.body.find("state")->s, "paused");

    EXPECT_TRUE(registry.Run("mach.reset", Press(true), id, hands).ok);
    const ActionResult early = registry.Run("mach.reset", Press(false), id, hands);
    EXPECT_EQ(early.error, "not_now") << "released before 1 s: no reset";

    EmulatorTestHelper::CleanupEmulator(emulator);
    EXPECT_EQ(registry.Run("run.step_in", Press(), id, hands).error, "Not Found") << "the instance is gone";
}
