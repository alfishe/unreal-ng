// WsHub and SurfaceHub: the topics of /api/v1/ws and its `surface` topic (control surfaces protocol.md, Stream Deck
// integration protocol.md §0), driven without a network: each "connection" is a send and a close function, the hub's
// loop runs tasks at once, its clock is the test's and its timer is recorded. The conformance of the protocol at the
// unit level: auth and its errors, join, actions with rid / without, coalescing, ctx (a setter and the default
// following the history), readouts and topics (full, then changed only, at the subscriber's rate), the timer armed
// only while somebody subscribes, follow and pin, revoked, bye; the `instances` topic (the events socket's view), the
// debug.* topics (DebugEventHub's events), the monitoring topics (host.stats, emulator.stats, debug.breakpoints, ...)
// and backpressure (a congested connection gets control messages only, then
// the latest values and the events with `dropped`; too long congested: slow_consumer)

#include "pch.h"

#include "ws-hub.h"
#include "common/statenode_json.h"

#include "3rdparty/message-center/messagecenter.h"
#include "_helpers/emulatortesthelper.h"
#include "_helpers/testwaithelper.h"
#include "emulator/media/recentfiles.h"
#include "emulator/actions/actionregistry.h"
#include "emulator/actions/activityfeed.h"
#include "emulator/actions/contextstate.h"
#include "emulator/actions/readouts.h"
#include "emulator/actions/targetstate.h"
#include "debugger/breakpoints/breakpointmanager.h"
#include "debugger/debugmanager.h"
#include "emulator/emulatorcontext.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/execution/automationgate.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/sound/audiomixer.h"
#include "emulator/execution/runpacer.h"

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

    void Connect(Inbox& inbox, const std::string& token = kToken, const std::string& source = "",
                 const std::string& host = "127.0.0.1", std::shared_ptr<WsFlow> flow = nullptr,
                 std::function<void()> probe = nullptr)
    {
        WsHub::Peer peer;
        peer.token = token;
        peer.source = source;
        peer.host = host;
        _hub->Open(&inbox, peer, inbox.Sender(), inbox.Closer(), std::move(flow), std::move(probe));
    }

    /// A debugger event as DebugEventHub publishes it (numbered)
    static Json::Value DebugEvent(const std::string& id, uint64_t seq, const std::string& event)
    {
        Json::Value out;
        out["op"] = "event";
        out["topic"] = "debug";
        out["seq"] = static_cast<Json::UInt64>(seq);
        out["emulator"] = id;
        out["cpu"] = "main";
        out["event"] = event;
        return out;
    }

    /// The messages of topic `t`
    static std::vector<Json::Value> InTopic(Inbox& inbox, const std::string& t)
    {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Messages())
            if (message["t"].asString() == t)
                out.push_back(message);
        return out;
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
    EXPECT_EQ(welcome["readouts"].size(), actions::ReadoutNames(*_emulator).size()) << "the whole vocabulary";
    EXPECT_EQ(welcome["serial"].asString(), "A1");
    EXPECT_TRUE(welcome["catalogue_rev"].isIntegral());
    EXPECT_EQ(welcome["features"].size(), 0u);

    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","serial":"A1"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "one surface per serial and connection";
}

TEST_F(WsHub_Test, OneConnectionCarriesASurfacePerSerialAndEveryOpNamesItsSession)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"plusxl","serial":"A1","rid":"j1","target":{"instance":")" +
                    _id + R"("}})");
    const Json::Value first = inbox.Last();
    Send(inbox, R"({"op":"join","v":1,"name":"own","model":"plus","serial":"B2","rid":"j2","features":["activity",)"
                R"("catalogue","bogus"],"target":{"instance":")" + _id + R"("}})");
    const Json::Value second = inbox.Last();
    ASSERT_EQ(first["op"].asString(), "welcome") << first.toStyledString();
    ASSERT_EQ(second["op"].asString(), "welcome") << second.toStyledString();
    EXPECT_EQ(first["rid"].asString(), "j1") << "the join's rid: the client knows which deck it answers";
    EXPECT_EQ(second["rid"].asString(), "j2");
    EXPECT_NE(first["session"].asString(), second["session"].asString());
    ASSERT_EQ(second["features"].size(), 2u) << "the features it knows";
    EXPECT_EQ(second["features"][0].asString(), "activity");
    const std::string a = first["session"].asString();
    const std::string b = second["session"].asString();

    // Two surfaces: an op without a session is ambiguous; with one it goes to that surface and the answer says so
    inbox.Clear();
    Send(inbox, R"({"op":"action","id":"test.echo","value":1,"rid":1})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request");
    EXPECT_EQ(inbox.Last()["rid"].asInt(), 1);
    inbox.Clear();
    Send(inbox, R"({"op":"action","session":")" + b + R"(","id":"test.echo","value":2,"rid":2})");
    ASSERT_TRUE(TestWait::For([&] { return !inbox.Ops("result").empty(); }));
    EXPECT_EQ(inbox.Ops("result")[0]["session"].asString(), b);
    Send(inbox, R"({"op":"subscribe","session":")" + a + R"(","readouts":["run.state"]})");
    ASSERT_FALSE(inbox.Ops("readout").empty());
    EXPECT_EQ(inbox.Ops("readout")[0]["session"].asString(), a) << "only the surface that subscribed";
    EXPECT_EQ(inbox.Ops("readout").size(), 1u);

    // bye with a session ends that surface; the socket and the other stay
    inbox.Clear();
    Send(inbox, R"({"op":"bye","session":")" + b + R"("})");
    EXPECT_FALSE(inbox.Closed());
    Send(inbox, R"({"op":"action","session":")" + b + R"(","id":"test.echo","value":1,"rid":3})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "Not Found");
    inbox.Clear();
    Send(inbox, R"({"op":"action","id":"test.echo","value":4,"rid":4})");
    ASSERT_TRUE(TestWait::For([&] { return !inbox.Ops("result").empty(); }));
    EXPECT_EQ(inbox.Ops("result")[0]["session"].asString(), a) << "one surface left: the session may be omitted";
    Send(inbox, R"({"op":"bye"})");
    EXPECT_TRUE(inbox.Closed()) << "bye without a session: every surface ends, the socket closes";
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

TEST_F(WsHub_Test, ActivityIsTheOtherSurfacesActionsOnTheBoundInstanceAtMostTwentyASecond)
{
    _registry->SetActivityFeed(&ActivityFeed::Instance());
    _hub->Start();
    Inbox watcher;
    Connect(watcher);
    Send(watcher, R"({"op":"join","v":1,"name":"deck","model":"plusxl","serial":"W","features":["activity"],)"
                  R"("target":{"instance":")" + _id + R"("}})");
    Inbox actor;
    Connect(actor);
    Send(actor, R"({"op":"join","v":1,"name":"pad","model":"plus","serial":"P","features":["activity"],)"
                R"("target":{"instance":")" + _id + R"("}})");
    watcher.Clear();
    actor.Clear();

    Send(actor, R"({"op":"action","id":"test.echo","value":{"d":2},"rid":1})");
    ASSERT_TRUE(TestWait::For([&] { return !watcher.Ops("activity").empty(); }));
    const Json::Value activity = watcher.Ops("activity")[0];
    EXPECT_EQ(activity["t"].asString(), "surface");
    EXPECT_EQ(activity["id"].asString(), "test.echo");
    EXPECT_EQ(activity["instance"].asString(), _id);
    EXPECT_EQ(activity["origin"].asString(), "companion");
    EXPECT_TRUE(activity["ok"].asBool());
    EXPECT_EQ(activity["value"]["d"].asDouble(), 2);
    ASSERT_TRUE(TestWait::For([&] { return !actor.Ops("result").empty(); }));
    EXPECT_TRUE(actor.Ops("activity").empty()) << "a surface never gets its own actions back";

    // From the GUI, 25 presses in the same second: 20 shown, the next one after the second says 5 were dropped
    watcher.Clear();
    Advance(1000);  // a new second: the echo above counted in the last one
    std::atomic<int> done{0};
    for (int i = 0; i < 25; i++)
    {
        ActionOrigin gui;
        ActionValue press;
        _registry->Submit("test.rewind", press, _id, gui, [&](ActionResult) { done++; });
    }
    ASSERT_TRUE(TestWait::ForExactly(done, 25));
    ASSERT_TRUE(TestWait::For([&] { return watcher.Ops("activity").size() >= 20; }));
    EXPECT_EQ(watcher.Ops("activity").size(), 20u);
    EXPECT_EQ(watcher.Ops("activity")[0]["origin"].asString(), "gui");
    Advance(1000);
    ActionOrigin gui;
    _registry->Submit("test.rewind", ActionValue{}, _id, gui, [&](ActionResult) { done++; });
    ASSERT_TRUE(TestWait::For([&] { return watcher.Ops("activity").size() >= 21; }));
    EXPECT_EQ(watcher.Ops("activity").back()["dropped"].asInt(), 5);
    _hub->Stop();
    _registry->SetActivityFeed(nullptr);
}

TEST_F(WsHub_Test, ACatalogueChangeIsPushedToTheSurfacesThatAskedForIt)
{
    Inbox asked;
    Connect(asked);
    Send(asked, R"({"op":"join","v":1,"name":"deck","model":"x","features":["catalogue"]})");
    Inbox other;
    Join(other);
    asked.Clear();
    _hub->OnCatalogue(42);
    ASSERT_EQ(asked.Ops("catalogue").size(), 1u);
    EXPECT_EQ(asked.Ops("catalogue")[0]["rev"].asInt(), 42);
    EXPECT_TRUE(other.Messages().empty());
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

TEST_F(WsHub_Test, SurfaceReadoutsAreTheWholeVocabulary)
{
    Inbox inbox;
    Join(inbox);
    Send(inbox, R"({"op":"subscribe","readouts":["machine","media.*","audio.mute","debug.mode","toggle.set.x"]})");
    EXPECT_TRUE(inbox.Ops("error").empty()) << inbox.Last().toStyledString();
    ASSERT_EQ(inbox.Ops("readout").size(), 1u);
    const Json::Value values = inbox.Ops("readout")[0]["values"];
    EXPECT_EQ(values["machine"]["model"].asString(), "PENTAGON");
    EXPECT_TRUE(values.isMember("media.tape"));
    EXPECT_TRUE(values.isMember("media.disk"));
    EXPECT_TRUE(values["audio.mute"].isMember("muted"));
    EXPECT_TRUE(values["debug.mode"].isMember("enabled"));
    EXPECT_TRUE(values.isMember("toggle.set.x")) << "a toggle by name: read as it is";
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

TEST_F(WsHub_Test, AudioMixerPushesTheLevelsAndMuteOfEveryRowOnceThenOnChange)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["audio.mixer"],"max_hz":50})");
    ASSERT_EQ(inbox.Ops("subscribed").size(), 1u);
    auto states = [&]() {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Ops("state"))
            if (message["t"].asString() == "audio.mixer" && message["instance"].asString() == _id)
                out.push_back(message);
        return out;
    };
    auto beeper = [](const Json::Value& value) {
        for (const Json::Value& row : value["devices"])
            if (row["source"].asString() == "beeper")
                return row;
        return Json::Value();
    };
    ASSERT_EQ(states().size(), 1u);
    EXPECT_FALSE(states()[0]["value"]["master"]["muted"].asBool());
    ASSERT_TRUE(beeper(states()[0]["value"]).isObject());
    EXPECT_FALSE(beeper(states()[0]["value"])["muted"].asBool());

    Advance(50);
    _hub->Tick();
    EXPECT_EQ(states().size(), 1u) << "unchanged: not sent again";

    AudioMixer::Change change;
    change.muted = 1;
    std::string error;
    ASSERT_TRUE(AudioMixer::Apply(_emulator->GetContext(), "beeper", change, error)) << error;
    Advance(50);
    _hub->Tick();
    ASSERT_EQ(states().size(), 2u);
    EXPECT_TRUE(beeper(states()[1]["value"])["muted"].asBool());
    EXPECT_FALSE(beeper(states()[1]["value"])["audible"].asBool());
}

TEST_F(WsHub_Test, RunStateCarriesThePaceAndItsChangeIsPushed)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["run.state","ttd.position"],"max_hz":20})");
    auto states = [&](const std::string& topic) {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Ops("state"))
            if (message["t"].asString() == topic && message["instance"].asString() == _id)
                out.push_back(message);
        return out;
    };
    ASSERT_EQ(states("run.state").size(), 1u);
    const Json::Value pace = states("run.state")[0]["value"]["pace"];
    EXPECT_EQ(pace["preset"].asString(), "full") << "no pacer yet: the default";
    EXPECT_FALSE(pace["running"].asBool());
    EXPECT_FALSE(pace.isMember("ticks")) << "no tick count: the value changes only with the pace";
    ASSERT_EQ(states("ttd.position").size(), 1u);
    EXPECT_EQ(states("ttd.position")[0]["value"]["unit"].asString(), "frames") << "the scrub unit";

    // A preset chosen on a stopped machine: the pace changes, nothing runs; the change is pushed
    size_t index = 0;
    ASSERT_TRUE(execution::RunPacer::PresetIndex("1op", index));
    const auto pacer = execution::RunPacer::For(EmulatorManager::GetInstance()->GetEmulator(_id));
    ASSERT_TRUE(pacer->Set(execution::Requester{execution::Origin::Gui, {}}, index, 300).Ok());
    Advance(50);
    _hub->Tick();
    ASSERT_EQ(states("run.state").size(), 2u);
    EXPECT_EQ(states("run.state")[1]["value"]["pace"]["preset"].asString(), "1op");
    EXPECT_EQ(states("run.state")[1]["value"]["pace"]["interval_ms"].asInt(), 300);
}

/// endregion </Readouts and topics>

/// region <instances and debug.*>

TEST_F(WsHub_Test, InstancesSendsTheViewOnceThenEachChangedEntryAndGoneAsTheEventsSocket)
{
    // The view a companion is shown (the WebAPI: CompanionMeJson): the test's
    Json::Value view(Json::objectValue);
    view["companion"]["id"] = "deck";
    view["emulators"] = Json::Value(Json::arrayValue);
    auto entry = [](const std::string& id, const std::string& state) {
        Json::Value e;
        e["id"] = id;
        e["state"] = state;
        return e;
    };
    view["emulators"].append(entry("a", "paused"));
    view["emulators"].append(entry("b", "running"));
    std::vector<std::string> askedFor;
    _hub->SetSnapshot([&](const companion::CompanionInfo& companion) {
        askedFor.push_back(companion.name);
        return view;
    });

    Inbox anonymous;
    Connect(anonymous, "");
    Send(anonymous, R"({"op":"subscribe","topics":["instances"]})");
    EXPECT_EQ(anonymous.Last()["error"].asString(), "companion_required") << "the view is a companion's";

    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["instances"]})");
    std::vector<Json::Value> messages = inbox.Messages();
    ASSERT_EQ(messages.size(), 2u);
    EXPECT_EQ(messages[0]["op"].asString(), "subscribed");
    const Json::Value hello = messages[1];
    EXPECT_EQ(hello["t"].asString(), "instances");
    EXPECT_EQ(hello["op"].asString(), "hello");
    EXPECT_EQ(hello["seq"].asUInt64(), 1u);
    EXPECT_EQ(hello["companion"]["id"].asString(), "deck");
    ASSERT_EQ(hello["emulators"].size(), 2u);
    EXPECT_EQ(askedFor, std::vector<std::string>{"deck"}) << "built once per companion and refresh";

    // Nothing changed: nothing sent; one entry changed: that entry
    inbox.Clear();
    _hub->RefreshInstances();
    EXPECT_TRUE(inbox.Messages().empty());
    view["emulators"][0]["state"] = "running";
    _hub->RefreshInstances();
    messages = inbox.Messages();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0]["op"].asString(), "emulator");
    EXPECT_EQ(messages[0]["seq"].asUInt64(), 2u);
    EXPECT_EQ(messages[0]["emulator"]["id"].asString(), "a");
    EXPECT_EQ(messages[0]["emulator"]["state"].asString(), "running");

    // An instance closed
    view["emulators"].resize(1);
    inbox.Clear();
    _hub->RefreshInstances();
    ASSERT_EQ(inbox.Messages().size(), 1u);
    EXPECT_EQ(inbox.Last()["op"].asString(), "emulator_gone");
    EXPECT_EQ(inbox.Last()["id"].asString(), "b");
    EXPECT_EQ(inbox.Last()["seq"].asUInt64(), 3u);

    // {"op":"snapshot"}: the whole view again; the tick re-checks once a second (what changes with time alone)
    inbox.Clear();
    EXPECT_TRUE(Send(inbox, R"({"op":"snapshot"})"));
    ASSERT_EQ(inbox.Ops("snapshot").size(), 1u);
    EXPECT_EQ(inbox.Ops("snapshot")[0]["emulators"].size(), 1u);
    view["emulators"].append(entry("c", "paused"));
    inbox.Clear();
    Advance(1000);
    _hub->Tick();
    ASSERT_EQ(inbox.Ops("emulator").size(), 1u);
    EXPECT_EQ(inbox.Ops("emulator")[0]["emulator"]["id"].asString(), "c");

    Send(inbox, R"({"op":"unsubscribe","topics":["instances"]})");
    Send(inbox, R"({"op":"snapshot"})");
    EXPECT_EQ(inbox.Last()["error"].asString(), "bad_request") << "snapshot needs the topic";
    EXPECT_EQ(_hub->TimerHz(), 0);
}

TEST_F(WsHub_Test, DebugTopicsCarryDebugEventHubsEventsAndNeedNoTokenFromThisHost)
{
    Inbox remote;
    Connect(remote, "", "", "192.168.1.5");
    Send(remote, R"({"op":"subscribe","topics":["debug.*"]})");
    EXPECT_EQ(remote.Last()["error"].asString(), "companion_required");
    EXPECT_TRUE(remote.Ops("subscribed").empty()) << "nothing subscribed: no ack, as for the state topics";

    Inbox local;
    Connect(local, "");
    Send(local, R"({"op":"subscribe","topics":["debug.*"]})");
    const Json::Value ack = local.Last();
    ASSERT_EQ(ack["op"].asString(), "subscribed");
    ASSERT_EQ(ack["topics"].size(), 2u);
    EXPECT_EQ(ack["topics"][0].asString(), "debug.asm_sync");
    EXPECT_EQ(ack["topics"][1].asString(), "debug.events");

    Inbox companionOnly;
    Connect(companionOnly);
    Send(companionOnly, R"({"op":"subscribe","topics":["debug.events"]})");
    local.Clear();
    companionOnly.Clear();

    _hub->OnDebugEvent("debug", DebugEvent(_id, 7, "paused"));
    Json::Value asmSync = DebugEvent(_id, 8, "asm_sync_built");
    asmSync["topic"] = "asm_sync";
    _hub->OnDebugEvent("asm_sync", asmSync);
    const std::vector<Json::Value> events = local.Ops("event");
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0]["t"].asString(), "debug.events");
    EXPECT_EQ(events[0]["event"].asString(), "paused");
    EXPECT_EQ(events[0]["seq"].asUInt64(), 7u) << "DebugEventHub's numbering, as /api/v1/websocket";
    EXPECT_EQ(events[0]["instance"].asString(), _id);
    EXPECT_EQ(events[0]["emulator"].asString(), _id);
    EXPECT_FALSE(events[0].isMember("topic")) << "t is the topic";
    EXPECT_FALSE(events[0].isMember("dropped"));
    EXPECT_EQ(events[1]["t"].asString(), "debug.asm_sync");
    ASSERT_EQ(companionOnly.Ops("event").size(), 1u) << "only the topics it subscribed";
    EXPECT_EQ(_hub->TimerHz(), WsHub::kDefaultHz);

    Send(local, R"({"op":"unsubscribe","topics":["debug.*"]})");
    Send(companionOnly, R"({"op":"unsubscribe","topics":["*"]})");
    local.Clear();
    _hub->OnDebugEvent("debug", DebugEvent(_id, 9, "resumed"));
    EXPECT_TRUE(local.Messages().empty());
    EXPECT_EQ(_hub->TimerHz(), 0);
}

/// endregion </instances and debug.*>

/// region <Backpressure>

TEST_F(WsHub_Test, ACongestedConnectionGetsControlMessagesOnlyThenTheLatestValuesAndEventsWithDropped)
{
    auto flow = std::make_shared<WsFlow>();
    int probes = 0;
    Inbox inbox;
    Connect(inbox, kToken, "", "127.0.0.1", flow, [&] { probes++; });
    Send(inbox, R"({"op":"subscribe","topics":["run.state","debug.events"],"max_hz":50})");
    ASSERT_EQ(InTopic(inbox, "run.state").size(), 1u);
    EXPECT_FALSE(InTopic(inbox, "run.state")[0]["value"]["turbo"].asBool());

    // Above the high-water mark: values wait, events queue (the oldest dropped), the transport is asked to measure
    flow->queued = 300000;
    flow->congested = true;
    inbox.Clear();
    _emulator->EnableTurboMode(false);
    for (uint64_t seq = 1; seq <= WsHub::kEventQueue + 44; ++seq)
        _hub->OnDebugEvent("debug", DebugEvent(_id, seq, "step_done"));
    Advance(20);
    _hub->Tick();
    Advance(20);
    _hub->Tick();
    EXPECT_TRUE(inbox.Messages().empty()) << "nothing but control messages";
    EXPECT_EQ(probes, 2) << "one measure per tick while congested";
    // ...control messages still go at once (the tick a subscribe wakes measures again)
    Send(inbox, R"({"op":"subscribe","topics":["surfaces.context"]})");
    ASSERT_EQ(inbox.Messages().size(), 1u);
    EXPECT_EQ(inbox.Last()["op"].asString(), "subscribed");
    EXPECT_EQ(probes, 3);

    // Drained: the latest values once, the events kept with how many were lost
    flow->congested = false;
    inbox.Clear();
    Advance(20);
    _hub->Tick();
    const std::vector<Json::Value> states = InTopic(inbox, "run.state");
    ASSERT_EQ(states.size(), 1u);
    EXPECT_TRUE(states[0]["value"]["turbo"].asBool()) << "the latest value";
    const std::vector<Json::Value> events = inbox.Ops("event");
    ASSERT_EQ(events.size(), WsHub::kEventQueue);
    EXPECT_EQ(events.front()["dropped"].asUInt64(), 44u);
    EXPECT_EQ(events.front()["seq"].asUInt64(), 45u) << "the oldest were dropped";
    EXPECT_FALSE(events[1].isMember("dropped"));
    EXPECT_EQ(events.back()["seq"].asUInt64(), WsHub::kEventQueue + 44);
    EXPECT_EQ(probes, 3) << "not measured once below the mark";
    _emulator->DisableTurboMode();
}

TEST_F(WsHub_Test, AConnectionCongestedLongerThanTheLimitIsClosedAsASlowConsumer)
{
    _hub->SetSlowConsumerLimit(std::chrono::milliseconds(500));
    auto flow = std::make_shared<WsFlow>();
    Inbox slow;
    Connect(slow, kToken, "", "127.0.0.1", flow);
    Send(slow, R"({"op":"subscribe","topics":["run.state"],"max_hz":10})");
    Inbox other;
    Connect(other);
    Send(other, R"({"op":"subscribe","topics":["run.state"],"max_hz":10})");

    flow->queued = 262145;
    flow->congested = true;
    _hub->Tick();  // congested from now
    Advance(400);
    _hub->Tick();
    flow->congested = false;  // drained in time: the clock starts again
    Advance(100);
    _hub->Tick();
    flow->congested = true;
    _hub->Tick();
    Advance(499);
    _hub->Tick();
    EXPECT_FALSE(slow.Closed());
    Advance(1);
    _hub->Tick();
    EXPECT_TRUE(slow.Closed());
    const Json::Value error = slow.Last();
    EXPECT_EQ(error["op"].asString(), "error");
    EXPECT_EQ(error["t"].asString(), "ws");
    EXPECT_EQ(error["error"].asString(), "slow_consumer");
    EXPECT_NE(error["message"].asString().find("262145"), std::string::npos) << error["message"].asString();
    EXPECT_FALSE(other.Closed()) << "the others go on";
    _hub->Remove(&slow);
    EXPECT_EQ(_hub->Connections(), 1u);
}

/// endregion </Backpressure>

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

/// region <Monitoring topics>

TEST_F(WsHub_Test, MonitoringTopicsPushOnSubscribeThenOneCoalescedValueAfterABurstOfChanges)
{
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["debug.breakpoints","host.stats","emulator.stats"],"max_hz":10})");
    ASSERT_EQ(inbox.Ops("subscribed").size(), 1u) << inbox.Last().toStyledString();
    auto states = [&](const std::string& topic) {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Ops("state"))
            if (message["t"].asString() == topic)
                out.push_back(message);
        return out;
    };
    // At once: each instance's value, and the process's without an instance
    ASSERT_EQ(states("debug.breakpoints").size(), 1u);
    EXPECT_EQ(states("debug.breakpoints")[0]["instance"].asString(), _id);
    EXPECT_EQ(states("debug.breakpoints")[0]["value"]["total"].asInt(), 0);
    ASSERT_EQ(states("host.stats").size(), 1u);
    EXPECT_FALSE(states("host.stats")[0].isMember("instance")) << "a topic of the process";
    EXPECT_GT(states("host.stats")[0]["value"]["rss_bytes"].asInt64(), 0);
    ASSERT_EQ(states("emulator.stats").size(), 1u);
    EXPECT_EQ(states("emulator.stats")[0]["value"]["state"].asString(), "stopped");
    EXPECT_FALSE(states("emulator.stats")[0]["value"]["measuring"].asBool()) << "not started: nothing measured";

    // Three changes within one interval: one message with the last value
    BreakpointManager* breakpoints = _emulator->GetContext()->pDebugManager->GetBreakpointsManager();
    const uint16_t a = breakpoints->AddExecutionBreakpoint(0x8000);
    breakpoints->AddMemWriteBreakpoint(0x4000);
    breakpoints->AddPortOutBreakpoint(0x00FE);
    ASSERT_TRUE(breakpoints->DeactivateBreakpoint(a));
    Advance(40);
    _hub->Tick();
    EXPECT_EQ(states("debug.breakpoints").size(), 1u) << "within the subscriber's 100 ms: held back";
    Advance(60);
    _hub->Tick();
    ASSERT_EQ(states("debug.breakpoints").size(), 2u);
    const Json::Value value = states("debug.breakpoints")[1]["value"];
    EXPECT_EQ(value["total"].asInt(), 3);
    EXPECT_EQ(value["active"].asInt(), 2);
    EXPECT_EQ(value["inactive"].asInt(), 1);
    EXPECT_EQ(value["by_kind"]["exec"].asInt(), 1);
    EXPECT_EQ(value["by_kind"]["write"].asInt(), 1);
    EXPECT_EQ(value["by_kind"]["port_out"].asInt(), 1);
    ASSERT_EQ(value["groups"].size(), 1u);
    EXPECT_EQ(value["groups"][0]["name"].asString(), "default");
    EXPECT_EQ(value["groups"][0]["total"].asInt(), 3);
    EXPECT_TRUE(value["last_hit"].isNull());

    // Unchanged: nothing more
    Advance(100);
    _hub->Tick();
    EXPECT_EQ(states("debug.breakpoints").size(), 2u);
    breakpoints->ClearBreakpoints();
}

TEST_F(WsHub_Test, RunStateSpeedIsTheHostSettingOnTheTopicAndTheReadoutAlike)
{
    // The machine's own turbo doubles the composed multiplier; the host setting stays 1x
    EmulatorState& state = _emulator->GetContext()->emulatorState;
    const uint8_t current = state.current_z80_frequency_multiplier;
    const uint8_t next = state.next_z80_frequency_multiplier;
    state.current_z80_frequency_multiplier = 2;
    state.next_z80_frequency_multiplier = 1;
    const Json::Value topic = WsHub::Read("run.state", _id, *_emulator);
    EXPECT_EQ(topic["speed"].asInt(), 1);
    EXPECT_EQ(topic, StateNodeToJson(actions::ReadReadout(*_emulator, "run.state"))) << "one producer";
    const Json::Value stats = WsHub::Read("emulator.stats", _id, *_emulator);
    EXPECT_EQ(stats["cpu"]["multiplier"].asInt(), 2) << "the composed clock is emulator.stats'";
    EXPECT_EQ(stats["cpu"]["host_multiplier"].asInt(), 1);
    state.current_z80_frequency_multiplier = current;
    state.next_z80_frequency_multiplier = next;
}

TEST_F(WsHub_Test, AFollowingSurfaceTakesTheFrontViewsInstanceAsTheTargetStateDoes)
{
    Emulator* second = EmulatorTestHelper::CreateStandardEmulator("PENTAGON", LoggerLevel::LogError);
    ASSERT_NE(second, nullptr);
    EmulatorManager* manager = EmulatorManager::GetInstance();
    ASSERT_TRUE(manager->SetSelectedEmulatorId(_id));
    {
        // A window showing the second instance has the focus: rule 3 beats the selection (rule 4)
        ViewHandle view = TargetState::Instance().RegisterView("Window 2", "test");
        TargetState::Instance().SetActive(view, second->GetId());
        TargetState::Instance().Activated(view);
        EXPECT_EQ(WsHub::FollowedInstance(), TargetState::Instance().Followed());
        EXPECT_EQ(WsHub::FollowedInstance(), second->GetId());

        Inbox inbox;
        Connect(inbox);
        Send(inbox, R"({"op":"join","v":1,"name":"deck","model":"x","target":"follow"})");
        ASSERT_EQ(inbox.Last()["op"].asString(), "welcome");
        EXPECT_EQ(inbox.Last()["instance"].asString(), second->GetId());
        inbox.Clear();
        view.Reset();  // the window closes: back to the selection
        _hub->Refresh();
        ASSERT_FALSE(inbox.Ops("target").empty());
        EXPECT_EQ(inbox.Ops("target").back()["instance"].asString(), _id);
        _hub->Remove(&inbox);
    }
    manager->SetSelectedEmulatorId("");
    const std::string secondId = second->GetId();
    EmulatorTestHelper::CleanupEmulator(second);
    actions::ContextState::Instance().Drop(secondId);
}

/// endregion </Monitoring topics>

/// The machine area's state topics (the readouts of the same name): pushed once on subscribe, then a change
TEST_F(WsHub_Test, TheMachineAreaTopicsPushTheirReadoutsAndAChange)
{
    RecentFiles::Instance().Clear();
    Inbox inbox;
    Connect(inbox);
    Send(inbox, R"({"op":"subscribe","topics":["media.recent","media.rzx","cap.record","machine.switches",)"
                R"("input.connectors"]})");
    ASSERT_EQ(inbox.Ops("subscribed").size(), 1u);
    EXPECT_EQ(inbox.Ops("subscribed")[0]["topics"].size(), 5u);
    auto states = [&](const std::string& topic) {
        std::vector<Json::Value> out;
        for (const Json::Value& message : inbox.Ops("state"))
            if (message["t"].asString() == topic && message["instance"].asString() == _id)
                out.push_back(message);
        return out;
    };
    ASSERT_EQ(states("media.recent").size(), 1u);
    EXPECT_EQ(states("media.recent")[0]["value"]["all"].size(), 0u);
    ASSERT_EQ(states("media.rzx").size(), 1u);
    EXPECT_FALSE(states("media.rzx")[0]["value"]["loaded"].asBool());
    ASSERT_EQ(states("cap.record").size(), 1u);
    ASSERT_EQ(states("machine.switches").size(), 1u);
    ASSERT_EQ(states("input.connectors").size(), 1u);
    EXPECT_TRUE(states("input.connectors")[0]["value"]["connectors"].isArray());

    RecentFiles::Instance().Note("tape", "/games/elite.tzx");
    Advance(100);
    _hub->Tick();
    ASSERT_EQ(states("media.recent").size(), 2u) << "the change is pushed";
    EXPECT_EQ(states("media.recent")[1]["value"]["all"][0]["name"].asString(), "elite.tzx");
    EXPECT_EQ(states("media.rzx").size(), 1u) << "unchanged: not sent again";
    RecentFiles::Instance().Clear();
}
