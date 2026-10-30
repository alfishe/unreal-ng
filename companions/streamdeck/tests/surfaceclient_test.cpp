// The daemon's surface client (sdb/surfaceclient.h) against a fake surface server on the same event loop (no host,
// no hardware): join per deck and welcome, the readouts into the faces and the strip, actions with rid and result
// (the executor from another thread, the REST fallback without a socket), ctx, target, activity, a second deck and
// its bye, a refused view target, reconnect after a drop, revoked and a new token. Then the pieces it feeds: readout
// paths, a scenario's needs, the runtime's live state and the strip's timeline.
#include "sdb/surfaceclient.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "check.h"
#include "sdb/actions.h"
#include "sdb/eventloop.h"
#include "sdb/navigator.h"
#include "sdb/runtime.h"
#include "sdb/scenario.h"
#include "sdb/scenariorender.h"
#include "sdb/surfacereadouts.h"
#include "wstestserver.h"

using namespace sdb;
using namespace std::chrono_literals;
using json = nlohmann::json;
using test::RunUntil;
using test::TestServer;

namespace
{

// What the client hands a deck
struct FakeSink final : surface::DeckSink
{
    bool live = false;
    std::map<std::string, json> readouts, strip;
    json target;
    std::vector<std::string> contexts;
    std::vector<std::pair<std::string, std::string>> activity;
    std::string notice;
    std::string own;  // the deck's own context
    scn::SurfaceNeeds needs;

    void SetLive(bool l) override
    {
        live = l;
    }
    void SetReadout(const std::string& path, const json& value) override
    {
        readouts[path] = value;
    }
    void SetStripReadout(const std::string& name, const json& value) override
    {
        strip[name] = value;
    }
    void SetTarget(const json& t) override
    {
        target = t;
    }
    void OnContext(const std::string& context) override
    {
        contexts.push_back(context);
    }
    void ShowActivity(const std::string& id, const std::string& text) override
    {
        activity.emplace_back(id, text);
    }
    void SetNotice(const std::string& text) override
    {
        notice = text;
    }
    [[nodiscard]] std::string Context() const override
    {
        return own;
    }
    [[nodiscard]] scn::SurfaceNeeds Needs() const override
    {
        return needs;
    }
};

// The surface topic of /api/v1/ws as far as the client uses it
struct FakeHost
{
    TestServer server;
    std::vector<json> got;  // every message, parsed
    int sessions = 0;
    std::map<std::string, std::string> serialOf;  // session -> serial
    bool refuseViews = true;

    explicit FakeHost(net::EventLoop& loop) : server(loop)
    {
        server.onText = [this](TestServer::Conn* c, const std::string& text) { OnText(c, text); };
    }

    void OnText(TestServer::Conn* c, const std::string& text)
    {
        const json j = json::parse(text, nullptr, false);
        got.push_back(j);
        const std::string op = j.value("op", "");
        if (op == "subscribe" && j.contains("topics"))
            Send(c, {{"t", "instances"}, {"op", "hello"}, {"seq", 1}, {"emulators", {{{"id", "i1"}}}}});
        else if (op == "join")
        {
            if (refuseViews && j["target"].is_object() && j["target"].contains("view"))
            {
                Send(c, {{"op", "error"},
                         {"t", "surface"},
                         {"rid", j["rid"]},
                         {"error", "not_now"},
                         {"message", "views come with SD4"}});
                return;
            }
            const std::string session = "s" + std::to_string(++sessions);
            serialOf[session] = j.value("serial", "");
            Send(c, {{"op", "welcome"},
                     {"t", "surface"},
                     {"session", session},
                     {"rid", j["rid"]},
                     {"v", 1},
                     {"instance", "i1"},
                     {"context", "play"},
                     {"target", Target("i1", 0)},
                     {"features", {"target", "activity", "catalogue"}}});
        }
        else if (op == "subscribe" && j.contains("readouts"))
            Send(c, {{"op", "readout"},
                     {"t", "surface"},
                     {"session", j["session"]},
                     {"instance", "i1"},
                     {"values",
                      {{"run.state", {{"state", "paused"}, {"speed", 1}, {"turbo", false}}},
                       {"ttd.position", {{"frame", 120}, {"first", 0}, {"end", 500}, {"history", true}}}}}});
        else if (op == "action" && j.contains("rid"))
        {
            json body = {{"echo", j.value("value", json())}};
            if (j.value("id", "") == "ttd.seek_by")
                body["marks"] = {{{"kind", "second"}, {"frame", 100}}};
            Send(c, {{"op", "result"},
                     {"t", "surface"},
                     {"session", j["session"]},
                     {"rid", j["rid"]},
                     {"ok", true},
                     {"body", body}});
        }
    }

    static json Target(const std::string& instance, int index)
    {
        return {{"instance", instance}, {"index", index},  {"model", "PENTAGON"},
                {"ram_kb", 128},        {"view", nullptr}, {"mode", "follow"}};
    }

    void Send(TestServer::Conn* c, const json& j)
    {
        server.SendText(c, j.dump());
    }
    void Push(const json& j)
    {
        if (TestServer::Conn* c = server.Last())
            Send(c, j);
    }
    // The messages of op `op` (and of a session)
    std::vector<json> Ops(const std::string& op, const std::string& session = {}) const
    {
        std::vector<json> out;
        for (const auto& j : got)
            if (j.value("op", "") == op && (session.empty() || j.value("session", "") == session))
                out.push_back(j);
        return out;
    }
};

std::shared_ptr<FakeSink> Sink(bool timeline = true)
{
    auto s = std::make_shared<FakeSink>();
    s->needs.readouts = {"run.state", "ttd.position"};
    s->needs.paths = {"run.state.paused"};
    s->needs.timeline = timeline;
    return s;
}

surface::Options Options(const FakeHost& host, const std::string& companionFile = {})
{
    surface::Options o;
    o.url = host.server.Url();
    o.token = "unc_deck";
    o.companionFile = companionFile;
    o.reconnectMin = 1ms;
    o.reconnectMax = 2ms;
    o.pingInterval = 0ms;
    o.fileCheck = 1ms;
    o.actionTimeout = 500ms;
    o.catalogueFile = "-";  // none: labels are the ids
    return o;
}

void TestJoinReadoutsActions()
{
    net::EventLoop loop;
    FakeHost host(loop);
    surface::Client client(loop, Options(host));
    auto deck = Sink();
    client.AddDeck({"A1", "plusxl", "Stream Deck + XL"}, deck);
    client.Start();
    CHECK(RunUntil(loop, [&] { return deck->live && !deck->readouts.empty(); }));

    // the handshake carries the token; the instances topic; one join per deck with its serial, model, features
    CHECK(!host.server.conns.empty() &&
          host.server.conns[0]->request.find("Authorization: Bearer unc_deck") != std::string::npos);
    CHECK(!host.Ops("subscribe").empty() && host.Ops("subscribe")[0]["topics"][0] == "instances");
    const auto joins = host.Ops("join");
    CHECK(joins.size() == 1 && joins[0]["serial"] == "A1" && joins[0]["model"] == "plusxl" &&
          joins[0]["name"] == "Stream Deck + XL" && joins[0]["target"] == "follow" && joins[0]["v"] == 1);
    CHECK(joins.size() == 1 && joins[0]["features"].size() == 3);
    // readouts: what the deck needs, at the timeline's rate, for its session
    const auto subs = host.Ops("subscribe", "s1");
    CHECK(subs.size() == 1 && subs[0]["readouts"].size() == 2 && subs[0]["max_hz"] == 50);
    // the face's path from run.state; the strip's readouts whole; the target
    CHECK(deck->readouts["run.state.paused"] == true);
    CHECK(deck->strip.count("ttd.position") && deck->strip["ttd.position"]["frame"] == 120);
    CHECK(deck->target.value("instance", "") == "i1");
    CHECK(client.Connected() && client.Joined("A1") && client.Status()["decks"][0]["session"] == "s1");
    CHECK(client.Instances().size() == 1);

    // an action: rid, the session, the value as the socket takes it (params beside it); the result comes back
    scn::ActionResult result;
    json body;
    bool done = false;
    client.Act("A1", {"ttd.seek_by", {{"delta", -3}, {"rate", 12}}, {{"unit", "frames"}}},
               [&](const scn::ActionResult& r, const json& b) {
                   result = r;
                   body = b;
                   done = true;
               });
    CHECK(RunUntil(loop, [&] { return done; }));
    const auto acts = host.Ops("action", "s1");
    CHECK(acts.size() == 1 && acts[0]["id"] == "ttd.seek_by" && acts[0]["rid"].is_number_integer() &&
          acts[0]["value"]["delta"] == -3 && acts[0]["value"]["params"]["unit"] == "frames");
    CHECK(result.ok && !result.notSent && result.message.find("socket") != std::string::npos);
    CHECK(deck->strip.count("ttd.marks") && deck->strip["ttd.marks"][0]["frame"] == 100);  // the seek's marks

    // the executor, from the runtime's action thread: waits for the loop's answer
    auto exec = client.Executor("A1");
    std::atomic<bool> started{false}, finished{false};
    scn::ActionResult threaded;
    std::thread worker([&] {
        started = true;
        threaded = exec->Run({"run.pause_toggle", nullptr, json::object()});
        finished = true;
    });
    CHECK(RunUntil(loop, [&] { return started && finished; }));
    worker.join();
    CHECK(threaded.ok && host.Ops("action", "s1").size() == 2 && !host.Ops("action")[1].contains("value"));

    // ctx -> the deck; activity -> the deck's look
    host.Push(
        {{"op", "ctx"}, {"t", "surface"}, {"session", "s1"}, {"instance", "i1"}, {"context", "ttd"}, {"by", "gui"}});
    host.Push({{"op", "activity"},
               {"t", "surface"},
               {"session", "s1"},
               {"instance", "i1"},
               {"id", "run.pause_toggle"},
               {"origin", "host_device"},
               {"surface", "Xbox pad"},
               {"ok", true}});
    CHECK(RunUntil(loop, [&] { return !deck->contexts.empty() && !deck->activity.empty(); }));
    CHECK(deck->contexts.back() == "ttd");
    CHECK(deck->activity[0].first == "run.pause_toggle" &&
          deck->activity[0].second.find("Xbox pad") != std::string::npos);

    // a new instance (machine replaced): target, then its default ctx; the deck keeps its page and says so
    deck->own = "ttd";
    host.Push({{"op", "target"},
               {"t", "surface"},
               {"session", "s1"},
               {"instance", "i2"},
               {"index", 0},
               {"model", "SPRINTER"},
               {"ram_kb", 4096},
               {"view", nullptr},
               {"mode", "follow"}});
    host.Push({{"op", "ctx"},
               {"t", "surface"},
               {"session", "s1"},
               {"instance", "i2"},
               {"context", "play"},
               {"by", "default"}});
    CHECK(RunUntil(loop, [&] { return host.Ops("action", "s1").size() == 3; }));
    CHECK(deck->target["instance"] == "i2" && deck->target["model"] == "SPRINTER" && !deck->target.contains("op"));
    CHECK(host.Ops("action", "s1")[2]["id"] == "ctx.set" && host.Ops("action", "s1")[2]["value"] == "ttd");
    CHECK(deck->contexts.size() == 1);  // not navigated away

    // a second deck on the same connection: its own join and session; when it goes, bye for that session
    auto second = Sink(false);
    client.AddDeck({"B2", "plus", "Stream Deck +"}, second);
    CHECK(RunUntil(loop, [&] { return second->live && host.Ops("subscribe", "s2").size() == 1; }));
    CHECK(host.Ops("join").size() == 2 && host.Ops("join")[1]["serial"] == "B2");
    CHECK(host.Ops("subscribe", "s2").size() == 1 && host.Ops("subscribe", "s2")[0]["max_hz"] == 10);
    CHECK_EQ(host.server.accepted, 1);  // one connection for both
    client.RemoveDeck("B2");            // on the loop thread: at once
    CHECK(RunUntil(loop, [&] { return host.Ops("bye", "s2").size() == 1; }));
    CHECK(!client.Joined("B2"));
    auto gone = client.Executor("B2")->Run({"run.pause_toggle", nullptr, json::object()});
    CHECK(gone.notSent);  // no session: the system host's REST fallback takes it
    // paired: its context waits for the join (the join sends it), no REST call to a host that may not listen yet
    auto early = client.Executor("B2")->Run({"ctx.set", "ttd", json::object()});
    CHECK(early.ok && !early.notSent);

    // a view the host cannot bind: the deck follows instead
    auto viewer = Sink(false);
    viewer->needs.target = {{"view", 1}};
    client.AddDeck({"C3", "xl", "Stream Deck XL"}, viewer);
    CHECK(RunUntil(loop, [&] { return viewer->live; }));
    const auto viewJoins = host.Ops("join");
    CHECK(viewJoins.size() == 4 && viewJoins[2]["target"]["view"] == 1 && viewJoins[3]["target"] == "follow");

    // the host goes away (unreal-qt restarts): live off, back with a new connection, every deck joined again
    host.sessions = 10;
    for (auto& c : host.server.conns)
        host.server.Kill(c.get());
    CHECK(RunUntil(loop, [&] { return !deck->live; }));
    CHECK(!client.Joined("A1"));
    CHECK(RunUntil(loop, [&] { return deck->live && viewer->live; }));
    CHECK(client.Status()["connections"] == 2);
    CHECK(client.Status()["decks"][0]["session"].get<std::string>() != "s1");
    client.Stop();
    CHECK(RunUntil(loop, [&] { return !client.Connected(); }));
    CHECK(host.Ops("bye").size() >= 3);  // B2's, then A1's and C3's
}

void TestRevokedAndNewToken()
{
    net::EventLoop loop;
    FakeHost host(loop);
    const auto file = std::filesystem::temp_directory_path() / "sdb-surface-revoke-test.json";
    std::filesystem::remove(file);
    surface::Client client(loop, Options(host, file.string()));
    auto deck = Sink(false);
    client.AddDeck({"A1", "plusxl", "Stream Deck + XL"}, deck);
    client.Start();
    CHECK(RunUntil(loop, [&] { return deck->live; }));

    host.Push({{"op", "revoked"}, {"t", "surface"}, {"message", "the user revoked this companion"}});
    CHECK(RunUntil(loop, [&] { return !deck->live; }));
    CHECK(deck->notice == "COMPANION REVOKED - TOUCH TO PAIR");
    CHECK(client.Status()["revoked"] == true && !client.Connected());
    auto r = client.Executor("A1")->Run({"run.pause_toggle", nullptr, json::object()});
    CHECK(r.notSent);
    const int accepted = host.server.accepted;

    // pairing again writes a new token: the client connects with it, the notice goes
    {
        std::ofstream out(file);
        out << json{{"webapi", "http://127.0.0.1:8090"}, {"token", "unc_new"}, {"companion_id", "c-2"}}.dump();
    }
    CHECK(RunUntil(loop, [&] { return deck->live; }));
    CHECK(host.server.accepted == accepted + 1 && deck->notice.empty());
    CHECK(host.server.Last() && host.server.Last()->request.find("Bearer unc_new") != std::string::npos);
    client.Stop();
    CHECK(RunUntil(loop, [&] { return !client.Connected(); }));
    std::filesystem::remove(file);

    // a refused handshake (401: a token the host no longer knows) is a revocation too, not a reconnect loop
    host.server.mode = TestServer::Mode::Refuse;
    surface::Client refused(loop, Options(host));
    auto other = Sink(false);
    refused.AddDeck({"Z9", "plus", "Stream Deck +"}, other);
    refused.Start();
    CHECK(RunUntil(loop, [&] { return !other->notice.empty(); }));
    CHECK(refused.Status()["revoked"] == true);
}

void TestNotPaired()
{
    net::EventLoop loop;
    FakeHost host(loop);
    surface::Options o = Options(host);
    o.token.clear();
    surface::Client client(loop, o);
    auto deck = Sink(false);
    client.AddDeck({"A1", "plusxl", "Stream Deck + XL"}, deck);
    client.Start();
    CHECK(RunUntil(loop, [&] { return client.Status()["paired"] == false && !client.Status()["error"].is_null(); }));
    CHECK_EQ(host.server.accepted, 0);
    bool done = false;
    client.Act("A1", {"run.pause_toggle", nullptr, json::object()},
               [&](const scn::ActionResult& r, const json&) { done = r.notSent; });
    CHECK(done);
    CHECK(client.Executor("A1")->Run({"ctx.set", "ttd", json::object()}).notSent);  // no join to wait for: REST
}

void TestReadoutPathsAndNeeds()
{
    using scn::SplitReadoutPath;
    CHECK(SplitReadoutPath("run.state.paused").name == "run.state" &&
          SplitReadoutPath("run.state.paused").field == "paused");
    CHECK(SplitReadoutPath("toggle.mach.turbo_toggle").name == "toggle.mach.turbo_toggle");
    CHECK(SplitReadoutPath("toggle.feature.sound.on").field == "on");
    CHECK(SplitReadoutPath("audio.mute.muted").name == "audio.mute");
    CHECK(SplitReadoutPath("media.cart.loaded").name == "media.cart");
    const json run{{"state", "paused"}, {"turbo", true}};
    CHECK(scn::ReadoutField(SplitReadoutPath("run.state.paused"), run) == true);
    CHECK(scn::ReadoutField(SplitReadoutPath("run.state.running"), run) == false);
    CHECK(scn::ReadoutField(SplitReadoutPath("run.state.turbo"), run) == true);
    CHECK(scn::ReadoutField(SplitReadoutPath("run.state.nothing"), run).is_null());

    std::vector<scn::Issue> issues;
    auto s = scn::Parse(json::parse(R"({"format":"streamdeck-scenario","version":2,"model":"plusxl",
      "settings":{"target":{"view":2}},
      "root":{"id":"home","type":"menu","strip":{"status":"tape"},"items":[
        {"id":"mute","type":"action","at":[0,0],"face_on":{"label":"M"},"state":"audio.mute.muted",
         "do":{"kind":"action","id":"audio.mute_toggle"}},
        {"id":"t","type":"menu","at":[0,1],"strip":{"status":"ttd"},"items":[]}]}})"),
                        issues);
    CHECK(s.has_value());
    if (s)
    {
        const scn::SurfaceNeeds n = scn::NeedsOf(*s);
        // an action key: the actions the host cannot run now (its key greys out)
        CHECK(n.readouts ==
              (std::vector<std::string>{"actions.unavailable", "audio.mute", "media.tape", "ttd.position"}));
        CHECK(n.paths == (std::vector<std::string>{"actions.unavailable", "audio.mute.muted"}));
        CHECK(n.timeline && n.target["view"] == 2);
    }
}

// The pixels of a colour close to `c`
int Count(const Rgb& im, draw::Color c, int x0, int x1, int y0, int y1)
{
    int n = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
        {
            const std::uint8_t* p = im.At(x, y);
            n += std::abs(p[0] - c.r) < 8 && std::abs(p[1] - c.g) < 8 && std::abs(p[2] - c.b) < 8;
        }
    return n;
}
int Pink(const Rgb& im)
{
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
        {
            const std::uint8_t* p = im.At(x, y);
            n += p[0] > 170 && p[2] > 110 && p[1] < 110;  // magenta / pink: red and blue, little green
        }
    return n;
}

void TestStripTimeline()
{
    std::vector<scn::Issue> issues;
    auto s = scn::Parse(json::parse(R"({"format":"streamdeck-scenario","version":2,"model":"plusxl",
      "settings":{"navigation":{"breadcrumbs":"none"}},
      "root":{"id":"home","type":"menu","strip":{"status":"ttd"},"items":[]}})"),
                        issues);
    CHECK(s.has_value());
    if (!s)
        return;
    const Model& m = *FindModelByName("plusxl");
    scn::Navigator nav(*s);
    const scn::Page page = scn::Layout(*s, nav, m);
    std::map<std::string, json> readouts{
        {"ttd.position",
         {{"frame", 250}, {"first", 0}, {"end", 1000}, {"state", "detached"}, {"history", true}, {"fps", 50}}},
        {"ttd.marks", {{{"kind", "bookmark"}, {"frame", 500}}}}};
    const json target = {{"instance", "i1"}, {"index", 1}, {"model", "PENTAGON"}, {"ram_kb", 128}, {"mode", "follow"}};
    scn::StripLive live;
    live.link = scn::StripLive::Link::Live;
    live.readouts = &readouts;
    live.target = &target;
    const Rgb im = scn::RenderStrip(page, m.lcdLogicalW, m.lcdLogicalH, m.dialTileW, {}, -1, {}, {}, &live);
    // the capsule under the header (scale 3 on the + XL strip): the played part (a blue-to-cyan gradient) up to a
    // quarter, the position (a light pill), the bookmark (amber, a diamond over the capsule)
    const int barY = 8 + 7 * 3 + 12, barH = 16;
    const int x0 = 12, x1 = m.lcdLogicalW - 12, quarter = x0 + (x1 - x0 - 1) / 4;
    auto playedIn = [&](const Rgb& pic, int xa, int xb) {
        int n = 0;
        for (int y = barY; y < barY + barH; ++y)
            for (int x = xa; x < xb; ++x)
            {
                const std::uint8_t* p = pic.At(x, y);
                n += p[2] > 120 && p[2] > p[0] + 60;  // blue, little red
            }
        return n;
    };
    auto played = [&](int xa, int xb) { return playedIn(im, xa, xb); };
    CHECK(played(x0 + 8, quarter - 6) > (quarter - 6 - x0 - 8) * barH * 3 / 4);
    CHECK(played(quarter + 8, x1) == 0);
    CHECK(Count(im, {235, 240, 250}, quarter - 3, quarter + 4, barY - 7, barY + barH + 7) > 20);
    const int half = x0 + (x1 - x0 - 1) / 2;
    CHECK(Count(im, {200, 160, 60}, half - 3, half + 4, barY - 9, barY + barH + 2) >= 5);
    const int track = barY;
    CHECK(Count(im, {90, 200, 230}, x0, x1, 0, track - 12) > 50);  // the frame counter
    CHECK(Pink(im) == 0);
    CHECK(scn::TargetLine(target) == "PENTAGON 128K (1)" && scn::TargetLine(nullptr) == "NO MACHINE");

    // the link lost: the target line says so, the timeline is not drawn from a stale readout
    live.link = scn::StripLive::Link::Lost;
    const Rgb lost = scn::RenderStrip(page, m.lcdLogicalW, m.lcdLogicalH, m.dialTileW, {}, -1, {}, {}, &live);
    CHECK(playedIn(lost, x0, x1) == 0);
    CHECK(Count(lost, {220, 170, 60}, x1 - 260, x1, 0, 40) > 20);  // RECONNECTING, amber
    // the revoked notice over everything
    live.link = scn::StripLive::Link::None;
    live.notice = "COMPANION REVOKED - TOUCH TO PAIR";
    const Rgb revoked = scn::RenderStrip(page, m.lcdLogicalW, m.lcdLogicalH, m.dialTileW, {}, -1, {}, {}, &live);
    CHECK(Count(revoked, {235, 215, 205}, 0, m.lcdLogicalW, 0, m.lcdLogicalH) > 100);
    CHECK(Pink(revoked) == 0);
}

}  // namespace

void TestSurfaceClient()
{
    std::puts("surface client");
    TestJoinReadoutsActions();
    TestRevokedAndNewToken();
    TestNotPaired();
    TestReadoutPathsAndNeeds();
    TestStripTimeline();
}
