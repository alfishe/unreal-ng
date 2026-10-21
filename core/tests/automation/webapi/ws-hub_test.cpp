// WsHub and SurfaceHub: the topics of /api/v1/ws and its `surface` topic (control surfaces protocol.md, Stream Deck
// integration protocol.md §0), driven without a network: each "connection" is a send and a close function, the hub's
// loop runs tasks at once, its clock is the test's and its timer is recorded. The conformance of the protocol at the
// unit level: auth and its errors, join, actions with rid / without, coalescing, ctx (a setter and the default
// following the history), readouts and topics (full, then changed only, at the subscriber's rate), the timer armed
// only while somebody subscribes, follow and pin, revoked, bye

#include "pch.h"

#include "ws-hub.h"

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/contextstate.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"

#include <json/json.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace actions;

namespace
{
constexpr const char* kToken = "unc_surface-deck-0123456789";
constexpr const char* kNarrowToken = "unc_surface-narrow-0123456789";

Json::Value Parse(const std::string& text)
{
    Json::Value value;
    Json::CharReaderBuilder reader;
    std::string errors;
    std::istringstream stream(text);
    Json::parseFromStream(reader, stream, &value, &errors);
    return value;
}

/// What one client received, parsed, and whether it was closed
class Inbox
{
public:
    std::function<void(const std::string&)> Sender()
    {
        return [this](const std::string& text) {
            Json::Value value = Parse(text);
            std::lock_guard<std::mutex> lock(_mutex);
            _messages.push_back(value);
            _count++;
        };
    }
    std::function<void()> Closer()
    {
        return [this] { _closed = true; };
    }
    std::vector<Json::Value> Messages()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _messages;
    }
    /// The messages of op `op`
    std::vector<Json::Value> Ops(const std::string& op)
    {
        std::vector<Json::Value> out;
        for (const Json::Value& message : Messages())
            if (message["op"].asString() == op)
                out.push_back(message);
        return out;
    }
    Json::Value Last()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        return _messages.empty() ? Json::Value() : _messages.back();
    }
    void Clear()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _messages.clear();
        _count = 0;
    }
    std::atomic<int>& Count() { return _count; }
    bool Closed() const { return _closed; }

private:
    std::mutex _mutex;
    std::vector<Json::Value> _messages;
    std::atomic<int> _count{0};
    std::atomic<bool> _closed{false};
};

/// A gate the test opens: an executor that waits on it holds its worker
class Gate
{
public:
    void Wait()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        _entered++;
        _cv.notify_all();
        _cv.wait(lock, [this] { return _open; });
    }
    bool WaitEntered()
    {
        std::unique_lock<std::mutex> lock(_mutex);
        return _cv.wait_for(lock, std::chrono::seconds(5), [this] { return _entered > 0; });
    }
    void Open()
    {
        std::lock_guard<std::mutex> lock(_mutex);
        _open = true;
        _cv.notify_all();
    }

private:
    std::mutex _mutex;
    std::condition_variable _cv;
    bool _open = false;
    int _entered = 0;
};
}  // namespace

class WsHub_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Companions().ResetForTesting();
        ASSERT_EQ(Companions().AddFromSpec(std::string("deck:") + kToken + ":all"), 1u);
        ASSERT_EQ(Companions().AddFromSpec(std::string("narrow:") + kNarrowToken + ":run"), 1u);
        // Not started: an instance the hub binds and reads; nothing here runs the CPU
        _emulator = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
        ASSERT_NE(_emulator, nullptr);
        _id = _emulator->GetId();

        _registry = std::make_unique<ActionRegistry>();
        // test.echo: a delta that answers what it got (the value, the rate, the params)
        Add({"test.echo", "Echo", "Test", ValueKind::Delta, false, false, "unit?, min?"},
            [this](const ActionCall& call) {
                if (_slow.load())
                {
                    _slow = false;
                    _gate.Wait();
                }
                StateNode body = StateNode::Object();
                body["delta"] = StateNode(call.value.delta);
                body["rate"] = StateNode(call.value.rate);
                body["params"] = call.value.params;
                return ActionResult::Success(std::move(body), "echoed");
            });
        // test.rewind: needs the rewind scope (the narrow companion has run only)
        ActionRules rewind;
        rewind.scope = ActionRules::Fixed(automation_gate::ScopeRewind);
        Add({"test.rewind", "Rewind", "Test", ValueKind::Press, false, false, {}},
            [](const ActionCall&) { return ActionResult::Success(); }, std::move(rewind));

        _hub = std::make_unique<WsHub>();
        _hub->SetRegistry(_registry.get());
        _hub->SetClock([this] { return _now; });
        WsHub::Timer timer;
        timer.arm = [this](double seconds, WsHub::Task) -> uint64_t {
            _armedHz = 1.0 / seconds;
            return ++_timerIds;
        };
        timer.disarm = [this](uint64_t) { _armedHz = 0; };
        _hub->SetLoop([](WsHub::Task task) { task(); }, std::move(timer));
    }

    void TearDown() override
    {
        _gate.Open();
        _hub.reset();  // waits for the results on their way
        _handles.clear();
        _registry.reset();
        actions::ContextState::Instance().Drop(_id);
        if (_emulator)
            EmulatorTestHelper::CleanupEmulator(_emulator);
        Companions().ResetForTesting();
    }

    static companion::CompanionRegistry& Companions() { return companion::CompanionRegistry::Instance(); }

    void Add(ActionInfo info, Executor run, ActionRules rules = {})
    {
        _handles.push_back(_registry->Register(std::move(info), std::move(run), std::move(rules)));
    }

    void Connect(Inbox& inbox, const std::string& token = kToken, const std::string& source = "")
    {
        WsHub::Peer peer;
        peer.token = token;
        peer.source = source;
        peer.host = "127.0.0.1";
        _hub->Open(&inbox, peer, inbox.Sender(), inbox.Closer());
    }

    bool Send(Inbox& inbox, const std::string& json) { return _hub->HandleMessage(&inbox, Parse(json)); }

    /// Connect and join pinned to the test's instance; returns the welcome
    Json::Value Join(Inbox& inbox, const std::string& token = kToken)
    {
        Connect(inbox, token);
        Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"elgato.streamdeck.plus","target":{"instance":")" +
                        _id + R"("}})");
        const Json::Value welcome = inbox.Last();
        inbox.Clear();
        return welcome;
    }

    void Advance(int ms) { _now += std::chrono::milliseconds(ms); }

    Emulator* _emulator = nullptr;
    std::string _id;
    std::unique_ptr<ActionRegistry> _registry;
    std::vector<RegistrationHandle> _handles;
    std::unique_ptr<WsHub> _hub;
    std::chrono::steady_clock::time_point _now = std::chrono::steady_clock::now();
    double _armedHz = 0;
    uint64_t _timerIds = 0;
    std::atomic<bool> _slow{false};
    Gate _gate;
};

/// region <Auth and join>

TEST_F(WsHub_Test, JoinNeedsACompanionTokenAndAgentsNeverJoin)
{
    Inbox anonymous;
    Connect(anonymous, "");
    EXPECT_TRUE(Send(anonymous, R"({"op":"join","v":1,"name":"deck","model":"x"})"));
    EXPECT_EQ(anonymous.Last()["op"].asString(), "error");
    EXPECT_EQ(anonymous.Last()["t"].asString(), "surface");
    EXPECT_EQ(anonymous.Last()["error"].asString(), "companion_required");
    EXPECT_FALSE(anonymous.Closed()) << "the session goes on: it may still authenticate";

    // {"op":"auth"} (ApiWebSocket stores the token and tells the hub) then join
    _hub->SetToken(&anonymous, kToken);
    Send(anonymous, R"({"op":"join","v":1,"name":"deck","model":"x"})");
    EXPECT_EQ(anonymous.Last()["op"].asString(), "welcome");

    Inbox agent;
    Connect(agent, kToken, "mcp");
    Send(agent, R"({"op":"join","v":1,"name":"deck","model":"x"})");
    EXPECT_EQ(agent.Last()["error"].asString(), "companion_not_for_agents");

    Inbox stranger;
    Connect(stranger, "unc_nobody-knows-me");
    Send(stranger, R"({"op":"join","v":1,"name":"deck","model":"x"})");
    EXPECT_EQ(stranger.Last()["error"].asString(), "companion_unknown");
    EXPECT_TRUE(stranger.Closed()) << "an unknown token ends the connection, as on the events socket";
}

TEST_F(WsHub_Test, AWelcomeNamesTheVersionSessionInstanceContextScopesAndTarget)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"join","v":3,"name":"deck","model":"elgato.streamdeck.plus","serial":"A1",)"
                R"("target":{"instance":")" + _id + R"("}})");
    const Json::Value welcome = inbox.Last();
    ASSERT_EQ(welcome["op"].asString(), "welcome") << welcome.toStyledString();
    EXPECT_EQ(welcome["t"].asString(), "surface");
    EXPECT_EQ(welcome["v"].asInt(), 1) << "the version chosen: the highest both speak";
    EXPECT_EQ(welcome["session"].asString().substr(0, 1), "s");
    EXPECT_EQ(welcome["instance"].asString(), _id);
    EXPECT_EQ(welcome["context"].asString(), "play") << "no history: the default";
    std::vector<std::string> scopes;
    for (const Json::Value& scope : welcome["scopes"])
        scopes.push_back(scope.asString());
    EXPECT_NE(std::find(scopes.begin(), scopes.end(), "rewind"), scopes.end());
    EXPECT_EQ(welcome["target"]["instance"].asString(), _id);
    EXPECT_EQ(welcome["target"]["mode"].asString(), "instance");
    EXPECT_EQ(welcome["target"]["model"].asString(), "PENTAGON");
    EXPECT_TRUE(welcome["target"]["view"].isNull()) << "views: SD4";
    EXPECT_EQ(welcome["readouts"].size(), WsHub::ReadoutNames().size());

    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "one surface per connection";
}

TEST_F(WsHub_Test, AJoinWithWrongFieldsOrATargetItCannotBindIsRefused)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"join","v":1,"model":"x"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "no name";
    Send(inbox, R"({"op":"join","name":"deck","model":"x"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "no v";
    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","target":{"view":"v1"}})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "not_now") << "views come with TargetState (SD4)";
    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","target":{"instance":"no-such-instance"}})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "Not Found");
    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","target":"elsewhere"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request");
    EXPECT_FALSE(inbox.Closed());
    EXPECT_TRUE(inbox.Ops("welcome").empty());
}

TEST_F(WsHub_Test, SurfaceOpsBeforeJoinAreBadRequestsAndUnknownOpsAreNotTheHubs)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"action","id":"test.echo","value":1,"rid":5})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request");
    EXPECT_EQ(inbox.Last()["rid"].asInt(), 5);
    Send(inbox, R"({"op":"subscribe","readouts":["run.state"]})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request");
    EXPECT_FALSE(Send(inbox, R"({"op":"frobnicate"})")) << "ApiWebSocket answers it";
    EXPECT_FALSE(Send(inbox, R"({"op":"call"})")) << "calls stay in ApiWebSocket";
}

/// endregion </Auth and join>

/// region <Actions>

TEST_F(WsHub_Test, AnActionWithARidIsAnsweredWithTheResultAndTheValueKeepsItsParams)
{
    Inbox inbox;
    Join(inbox);
    Send(inbox, R"({"op":"action","id":"test.echo","value":{"d":-3,"rate":18.5,"unit":"frames","min":1},"rid":41})");
    ASSERT_TRUE(TestWait::ForAtLeast(inbox.Count(), 1));
    const Json::Value result = inbox.Last();
    ASSERT_EQ(result["op"].asString(), "result") << result.toStyledString();
    EXPECT_EQ(result["t"].asString(), "surface");
    EXPECT_EQ(result["rid"].asInt(), 41);
    EXPECT_TRUE(result["ok"].asBool());
    EXPECT_EQ(result["body"]["delta"].asDouble(), -3);
    EXPECT_EQ(result["body"]["rate"].asDouble(), 18.5);
    EXPECT_EQ(result["body"]["params"]["unit"].asString(), "frames");
    EXPECT_EQ(result["body"]["params"]["min"].asInt(), 1);

    // The REST spelling and a bare number are values too; a success without a rid is not answered
    inbox.Clear();
    Send(inbox, R"({"op":"action","id":"test.echo","value":{"delta":2,"params":{"unit":"seconds"}},"rid":"r2"})");
    Send(inbox, R"({"op":"action","id":"test.echo","value":5})");
    Send(inbox, R"({"op":"action","id":"test.echo","value":1,"rid":3})");
    ASSERT_TRUE(TestWait::ForAtLeast(inbox.Count(), 2));
    const std::vector<Json::Value> results = inbox.Ops("result");
    ASSERT_EQ(results.size(), 2u);
    EXPECT_EQ(results[0]["rid"].asString(), "r2");
    EXPECT_EQ(results[0]["body"]["params"]["unit"].asString(), "seconds");
    EXPECT_EQ(results[1]["rid"].asInt(), 3) << "results in submission order: one worker per instance";
}

TEST_F(WsHub_Test, ARefusalIsTheResultsErrorOrAnErrorWithoutARid)
{
    Inbox inbox;
    Join(inbox);
    Send(inbox, R"({"op":"action","id":"no.such","rid":1})");
    ASSERT_TRUE(TestWait::ForAtLeast(inbox.Count(), 1));
    EXPECT_FALSE(inbox.Last()["ok"].asBool());
    EXPECT_EQ(inbox.Last()["error"].asString(), "Not Found");
    inbox.Clear();
    Send(inbox, R"({"op":"action","id":"no.such"})");
    ASSERT_TRUE(TestWait::ForAtLeast(inbox.Count(), 1));
    EXPECT_EQ(inbox.Last()["op"].asString(), "error");
    EXPECT_EQ(inbox.Last()["error"].asString(), "Not Found");
    inbox.Clear();
    Send(inbox, R"({"op":"action","id":"test.echo","value":"fast","rid":2})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "a delta takes a number";
    EXPECT_EQ(inbox.Last()["rid"].asInt(), 2);

    // The scope the companion was not granted: the registry's companion gate, as on REST
    Inbox narrow;
    Join(narrow, kNarrowToken);
    Send(narrow, R"({"op":"action","id":"test.rewind","value":{"p":true},"rid":9})");
    ASSERT_TRUE(TestWait::ForAtLeast(narrow.Count(), 1));
    EXPECT_EQ(narrow.Last()["op"].asString(), "result");
    EXPECT_EQ(narrow.Last()["error"].asString(), "companion_forbidden");
    EXPECT_EQ(narrow.Last()["body"]["required"].asString(), "rewind");
}

TEST_F(WsHub_Test, DeltasQueuedBehindARunningOneMergeAndEveryRidGetsTheMergedResult)
{
    Inbox inbox;
    Join(inbox);
    _slow = true;
    Send(inbox, R"({"op":"action","id":"test.echo","value":{"d":1},"rid":1})");
    ASSERT_TRUE(_gate.WaitEntered());
    Send(inbox, R"({"op":"action","id":"test.echo","value":{"d":2},"rid":2})");
    Send(inbox, R"({"op":"action","id":"test.echo","value":{"d":3},"rid":3})");
    _gate.Open();
    ASSERT_TRUE(TestWait::ForAtLeast(inbox.Count(), 3));
    const std::vector<Json::Value> results = inbox.Ops("result");
    ASSERT_EQ(results.size(), 3u);
    EXPECT_EQ(results[0]["body"]["delta"].asDouble(), 1);
    EXPECT_EQ(results[1]["body"]["delta"].asDouble(), 5) << "2 and 3 merged";
    EXPECT_EQ(results[2]["body"]["delta"].asDouble(), 5);
    EXPECT_EQ(results[1]["rid"].asInt() + results[2]["rid"].asInt(), 5);
}

/// endregion </Actions>

/// region <ctx>

TEST_F(WsHub_Test, TheBoundInstancesContextIsPushedBySetterAndByTheDefaultFollowingTheHistory)
{
    _hub->Start();
    Inbox inbox;
    Join(inbox);
    std::string error;
    ASSERT_TRUE(ContextState::Instance().Set(_id, _emulator, "ttd.timecode", "gui", error)) << error;
    ASSERT_TRUE(TestWait::For([&] { return !inbox.Ops("ctx").empty(); }));
    Json::Value ctx = inbox.Ops("ctx").back();
    EXPECT_EQ(ctx["t"].asString(), "surface");
    EXPECT_EQ(ctx["instance"].asString(), _id);
    EXPECT_EQ(ctx["context"].asString(), "ttd.timecode");
    EXPECT_EQ(ctx["by"].asString(), "gui");

    // Back to the default, then a history appears: the default flips to ttd and the surface is told (Q-7)
    ASSERT_TRUE(ContextState::Instance().Set(_id, _emulator, "", "gui", error));
    ASSERT_TRUE(TestWait::For([&] { return inbox.Ops("ctx").size() >= 2; }));
    EXPECT_EQ(inbox.Ops("ctx")[1]["context"].asString(), "play");
    ContextState::Instance().HistoryChanged(_id, true);
    ASSERT_TRUE(TestWait::For([&] { return inbox.Ops("ctx").size() >= 3; }));
    ctx = inbox.Ops("ctx")[2];
    EXPECT_EQ(ctx["context"].asString(), "ttd");
    EXPECT_EQ(ctx["by"].asString(), "default");
    _hub->Stop();
}

/// endregion </ctx>

/// region <Readouts and topics>

TEST_F(WsHub_Test, ReadoutsComeInFullOnSubscribeThenOnlyWhatChangedAtTheSubscribersRate)
{
    Inbox inbox;
    Join(inbox);
    EXPECT_EQ(_hub->TimerHz(), 0) << "nobody subscribes: no timer";
    Send(inbox, R"({"op":"subscribe","readouts":["run.state","ttd.*","bogus"],"max_hz":50})");
    EXPECT_EQ(_armedHz, 50);
    const std::vector<Json::Value> errors = inbox.Ops("error");
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_EQ(errors[0]["error"].asString(), "Not Found");
    const std::vector<Json::Value> readouts = inbox.Ops("readout");
    ASSERT_EQ(readouts.size(), 1u) << "the full values at once";
    const Json::Value& values = readouts[0]["values"];
    EXPECT_EQ(readouts[0]["t"].asString(), "surface");
    EXPECT_EQ(readouts[0]["instance"].asString(), _id);
    EXPECT_EQ(values["run.state"]["state"].asString(), "stopped");
    EXPECT_FALSE(values["run.state"]["turbo"].asBool());
    EXPECT_TRUE(values["ttd.position"].isMember("frame"));
    EXPECT_FALSE(values["ttd.position"]["history"].asBool());
    EXPECT_NEAR(values["ttd.position"]["fps"].asDouble(), 48.83, 0.01);

    // Nothing changed: nothing sent
    inbox.Clear();
    Advance(20);
    _hub->Tick();
    EXPECT_TRUE(inbox.Messages().empty());

    // Turbo: run.state changed, alone - but not before the subscriber's interval (20 ms at 50 Hz)
    _emulator->EnableTurboMode();
    Advance(20);
    _hub->Tick();
    ASSERT_EQ(inbox.Ops("readout").size(), 1u);
    EXPECT_TRUE(inbox.Last()["values"]["run.state"]["turbo"].asBool());
    EXPECT_FALSE(inbox.Last()["values"].isMember("ttd.position"));
    _emulator->DisableTurboMode();
    Advance(5);
    _hub->Tick();
    EXPECT_EQ(inbox.Ops("readout").size(), 1u) << "within 20 ms of the last: held back";
    Advance(15);
    _hub->Tick();
    ASSERT_EQ(inbox.Ops("readout").size(), 2u);
    EXPECT_FALSE(inbox.Last()["values"]["run.state"]["turbo"].asBool());

    Send(inbox, R"({"op":"unsubscribe","readouts":["*"]})");
    EXPECT_EQ(_hub->TimerHz(), 0) << "the last subscriber left: the timer is disarmed";
    EXPECT_EQ(_armedHz, 0);
}

TEST_F(WsHub_Test, TheTimerRunsAtTheHighestRateAnyoneAskedFor)
{
    Inbox slow;
    Join(slow);
    Send(slow, R"({"op":"subscribe","readouts":["run.state"],"max_hz":10})");
    EXPECT_EQ(_hub->TimerHz(), 10);
    Inbox fast;
    Connect(fast);
    Send(fast, R"({"op":"subscribe","topics":["ttd.position"],"max_hz":500})");
    EXPECT_EQ(_hub->TimerHz(), 50) << "max_hz is capped at 50";
    _hub->Remove(&fast);
    EXPECT_EQ(_hub->TimerHz(), 10);

    // At 10 Hz a change waits for the subscriber's 100 ms
    slow.Clear();
    _emulator->EnableTurboMode();
    Advance(20);
    _hub->Tick();
    EXPECT_TRUE(slow.Ops("readout").empty());
    Advance(80);
    _hub->Tick();
    EXPECT_EQ(slow.Ops("readout").size(), 1u);
    _emulator->DisableTurboMode();
    _hub->Remove(&slow);
    EXPECT_EQ(_hub->TimerHz(), 0);
}

TEST_F(WsHub_Test, TopicsNeedATokenAndPushEachInstancesStateOnceThenChanges)
{
    Inbox anonymous;
    Connect(anonymous, "");
    Send(anonymous, R"({"op":"subscribe","topics":["run.state"]})");
    EXPECT_EQ(anonymous.Last()["error"].asString(), "companion_required");
    EXPECT_EQ(_hub->TimerHz(), 0);

    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["run.state","surfaces.*","nope","surface"],"max_hz":20})");
    const std::vector<Json::Value> errors = inbox.Ops("error");
    ASSERT_EQ(errors.size(), 2u);
    EXPECT_EQ(errors[0]["t"].asString(), "nope");
    EXPECT_EQ(errors[0]["error"].asString(), "Not Found");
    EXPECT_EQ(errors[1]["t"].asString(), "surface");
    EXPECT_EQ(errors[1]["error"].asString(), "bad_request") << "join opens the surface topic";
    const std::vector<Json::Value> acks = inbox.Ops("subscribed");
    ASSERT_EQ(acks.size(), 1u);
    EXPECT_EQ(acks[0]["topics"].size(), 2u);
    EXPECT_EQ(acks[0]["max_hz"].asDouble(), 20);
    EXPECT_EQ(_hub->TimerHz(), 20);

    auto states = [&](const std::string& topic) {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Ops("state"))
            if (message["t"].asString() == topic && message["instance"].asString() == _id)
                out.push_back(message);
        return out;
    };
    ASSERT_EQ(states("run.state").size(), 1u);
    EXPECT_EQ(states("run.state")[0]["value"]["state"].asString(), "stopped");
    ASSERT_EQ(states("surfaces.context").size(), 1u);
    EXPECT_EQ(states("surfaces.context")[0]["value"]["context"].asString(), "play");

    std::string error;
    ASSERT_TRUE(ContextState::Instance().Set(_id, _emulator, "debug.disasm", "gui", error));
    Advance(50);
    _hub->Tick();
    ASSERT_EQ(states("surfaces.context").size(), 2u);
    EXPECT_EQ(states("surfaces.context")[1]["value"]["context"].asString(), "debug.disasm");
    EXPECT_EQ(states("surfaces.context")[1]["value"]["by"].asString(), "gui");
    EXPECT_EQ(states("run.state").size(), 1u) << "unchanged: not sent again";

    Send(inbox, R"({"op":"unsubscribe","topics":["run.state","surfaces.context"]})");
    EXPECT_EQ(inbox.Last()["op"].asString(), "unsubscribed");
    EXPECT_EQ(inbox.Last()["topics"].size(), 0u);
    EXPECT_EQ(_hub->TimerHz(), 0);
}

/// endregion </Readouts and topics>

/// region <Binding, revocation, bye>

TEST_F(WsHub_Test, AFollowingSurfaceFollowsTheSelectionWithTargetCtxAndAFullReadout)
{
    Emulator* second = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(second, nullptr);
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_TRUE(manager->SetSelectedEmulatorId(_id));

    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","target":"follow"})");
    ASSERT_EQ(inbox.Last()["op"].asString(), "welcome");
    EXPECT_EQ(inbox.Last()["instance"].asString(), _id);
    EXPECT_EQ(inbox.Last()["target"]["mode"].asString(), "follow");
    Send(inbox, R"({"op":"subscribe","readouts":["run.state"],"max_hz":50})");
    inbox.Clear();

    ASSERT_TRUE(manager->SetSelectedEmulatorId(second->GetId()));
    _hub->Refresh();
    const std::vector<Json::Value> messages = inbox.Messages();
    ASSERT_GE(messages.size(), 3u);
    EXPECT_EQ(messages[0]["op"].asString(), "target");
    EXPECT_EQ(messages[0]["instance"].asString(), second->GetId());
    EXPECT_EQ(messages[0]["mode"].asString(), "follow");
    EXPECT_EQ(messages[1]["op"].asString(), "ctx");
    EXPECT_EQ(messages[1]["instance"].asString(), second->GetId());
    EXPECT_EQ(messages[1]["by"].asString(), "default");
    EXPECT_EQ(messages[2]["op"].asString(), "readout");
    EXPECT_EQ(messages[2]["instance"].asString(), second->GetId());
    EXPECT_TRUE(messages[2]["values"].isMember("run.state")) << "the new instance's values in full";

    // A pinned surface keeps its instance; when it goes, its target is null
    Inbox pinned;
    Join(pinned);
    manager->SetSelectedEmulatorId("");
    const std::string first = _id;
    EmulatorTestHelper::CleanupEmulator(_emulator);
    _emulator = nullptr;
    _hub->Refresh();
    const std::vector<Json::Value> targets = pinned.Ops("target");
    ASSERT_EQ(targets.size(), 1u);
    EXPECT_TRUE(targets[0]["instance"].isNull());
    EXPECT_EQ(targets[0]["mode"].asString(), "instance");
    // ...and the follower took the only one left
    EXPECT_EQ(inbox.Ops("target").back()["instance"].asString(), second->GetId());
    EmulatorTestHelper::CleanupEmulator(second);
    actions::ContextState::Instance().Drop(first);
}

TEST_F(WsHub_Test, ARevokedCompanionIsToldAndClosedAndByeCloses)
{
    Inbox surface;
    Join(surface);
    Inbox topics;
    Connect(topics, kNarrowToken);
    Send(topics, R"({"op":"subscribe","topics":["run.state"]})");
    ASSERT_EQ(Companions().Revoke("deck"), 1u);
    _hub->Refresh();
    EXPECT_EQ(surface.Last()["op"].asString(), "revoked");
    EXPECT_EQ(surface.Last()["t"].asString(), "surface");
    EXPECT_TRUE(surface.Closed());
    EXPECT_FALSE(topics.Closed()) << "another companion goes on";

    Send(topics, R"({"op":"bye"})");
    EXPECT_TRUE(topics.Closed());
}

/// endregion </Binding, revocation, bye>
