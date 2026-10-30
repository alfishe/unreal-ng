// The daemon's life (sdb/daemonlife.h) and its pairing (sdb/pairing.h): one daemon per user (the lock, its holder,
// a second start that adopts the running one by handing it its parent), the parents' leases (a parent gone or
// released ends a daemon its parents started, never one started by hand), the routes on the automation planes; the
// pairing against a fake host (pending, approved: the token in the companion file; refused, unreachable), the
// strip's lines, and the deck's tap on a notice.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <span>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "check.h"
#include "sdb/automation.h"
#include "sdb/daemonlife.h"
#include "sdb/deckpages.h"
#include "sdb/device.h"
#include "sdb/models.h"
#include "sdb/pairing.h"
#include "sdb/runtime.h"
#include "sdb/scenario.h"
#include "sdb/surfaceclient.h"
#include "sdb/transport.h"

using namespace sdb;
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace
{

struct NullTransport final : Transport
{
    int Write(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int SendFeature(std::span<const std::uint8_t> r) override
    {
        return static_cast<int>(r.size());
    }
    int GetFeature(std::span<std::uint8_t> b) override
    {
        return static_cast<int>(b.size());
    }
    int Read(std::span<std::uint8_t>, int) override
    {
        return 0;
    }
};

fs::path Scratch(const std::string& name)
{
    static int counter = 0;
    const fs::path dir = fs::temp_directory_path() /
                         ("sdb-life-" + std::to_string(life::CurrentPid()) + "-" + std::to_string(++counter));
    fs::create_directories(dir);
    return dir / name;
}

// Until `done` (bounded: a broken case fails instead of hanging)
template <class F>
bool Until(F done, int ms = 3000)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!done())
    {
        if (std::chrono::steady_clock::now() > end)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

automation::Request Req(const std::string& method, const std::string& path, json body = nullptr)
{
    automation::Request r;
    r.method = method;
    r.path = path;
    r.body = std::move(body);
    return r;
}

void TestLock()
{
    const std::string path = Scratch("unreal-streamdeck.lock").string();
    life::InstanceLock first;
    bool busy = true;
    std::string error;
    CHECK(first.Acquire(path, &busy, &error));
    CHECK(!busy && first.Held());
    auto holder = life::ReadHolder(path);
    CHECK(holder && holder->pid == life::CurrentPid() && holder->api == 0);  // written at once, the ports later

    // a second start finds it held: busy, and reads who
    life::InstanceLock second;
    CHECK(!second.Acquire(path, &busy, &error));
    CHECK(busy && !second.Held());
    life::Holder self;
    self.pid = life::CurrentPid();
    self.api = 4321;
    self.viewers = 4320;
    self.version = "1.0";
    self.host = "http://127.0.0.1:8090";
    CHECK(first.Publish(self));
    holder = life::ReadHolder(path);
    CHECK(holder && holder->api == 4321 && holder->viewers == 4320 && holder->host == "http://127.0.0.1:8090");

    // released (the daemon ends): the next start takes it; a stale holder is never read
    first.Release();
    CHECK(second.Acquire(path, &busy, &error));
    CHECK(life::ReadHolder(path) && life::ReadHolder(path)->api == 0);
    second.Release();
    CHECK(!life::ReadHolder(Scratch("none.lock").string()));
}

void TestParents()
{
    // started by its parents: the last lease gone ends it
    life::Parents p(false);
    CHECK(!p.Poll());  // no parent yet: nothing to end
    std::string error;
    CHECK(p.Add(life::CurrentPid(), &error));
    CHECK(p.Add(life::CurrentPid()));  // again: the same lease
    CHECK(!p.Add(0, &error) && !error.empty());
    CHECK(p.List().size() == 1);
    CHECK(!p.Poll());
    CHECK(!p.Remove(12345678));
    CHECK(p.Remove(life::CurrentPid()));
    CHECK(p.Poll());

    // started by hand: its leases come and go, it stays
    life::Parents alone(true);
    CHECK(alone.Add(life::CurrentPid()));
    CHECK(alone.Remove(life::CurrentPid()));
    CHECK(!alone.Poll());

#ifndef _WIN32
    // a parent that dies: its process handle says so
    int pipeFd[2];
    CHECK(pipe(pipeFd) == 0);
    const pid_t child = fork();
    if (child == 0)
    {
        close(pipeFd[1]);
        char byte = 0;
        (void)!read(pipeFd[0], &byte, 1);  // until the parent closes its end
        _exit(0);
    }
    close(pipeFd[0]);
    life::Parents q(false);
    CHECK(q.Add(static_cast<int>(child)));
    CHECK(life::ProcessAlive(static_cast<int>(child)));
    CHECK(!q.Poll());
    close(pipeFd[1]);
    int status = 0;
    waitpid(child, &status, 0);
    std::vector<int> gone;
    CHECK(Until([&] { return q.Poll(&gone); }));
    CHECK(gone.size() == 1 && gone[0] == static_cast<int>(child));
    CHECK(!life::ProcessAlive(static_cast<int>(child)));
    CHECK(!q.Add(static_cast<int>(child)));  // gone: no lease for it
#endif
}

void TestLifeRoutesAndAdopt()
{
    automation::Api api("test", "1.0");
    life::Parents parents(false);
    bool quit = false;
    life::AddLifeRoutes(api, parents, [&quit] { quit = true; });
    CHECK(api.Dispatch(Req("POST", "/api/v1/parents", json{{"pid", -1}})).status == 400);
    CHECK(api.Dispatch(Req("POST", "/api/v1/parents", json{{"pid", 0x7FFFFFF0}})).status == 404);
    CHECK(api.Dispatch(Req("GET", "/api/v1/parents")).body["parents"].empty());

    automation::Server server;
    std::string error;
    CHECK(server.Start(api, "127.0.0.1", 0, &error));
    const std::string path = Scratch("unreal-streamdeck.lock").string();
    life::InstanceLock lock;
    CHECK(lock.Acquire(path));
    // still starting: the holder has no port yet, a second start waits for it (bounded)
    const auto early = life::Adopt(path, 0, 60, &error);
    CHECK(early && early->api == 0 && !error.empty());  // it runs; no automation to hand a lease to
    life::Holder self;
    self.pid = life::CurrentPid();
    self.api = server.Port();
    CHECK(lock.Publish(self));
    // a second start with --parent: the running daemon gets the lease, the start ends
    auto holder = life::Adopt(path, life::CurrentPid(), 1000, &error);
    CHECK(holder && holder->api == server.Port());
    CHECK(parents.List().size() == 1 && parents.List()[0] == life::CurrentPid());
    // the lease given back: the last one, the daemon ends
    const auto released = api.Dispatch(Req("DELETE", "/api/v1/parents/" + std::to_string(life::CurrentPid())));
    CHECK(released.status == 200 && released.body["ending"] == true);
    CHECK(api.Dispatch(Req("DELETE", "/api/v1/parents/" + std::to_string(life::CurrentPid()))).status == 404);
    CHECK(api.Dispatch(Req("POST", "/api/v1/quit")).status == 200 && quit);
    server.Stop();
    lock.Release();
    CHECK(!life::Adopt(Scratch("nobody.lock").string(), 0, 30, &error));
}

// The host's pairing routes, as unreal-qt answers them (companion protocol §3)
struct FakeHost
{
    automation::Api api{"host", "1.0"};
    automation::Server server;
    int polls = 0, approveAfter = 2, requests = 0;
    int requestStatus = 202;
    std::string requestError;
    bool cancelled = false;

    FakeHost()
    {
        api.Add({"POST", "/api/v1/companions/pairings", "", "", {}, {}, nullptr, [this](const automation::Request& r) {
                     ++requests;
                     if (requestStatus != 202)
                         return automation::Reply{requestStatus, json{{"error", requestError}, {"message", "no"}}, {}, {}};
                     const bool named = r.body.is_object() && r.body.value("name", "") == "Stream Deck daemon";
                     return automation::Reply{202,
                                              {{"pairing_id", named ? "p-1" : "p-unnamed"},
                                               {"code", "418205"},
                                               {"expires_in", 120}},
                                              {},
                                              {}};
                 }});
        api.Add({"GET", "/api/v1/companions/pairings/{id}", "", "", {}, {}, nullptr, [this](const automation::Request&) {
                     if (++polls < approveAfter)
                         return automation::Reply::Ok({{"state", "pending"}, {"expires_in", 100}});
                     return automation::Reply::Ok({{"state", "approved"},
                                                   {"token", "unc_test-token-0123456789"},
                                                   {"companion_id", "c-0123456789ab"},
                                                   {"scopes", json::array({"run", "input"})}});
                 }});
        api.Add({"DELETE", "/api/v1/companions/pairings/{id}", "", "", {}, {}, nullptr,
                 [this](const automation::Request&) {
                     cancelled = true;
                     return automation::Reply::Ok({{"cancelled", true}});
                 }});
        std::string error;
        server.Start(api, "127.0.0.1", 0, &error);
    }
    std::string Url() const
    {
        return "http://127.0.0.1:" + std::to_string(server.Port());
    }
};

void TestPairing()
{
    FakeHost host;
    pairing::Options o;
    o.webapi = host.Url();
    o.file = Scratch("companion.json").string();
    o.pollMs = 5;
    pairing::Pairer pairer(o);
    std::vector<std::string> states;
    std::mutex statesMutex;
    pairer.onChange = [&](const json& s) {
        std::lock_guard l(statesMutex);
        states.push_back(s.value("state", ""));
    };
    CHECK(pairer.Status()["state"] == "idle" && pairer.Status()["paired"] == false);
    const json started = pairer.Start();
    CHECK(started["state"] == "requesting");
    CHECK(pairer.Start()["state"] != "idle");  // one at a time: the same pairing again
    CHECK(Until([&] { return pairer.Status()["state"] == "paired"; }));
    CHECK(host.requests == 1);
    const json done = pairer.Status();
    CHECK(done["paired"] == true && done["companion_id"] == "c-0123456789ab");
    const auto companion = surface::LoadCompanion(o.file);
    CHECK(companion && companion->token == "unc_test-token-0123456789" && companion->webapi == host.Url());
#ifndef _WIN32
    CHECK((fs::status(o.file).permissions() & fs::perms::group_all) == fs::perms::none);
    CHECK((fs::status(o.file).permissions() & fs::perms::others_all) == fs::perms::none);
#endif
    {
        std::lock_guard l(statesMutex);
        CHECK(states.size() >= 3 && states.front() == "requesting" && states.back() == "paired");
        CHECK(std::find(states.begin(), states.end(), "pending") != states.end());
    }

    // the routes: the same state; a new one asked again
    automation::Api api("daemon", "1.0");
    pairing::AddPairRoutes(api, pairer);
    CHECK(api.Dispatch(Req("GET", "/api/v1/pair")).body["state"] == "paired");

    // refused: no GUI at the host (the video wall, headless)
    host.requestStatus = 503;
    host.requestError = "pairing_unavailable";
    (void)api.Dispatch(Req("POST", "/api/v1/pair"));
    CHECK(Until([&] { return pairer.Status()["state"] == "refused"; }));
    CHECK(pairer.Status()["error"] == "pairing_unavailable");
    CHECK(pairing::NoticeOf(pairer.Status(), false, false) == "PAIRING NEEDS THE UNREALNG WINDOW");

    // a pending pairing cancelled
    host.requestStatus = 202;
    host.polls = 0;
    host.approveAfter = 1000000;
    (void)pairer.Start();
    CHECK(Until([&] { return pairer.Status()["state"] == "pending"; }));
    CHECK(pairer.Status()["code"] == "418205");
    (void)api.Dispatch(Req("DELETE", "/api/v1/pair"));
    CHECK(Until([&] { return pairer.Status()["state"] == "idle"; }));
    CHECK(host.cancelled);

    // nobody at the address
    pairing::Options gone = o;
    gone.webapi = "http://127.0.0.1:1";
    gone.file = Scratch("other.json").string();
    pairing::Pairer lonely(gone);
    (void)lonely.Start();
    CHECK(Until([&] { return lonely.Status()["state"] == "unreachable"; }));
    CHECK(lonely.Status()["paired"] == false);
}

void TestNotices()
{
    using pairing::NoticeOf;
    CHECK(NoticeOf({{"state", "idle"}}, false, false) == surface::kNotPairedNotice);
    CHECK(NoticeOf({{"state", "idle"}}, true, true) == surface::kRevokedNotice);
    CHECK(NoticeOf({{"state", "idle"}}, true, false).empty());
    CHECK(NoticeOf({{"state", "paired"}}, true, false).empty());
    CHECK(NoticeOf({{"state", "pending"}, {"code", "418205"}}, false, false) == "PAIR CODE 418 205 - APPROVE IN UNREALNG");
    CHECK(NoticeOf({{"state", "denied"}}, false, false) == "PAIRING DENIED - TOUCH TO TRY AGAIN");
    CHECK(NoticeOf({{"state", "expired"}}, false, true) == "PAIRING EXPIRED - TOUCH TO TRY AGAIN");
    CHECK(NoticeOf({{"state", "refused"}, {"error", "pairing_disabled"}}, false, false) == "PAIRING IS OFF IN UNREALNG");
    CHECK(NoticeOf({{"state", "unreachable"}}, false, false) == "UNREALNG NOT RUNNING - TOUCH TO TRY AGAIN");
}

// A tap on the strip while a notice shows pairs; without one it is the page's (the crumbs); the activity look
void TestNoticeTap()
{
    const Model& m = *FindModelByName("plusxl");
    Device dev(m, std::make_unique<NullTransport>(), "T");
    std::vector<scn::Issue> issues;
    const auto s = scn::Parse(json::parse(R"({"name": "t", "model": "plusxl",
        "root": {"id": "home", "type": "menu", "title": "Home", "items": [
            {"id": "a", "type": "action", "title": "A", "at": [0, 0], "do": {"kind": "webapi", "path": "/a"}}]}})"),
                              issues);
    CHECK(s.has_value());
    if (!s)
        return;
    scn::RuntimeOptions opt;
    opt.syncActions = true;
    scn::Runtime rt(dev, *s, nullptr, opt);
    int taps = 0;
    rt.onNoticeTap = [&taps] { ++taps; };
    rt.Step(0);
    Event tap{EventType::LcdTap};
    tap.x = 100;
    tap.y = 50;
    rt.Inject(tap);
    rt.Step(0);
    CHECK(taps == 0);  // no notice: the page's tap
    rt.SetNotice(surface::kNotPairedNotice);
    CHECK(rt.Notice() == surface::kNotPairedNotice);
    rt.Inject(tap);
    rt.Step(0);
    CHECK(taps == 1);
    rt.SetNotice("");
    rt.Inject(tap);
    rt.Step(0);
    CHECK(taps == 1);
}

// ACC-6: a restarted daemon opens each deck on the page it showed. The runtime reports its path on every menu
// change; the store keeps it per serial and scenario in a file another process reads back
void TestDeckPages()
{
    const std::string file = Scratch("deck-pages.json").string();
    std::vector<scn::Issue> issues;
    const auto s = scn::Parse(json::parse(R"({"name": "t", "model": "plusxl",
        "root": {"id": "home", "type": "menu", "title": "Home", "items": [
            {"id": "time", "type": "menu", "title": "Time", "at": [0, 1], "items": [
                {"id": "a", "type": "action", "title": "A", "at": [1, 1], "do": {"kind": "webapi", "path": "/a"}}]}]}})"),
                              issues);
    CHECK(s.has_value());
    if (!s)
        return;
    const Model& m = *FindModelByName("plusxl");
    scn::RuntimeOptions opt;
    opt.syncActions = true;
    {
        DeckPages pages(file);
        Device dev(m, std::make_unique<NullTransport>(), "S1");
        scn::Runtime rt(dev, *s, nullptr, opt);
        rt.RestorePath(pages.Get("S1", s->name));
        rt.onPathChanged = [&pages](const std::string& scenario, const std::vector<std::string>& ids) {
            pages.Set("S1", scenario, ids);
        };
        rt.Navigate({{"to", "node"}, {"node", "time"}});
        rt.Step(0);
        CHECK(rt.State()["path"] == json::array({"Home", "Time"}));
    }
    DeckPages again(file);  // the restarted daemon
    CHECK(again.Get("S1", "t") == std::vector<std::string>({"home", "time"}));
    CHECK(again.Get("S1", "other").empty());  // another scenario: its home
    CHECK(again.Get("S2", "t").empty());
    Device dev(m, std::make_unique<NullTransport>(), "S1");
    scn::Runtime rt(dev, *s, nullptr, opt);
    rt.RestorePath(again.Get("S1", s->name));
    CHECK(rt.State()["path"] == json::array({"Home", "Time"}));
    rt.RestorePath({"home", "gone"});  // a menu the scenario no longer has: the prefix that exists
    CHECK(rt.State()["path"] == json::array({"Home"}));
}
}  // namespace

void TestDaemonLife()
{
    TestDeckPages();
    std::printf("daemon life, pairing\n");
    TestLock();
    TestParents();
    TestLifeRoutesAndAdopt();
    TestPairing();
    TestNotices();
    TestNoticeTap();
}
