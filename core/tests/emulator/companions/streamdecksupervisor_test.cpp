// The Stream Deck daemon's supervisor (companion::streamdeck::Supervisor; Stream Deck integration daemon.md §7,
// architecture.md §8) and its verbs on every plane (companion::streamdeck::Call). The state machine runs on the
// test's own clock with a fake launcher and a fake daemon: no thread, no process, no wait.

#include <gtest/gtest.h>

#include <algorithm>
#include <fstream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "_helpers/testpathhelper.h"
#include "emulator/companions/streamdeckapi.h"
#include "emulator/companions/streamdecksupervisor.h"
#include "emulator/execution/companionregistry.h"
#include "emulator/state/statenodejson.h"

using namespace companion::streamdeck;

namespace
{

/// What the fake processes and the fake daemon share
struct World
{
    bool up = false;        ///< the daemon's automation answers
    bool hung = false;      ///< it runs but answers nothing
    int pid = 0;            ///< the answering daemon's pid
    std::string host = "http://127.0.0.1:8090";
    std::string pairing = R"({"state":"idle","paired":false,"companion_id":""})";
    std::vector<std::string> calls;  ///< "METHOD path body"
    std::vector<int> leases;
    int nextPid = 1000;
    bool launchFails = false;
    bool upOnStart = true;  ///< a started daemon answers at once
    std::vector<LaunchSpec> starts;

    bool Called(const std::string& prefix) const
    {
        return std::any_of(calls.begin(), calls.end(), [&](const std::string& c) { return c.rfind(prefix, 0) == 0; });
    }
    size_t Count(const std::string& prefix) const
    {
        return static_cast<size_t>(
            std::count_if(calls.begin(), calls.end(), [&](const std::string& c) { return c.rfind(prefix, 0) == 0; }));
    }
};

class FakeProcess : public IProcess
{
public:
    FakeProcess(World& world, int pid) : _world(world), _pid(pid) {}
    int Pid() const override { return _pid; }
    bool Exited(int& code, bool& crashed) override
    {
        code = this->code;
        crashed = this->crashed;
        return exited;
    }
    void Terminate() override
    {
        ++terminates;
        End(0, false);
    }
    void Kill() override
    {
        ++kills;
        End(137, true);
    }
    void End(int c, bool crash)
    {
        exited = true;
        code = c;
        crashed = crash;
        if (_world.pid == _pid)
            _world.up = false;
    }

    bool exited = false;
    int code = 0;
    bool crashed = false;
    int terminates = 0, kills = 0;

private:
    World& _world;
    int _pid;
};

class FakeLauncher : public ILauncher
{
public:
    explicit FakeLauncher(World& world) : _world(world) {}
    std::unique_ptr<IProcess> Start(const LaunchSpec& spec, std::string& error) override
    {
        _world.starts.push_back(spec);
        if (_world.launchFails)
        {
            error = "cannot start " + spec.path + ": No such file or directory";
            return nullptr;
        }
        auto process = std::make_unique<FakeProcess>(_world, ++_world.nextPid);
        last = process.get();
        if (_world.upOnStart)
        {
            _world.up = true;
            _world.hung = false;
            _world.pid = process->Pid();
        }
        return process;
    }
    FakeProcess* last = nullptr;  ///< owned by the supervisor while it runs

private:
    World& _world;
};

class FakeDaemon : public IDaemonHttp
{
public:
    explicit FakeDaemon(World& world) : _world(world) {}
    HttpResult Call(int port, const std::string& method, const std::string& path, const std::string& body,
                    int timeoutMs) override
    {
        (void)port;
        (void)timeoutMs;
        _world.calls.push_back(method + " " + path + (body.empty() ? "" : " " + body));
        if (!_world.up || _world.hung)
            return {0, "", "connection refused"};
        if (method == "GET" && path == "/api/v1/health")
            return {200,
                    R"({"ok":true,"name":"unreal-streamdeck","version":"1.0","decks":1,"viewers":2,"pid":)" +
                        std::to_string(_world.pid) + R"(,"host":")" + _world.host + R"(","surface":"connected","pairing":)" +
                        _world.pairing + "}",
                    ""};
        if (method == "GET" && path == "/api/v1/decks")
            return {200, R"({"decks":[{"serial":"A1B2C3","model":"Stream Deck + XL","real":true,"brightness":70}]})", ""};
        if (method == "POST" && path == "/api/v1/parents")
        {
            StateNode node;
            StateNodeFromJsonText(body, node);
            _world.leases.push_back(static_cast<int>(node.find("pid") ? node.find("pid")->i : 0));
            return {200, R"({"parents":[]})", ""};
        }
        if (method == "POST" && path == "/api/v1/quit")
            return {200, R"({"quitting":true})", ""};
        if (method == "POST" && path == "/api/v1/pair")
            return {200, R"({"state":"requesting","paired":false})", ""};
        if (method == "GET" && path == "/api/v1/pair")
            return {200, _world.pairing, ""};
        if (method == "PUT" && path.find("/brightness") != std::string::npos)
        {
            const size_t at = body.find(':');
            return {200, R"({"serial":"A1B2C3","brightness":)" + body.substr(at + 1, body.size() - at - 2) + "}", ""};
        }
        return {200, "{}", ""};
    }

private:
    World& _world;
};

std::string Str(const StateNode& node, const char* key)
{
    const StateNode* v = node.find(key);
    return v && v->kind == StateNode::Kind::String ? v->s : std::string();
}

int64_t Int(const StateNode& node, const char* key)
{
    const StateNode* v = node.find(key);
    return v && v->kind == StateNode::Kind::Int ? v->i : -1;
}

constexpr int kOwnPid = 4242;

} // namespace

class StreamDeckSupervisor_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        _launcher = std::make_shared<FakeLauncher>(_world);
        _sup.SetLauncher(_launcher);
        _sup.SetHttp(std::make_shared<FakeDaemon>(_world));
    }
    void TearDown() override { _sup.Detach(); }

    void Attach(Mode mode, int decks = 1)
    {
        _decks = decks;
        Host host;
        host.webapiPort = 8090;
        host.ownPid = kOwnPid;
        host.defaultBinary = "/app/UnrealNG.app/Contents/MacOS/unreal-streamdeck";
        host.logFile = "/tmp/daemon.log";
        host.decksAttached = [this] { return _decks; };
        Settings settings;
        settings.mode = mode;
        _sup.Attach(host, settings, false);
    }
    /// One step _ms later (the deck check runs every second: a step a second sees a new count)
    void At(int64_t ms) { _sup.Step(_now = ms); }
    std::string State() const { return Str(_sup.Status(), "state"); }

    World _world;
    std::shared_ptr<FakeLauncher> _launcher;
    Supervisor _sup;
    int _decks = 0;
    int64_t _now = 0;
};

TEST_F(StreamDeckSupervisor_Test, ModeDeckStartsItWhenADeckIsAttachedAndKeepsItWhenTheDeckGoes)
{
    Attach(Mode::Deck, 0);
    At(0);
    EXPECT_EQ(State(), "waiting");
    EXPECT_TRUE(_world.starts.empty()) << "no deck: nothing started";

    _decks = 1;
    At(1000);
    ASSERT_EQ(_world.starts.size(), 1u);
    EXPECT_EQ(State(), "starting");
    EXPECT_TRUE(_world.Called("GET /api/v1/health")) << "it looked for a running one first (to adopt)";
    const LaunchSpec& spec = _world.starts[0];
    EXPECT_EQ(spec.path, "/app/UnrealNG.app/Contents/MacOS/unreal-streamdeck");
    EXPECT_EQ(spec.logFile, "/tmp/daemon.log");
    const std::vector<std::string> expected = {"run",    "--wait",         "--api",    "37053",
                                               "--viewers", "37050",       "--host",   "127.0.0.1:8090",
                                               "--parent", std::to_string(kOwnPid), "--activity", "flash"};
    EXPECT_EQ(spec.args, expected);

    At(1200);
    EXPECT_EQ(State(), "running");
    const StateNode status = _sup.Status();
    EXPECT_EQ(Int(status, "pid"), _launcher->last->Pid());
    EXPECT_TRUE(status.find("own")->b);
    EXPECT_EQ(Str(*status.find("daemon"), "version"), "1.0");
    EXPECT_EQ(status.find("decks")->size(), 1u);
    EXPECT_TRUE(_world.Called("PUT /api/v1/activity {\"look\":\"flash\"}")) << "the look applied once it answers";

    _decks = 0;
    At(2200);
    At(3200);
    EXPECT_EQ(State(), "running") << "the deck went: its runtime waits for it, own decks go on";
    EXPECT_EQ(_world.starts.size(), 1u);
}

TEST_F(StreamDeckSupervisor_Test, ACrashIsRestartedAfterOneTwoFourSecondsThenAPauseOfAMinute)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    ASSERT_EQ(State(), "running");

    int64_t t = 1000;
    for (int64_t delay : {1000, 2000, 4000})
    {
        _launcher->last->End(139, true);
        At(t);
        ASSERT_EQ(State(), "backoff") << delay;
        const size_t starts = _world.starts.size();
        At(t + delay - 1);
        EXPECT_EQ(_world.starts.size(), starts) << "not before " << delay << " ms";
        At(t + delay);
        EXPECT_EQ(_world.starts.size(), starts + 1) << "again after " << delay << " ms";
        t += delay + 1000;
    }
    EXPECT_EQ(Int(_sup.Status(), "restarts"), 3);

    _launcher->last->End(139, true);
    At(t);
    EXPECT_EQ(State(), "failed") << "the fourth end in a row: a pause";
    EXPECT_NE(Str(_sup.Status(), "error").find("60 s"), std::string::npos);
    const size_t starts = _world.starts.size();
    At(t + Supervisor::kGiveUpMs - 1);
    EXPECT_EQ(_world.starts.size(), starts);
    At(t + Supervisor::kGiveUpMs);
    EXPECT_EQ(_world.starts.size(), starts + 1) << "tried again after the minute";
    const StateNode after = _sup.Status();
    const StateNode* last = after.find("last_exit");
    ASSERT_NE(last, nullptr);
    EXPECT_TRUE(last->find("crashed")->b);
}

TEST_F(StreamDeckSupervisor_Test, AStableRunStartsTheCrashCountAgain)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    _launcher->last->End(139, true);
    At(1000);
    At(2000);  // started again after 1 s
    At(2200);
    ASSERT_EQ(State(), "running");
    for (int64_t t = 4200; t <= 2200 + Supervisor::kStableMs + Supervisor::kHealthEveryMs; t += 2000)
        At(t);
    EXPECT_EQ(Int(_sup.Status(), "failures"), 0) << "30 s healthy: the next crash waits 1 s again";
    _launcher->last->End(139, true);
    const int64_t t = _now + 1;
    At(t);
    const size_t starts = _world.starts.size();
    At(t + 1000);
    EXPECT_EQ(_world.starts.size(), starts + 1);
}

TEST_F(StreamDeckSupervisor_Test, AHungDaemonIsKilledAfterThreeFailedHealthChecks)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    ASSERT_EQ(State(), "running");
    FakeProcess* hung = _launcher->last;
    _world.hung = true;
    At(2200);
    At(4200);
    EXPECT_EQ(hung->kills, 0) << "two failed checks: not yet";
    At(6200);
    EXPECT_EQ(hung->kills, 1) << "the third: killed";
    EXPECT_NE(Str(_sup.Status(), "error").find("stopped answering"), std::string::npos);
    _world.hung = false;
    At(6300);
    EXPECT_EQ(State(), "backoff");
    At(7300);
    EXPECT_EQ(_world.starts.size(), 2u);
}

TEST_F(StreamDeckSupervisor_Test, ADaemonThatRunsIsAdoptedWithALeaseGivenBackAtDetach)
{
    _world.up = true;
    _world.pid = 777;  // started by hand, or by Stream Deck Studio
    Attach(Mode::Always);
    At(0);
    EXPECT_EQ(State(), "adopted");
    EXPECT_TRUE(_world.starts.empty());
    ASSERT_EQ(_world.leases.size(), 1u);
    EXPECT_EQ(_world.leases[0], kOwnPid);
    const StateNode status = _sup.Status();
    EXPECT_FALSE(status.find("own")->b);
    EXPECT_EQ(Int(status, "pid"), 777);
    EXPECT_TRUE(_world.Called("PUT /api/v1/activity"));

    _sup.Detach();
    EXPECT_TRUE(_world.Called("DELETE /api/v1/parents/" + std::to_string(kOwnPid)))
        << "the lease back: it ends unless its other starter still holds it";
    EXPECT_FALSE(_world.Called("POST /api/v1/quit")) << "never stopped at quit";
}

TEST_F(StreamDeckSupervisor_Test, AnAdoptedDaemonThatGoesIsReplacedByOurs)
{
    _world.up = true;
    _world.pid = 777;
    Attach(Mode::Always);
    At(0);
    ASSERT_EQ(State(), "adopted");
    _world.up = false;
    At(2000);
    At(4000);
    EXPECT_EQ(State(), "adopted");
    At(6000);
    At(6250);
    EXPECT_EQ(_world.starts.size(), 1u) << "three failed checks: it went, ours starts";
}

TEST_F(StreamDeckSupervisor_Test, ADaemonOfAnotherHostIsLeftAlone)
{
    _world.up = true;
    _world.pid = 777;
    _world.host = "http://127.0.0.1:8190";
    Attach(Mode::Always);
    At(0);
    EXPECT_EQ(State(), "foreign");
    EXPECT_TRUE(_world.leases.empty());
    EXPECT_TRUE(_world.starts.empty());
    EXPECT_NE(Str(_sup.Status(), "error").find("8190"), std::string::npos);
}

TEST_F(StreamDeckSupervisor_Test, StopAsksItToQuitTerminatesAfterTwoSecondsAndNothingStartsUntilStart)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    FakeProcess* child = _launcher->last;
    _sup.Stop();
    At(300);
    EXPECT_TRUE(_world.Called("POST /api/v1/quit"));
    EXPECT_EQ(child->terminates, 0);
    At(300 + Supervisor::kStopGraceMs);
    EXPECT_EQ(child->terminates, 1) << "it did not go by itself within 2 s";
    At(2400);
    EXPECT_EQ(State(), "stopped");
    At(5000);
    At(9000);
    EXPECT_EQ(_world.starts.size(), 1u) << "stopped by the user: not started again";
    _sup.Start();
    At(9100);
    EXPECT_EQ(_world.starts.size(), 2u);
}

TEST_F(StreamDeckSupervisor_Test, RestartStartsAgainAtOnceWithoutCountingACrash)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    FakeProcess* first = _launcher->last;
    _sup.Restart();
    At(300);
    EXPECT_TRUE(_world.Called("POST /api/v1/quit"));
    first->End(0, false);  // it quits
    At(400);
    EXPECT_EQ(_world.starts.size(), 2u);
    EXPECT_EQ(State(), "starting");
    EXPECT_EQ(Int(_sup.Status(), "restarts"), 0);
}

TEST_F(StreamDeckSupervisor_Test, ModeOffGivesTheLeaseBackAndStartsNothing)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    std::string error;
    StateNode changes = StateNode::Object();
    changes["mode"] = "off";
    ASSERT_TRUE(_sup.Update(changes, error)) << error;
    At(300);
    EXPECT_TRUE(_world.Called("DELETE /api/v1/parents/" + std::to_string(kOwnPid)));
    EXPECT_FALSE(_world.Called("POST /api/v1/quit")) << "another starter may still hold it";
    EXPECT_EQ(State(), "off");
    _launcher->last->End(0, false);  // nobody else held it: it ended by itself
    At(1300);
    EXPECT_EQ(State(), "off");
    EXPECT_EQ(_world.starts.size(), 1u);

    Settings off = _sup.GetSettings();
    EXPECT_EQ(off.mode, Mode::Off);
    _sup.Start();  // the user asks: also in mode off
    At(1400);
    EXPECT_EQ(_world.starts.size(), 2u);
}

TEST_F(StreamDeckSupervisor_Test, SettingsAreCheckedSavedAndAppliedToTheDaemon)
{
    Attach(Mode::Always);
    int saved = 0;
    Settings last;
    _sup.SetSettingsListener([&](const Settings& s) {
        ++saved;
        last = s;
    });
    std::string error;
    StateNode bad = StateNode::Object();
    bad["mode"] = "sometimes";
    EXPECT_FALSE(_sup.Update(bad, error));
    EXPECT_NE(error.find("off, deck or always"), std::string::npos);
    bad = StateNode::Object();
    bad["api_port"] = "70000";
    EXPECT_FALSE(_sup.Update(bad, error));
    EXPECT_EQ(saved, 0) << "nothing changed";

    At(0);
    At(200);
    StateNode changes = StateNode::Object();
    changes["activity"] = "line";
    changes["brightness"] = StateNode::Object();
    changes["brightness"]["A1B2C3"] = int64_t{35};
    ASSERT_TRUE(_sup.Update(changes, error)) << error;
    EXPECT_EQ(saved, 1);
    EXPECT_EQ(last.activity, "line");
    EXPECT_EQ(last.brightness["A1B2C3"], 35);
    EXPECT_TRUE(_sup.Update(changes, error));
    EXPECT_EQ(saved, 1) << "the same values: not saved again";
    At(2200);
    EXPECT_TRUE(_world.Called("PUT /api/v1/activity {\"look\":\"line\"}"));
    EXPECT_TRUE(_world.Called("PUT /api/v1/decks/A1B2C3/brightness {\"value\":35}"));
    At(4200);
    EXPECT_EQ(_world.Count("PUT /api/v1/decks/A1B2C3/brightness"), 1u) << "applied once per daemon";

    StateNode port = StateNode::Object();
    port["api_port"] = int64_t{37253};
    ASSERT_TRUE(_sup.Update(port, error));
    At(4300);
    EXPECT_TRUE(_world.Called("POST /api/v1/quit")) << "new ports: ours restarts on them";
}

TEST_F(StreamDeckSupervisor_Test, AMissingBinaryFailsWithTheReasonAndTriesAgainLater)
{
    _world.launchFails = true;
    Attach(Mode::Always);
    At(0);
    EXPECT_EQ(State(), "failed");
    EXPECT_NE(Str(_sup.Status(), "error").find("No such file"), std::string::npos);
    _world.launchFails = false;
    At(Supervisor::kGiveUpMs);
    EXPECT_EQ(_world.starts.size(), 2u);
}

TEST_F(StreamDeckSupervisor_Test, AStartThatFindsOneRunningEndsAndTheRunningOneIsAdopted)
{
    // ours ends at once with 0: it found the lock taken and handed our lease to the running daemon
    _world.upOnStart = false;
    Attach(Mode::Always);
    At(0);
    ASSERT_EQ(_world.starts.size(), 1u);
    _launcher->last->End(0, false);
    _world.up = true;
    _world.pid = 555;  // the one holding the lock, on our port
    At(100);
    EXPECT_EQ(State(), "backoff") << "a clean end at once is not taken for a healthy run";
    At(1100);
    EXPECT_EQ(State(), "adopted");
    EXPECT_EQ(_world.starts.size(), 1u);
}

TEST_F(StreamDeckSupervisor_Test, TheBinaryComesFromTheEnvironmentThenTheSettingThenTheHost)
{
    Attach(Mode::Off);
    EXPECT_EQ(_sup.BinaryPath(), "/app/UnrealNG.app/Contents/MacOS/unreal-streamdeck");
    std::string error;
    StateNode changes = StateNode::Object();
    changes["binary"] = "/opt/sd/unreal-streamdeck";
    ASSERT_TRUE(_sup.Update(changes, error));
    EXPECT_EQ(_sup.BinaryPath(), "/opt/sd/unreal-streamdeck");
}

TEST_F(StreamDeckSupervisor_Test, TheSummaryTheTopicPushesHasNoTimeInIt)
{
    Attach(Mode::Always);
    At(0);
    At(200);
    const StateNode summary = _sup.Summary();
    EXPECT_EQ(Str(summary, "state"), "running");
    EXPECT_EQ(Int(summary, "decks"), 1);
    EXPECT_EQ(Int(summary, "viewers"), 2);
    EXPECT_EQ(Str(*summary.find("pairing"), "state"), "idle");
    EXPECT_EQ(summary.find("restart_in_ms"), nullptr);
    EXPECT_EQ(StateNodeToJsonText(summary), StateNodeToJsonText(_sup.Summary())) << "the same twice: nothing pushed";
}

// ---------------------------------------------------------------- the verbs every plane calls

class StreamDeckApi_Test : public ::testing::Test
{
protected:
    void SetUp() override
    {
        Supervisor::Instance().ResetForTesting();
        _launcher = std::make_shared<FakeLauncher>(_world);
        Supervisor::Instance().SetLauncher(_launcher);
        Supervisor::Instance().SetHttp(std::make_shared<FakeDaemon>(_world));
        companion::CompanionRegistry::Instance().ResetForTesting();
    }
    void TearDown() override
    {
        Supervisor::Instance().ResetForTesting();
        companion::CompanionRegistry::Instance().ResetForTesting();
    }
    void Attach(Mode mode)
    {
        Host host;
        host.ownPid = kOwnPid;
        host.defaultBinary = "/bin/unreal-streamdeck";
        host.decksAttached = [] { return 1; };
        Settings settings;
        settings.mode = mode;
        Supervisor::Instance().Attach(host, settings, false);
    }
    static StateNode Args(std::initializer_list<std::pair<const char*, StateNode>> list)
    {
        StateNode args = StateNode::Object();
        for (const auto& [k, v] : list)
            args[k] = v;
        return args;
    }

    World _world;
    std::shared_ptr<FakeLauncher> _launcher;
};

TEST_F(StreamDeckApi_Test, WithoutUnrealQtOnlyStatusAndUdevAnswer)
{
    Reply status = Call("status", StateNode::Object());
    ASSERT_TRUE(status.Ok());
    EXPECT_FALSE(status.body.find("status")->find("attached")->b);
    EXPECT_EQ(Str(*status.body.find("status"), "state"), "detached");
    EXPECT_TRUE(Call("udev", StateNode::Object()).Ok());
    for (const char* verb : {"start", "stop", "settings", "pair", "brightness"})
    {
        const Reply r = Call(verb, StateNode::Object());
        EXPECT_EQ(r.status, 503) << verb;
        EXPECT_EQ(Str(r.body, "error"), "supervisor_unavailable") << verb;
    }
    EXPECT_EQ(Call("nonsense", StateNode::Object()).status, 400);
    EXPECT_EQ(Verbs().size(), 11u);
}

TEST_F(StreamDeckApi_Test, SettingsReadAndChange)
{
    Attach(Mode::Off);
    Reply r = Call("settings", StateNode::Object());
    ASSERT_TRUE(r.Ok());
    EXPECT_EQ(Str(*r.body.find("settings"), "mode"), "off");
    EXPECT_EQ(Int(*r.body.find("settings"), "api_port"), 37053);
    r = Call("settings", Args({{"mode", "always"}, {"activity", "line"}, {"viewers_port", "37250"}}));
    ASSERT_TRUE(r.Ok()) << StateNodeToJsonText(r.body);
    EXPECT_EQ(Str(*r.body.find("settings"), "mode"), "always");
    EXPECT_EQ(Str(*r.body.find("settings"), "activity"), "line");
    EXPECT_EQ(Int(*r.body.find("settings"), "viewers_port"), 37250);
    EXPECT_EQ(Call("settings", Args({{"activity", "loud"}})).status, 400);
    EXPECT_EQ(Call("settings", Args({{"colour", "red"}})).status, 400);
}

TEST_F(StreamDeckApi_Test, PairAndBrightnessGoToTheRunningDaemon)
{
    Attach(Mode::Always);
    EXPECT_EQ(Call("pair", StateNode::Object()).status, 409) << "nothing runs yet";
    Supervisor::Instance().Step(0);
    Supervisor::Instance().Step(200);
    Reply r = Call("pair", StateNode::Object());
    ASSERT_TRUE(r.Ok()) << StateNodeToJsonText(r.body);
    EXPECT_EQ(Str(*r.body.find("pairing"), "state"), "requesting");
    EXPECT_TRUE(_world.Called("POST /api/v1/pair"));

    r = Call("brightness", Args({{"value", "40"}}));
    ASSERT_TRUE(r.Ok()) << StateNodeToJsonText(r.body);
    EXPECT_TRUE(_world.Called("PUT /api/v1/decks/first/brightness {\"value\":40}"));
    EXPECT_EQ(Supervisor::Instance().GetSettings().brightness.at("A1B2C3"), 40) << "kept for that deck";
    EXPECT_EQ(Call("brightness", Args({{"value", "140"}})).status, 400);
}

TEST_F(StreamDeckApi_Test, RevokeTakesTheDaemonsOwnCompanion)
{
    auto& registry = companion::CompanionRegistry::Instance();
    ASSERT_EQ(registry.AddFromSpec("Stream Deck daemon:unc_streamdeck-daemon-token-0001:all"), 1u);
    const auto info = registry.Find("Stream Deck daemon");
    ASSERT_TRUE(info);
    _world.pairing = R"({"state":"paired","paired":true,"companion_id":")" + info->id + R"("})";
    Attach(Mode::Always);
    EXPECT_EQ(Call("revoke", StateNode::Object()).status, 404) << "the daemon does not answer: whose companion?";
    Supervisor::Instance().Step(0);
    Supervisor::Instance().Step(200);
    const Reply r = Call("revoke", StateNode::Object());
    ASSERT_TRUE(r.Ok()) << StateNodeToJsonText(r.body);
    EXPECT_EQ(Str(r.body, "id"), info->id);
    EXPECT_FALSE(registry.Find(info->id));
    EXPECT_EQ(Call("revoke", Args({{"id", "c-000000000000"}})).status, 404);
}

TEST_F(StreamDeckApi_Test, StartStopAndRestartAreRequestsTheNextStepApplies)
{
    Attach(Mode::Off);
    Reply r = Call("start", StateNode::Object());
    ASSERT_TRUE(r.Ok());
    EXPECT_EQ(Str(r.body, "requested"), "start");
    Supervisor::Instance().Step(0);
    EXPECT_EQ(_world.starts.size(), 1u);
    ASSERT_TRUE(Call("stop", StateNode::Object()).Ok());
    Supervisor::Instance().Step(100);
    EXPECT_TRUE(_world.Called("POST /api/v1/quit"));
}

TEST_F(StreamDeckApi_Test, TheUdevRuleIsTheShippedOnes)
{
    const Reply r = Call("udev", StateNode::Object());
    ASSERT_TRUE(r.Ok());
    const std::string rule = Str(r.body, "rule");
    const auto file = TestPathHelper::FindProjectRoot() / "companions" / "streamdeck" / "linux" /
                      "70-unreal-streamdeck.rules";
    std::ifstream in(file);
    ASSERT_TRUE(in) << file;
    std::string line;
    int rules = 0;
    while (std::getline(in, line))
        if (!line.empty() && line[0] != '#')
        {
            ++rules;
            EXPECT_NE(rule.find(line), std::string::npos) << "the page shows every rule of the file: " << line;
        }
    EXPECT_EQ(rules, 2);
}
