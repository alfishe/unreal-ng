// Runtime on a mock transport: rendering only what changed, menus, back key (press / hold), strip crumbs, swipe,
// leaf "then", dials, toggles, failures, reload keeping the stack, the state JSON.
#include "sdb/runtime.h"

#include <chrono>
#include <map>
#include <thread>
#include <utility>

#include "check.h"
#include "sdb/reconnect.h"
#include "sdb/scenariorender.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
struct NullTransport final : Transport
{
    int writes = 0;
    int Write(std::span<const std::uint8_t> r) override
    {
        ++writes;
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

struct Host final : ActionHost
{
    std::vector<std::string> log;
    Runtime* rt = nullptr;
    HttpResponse Webapi(const std::string& m, const std::string& p, const json& b, bool) override
    {
        log.push_back(m + " " + p + (b.is_null() ? "" : " " + b.dump()));
        HttpResponse r;
        r.status = p == "/fail" ? 500 : 200;
        return r;
    }
    HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
    {
        return {};
    }
    int Shell(const std::string&, int, std::string&) override
    {
        return 0;
    }
    bool Open(const std::string&) override
    {
        return true;
    }
    bool Keys(const std::string&, const std::string&, std::string&) override
    {
        return true;
    }
    void Navigate(const json& r) override
    {
        if (rt)
            rt->Navigate(r);
    }
    void Brightness(const json&) override {}
    void SleepMs(int) override {}
    void Note(const std::string&) override {}
};

json Doc()
{
    return json::parse(R"({ "name": "rt", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home",
        "dials": [ { "index": 0, "label": "SEEK", "turn": { "kind": "webapi", "path": "/seek", "body": { "d": "$ticks" } } } ],
        "items": [
          { "id": "pause", "type": "action", "title": "Pause", "at": [3, 1], "do": { "kind": "webapi", "path": "/pause" } },
          { "id": "tape", "type": "action", "title": "Tape", "at": [3, 2], "face_on": { "label": "PLAYING", "bg": "B08000" },
            "do": { "kind": "toggle", "on": { "kind": "webapi", "path": "/tape/play" }, "off": { "kind": "webapi", "path": "/tape/stop" } } },
          { "id": "bad", "type": "action", "title": "Bad", "at": [3, 3], "do": { "kind": "webapi", "path": "/fail" } },
          { "id": "go", "type": "action", "title": "Go", "at": [3, 4], "do": { "kind": "nav", "to": "node", "node": "bp" } },
          { "id": "debug", "type": "menu", "title": "Debug", "at": [3, 5], "nav": { "back": [1, 0] },
            "items": [
              { "id": "step", "type": "action", "title": "Step", "at": [1, 1], "do": { "kind": "webapi", "path": "/step" }, "then": "back" },
              { "id": "bp", "type": "menu", "title": "Breakpoints", "at": [1, 2],
                "items": [ { "id": "bp.add", "type": "action", "title": "Add", "at": [0, 3], "do": { "kind": "webapi", "path": "/bp" }, "then": "home" } ] }
            ] }
        ] } })");
}

Scenario Load(const json& j)
{
    std::vector<Issue> issues;
    auto s = Parse(j, issues);
    for (auto& i : issues)
        std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
    return *s;
}

std::vector<std::string> StackIds(const Runtime& rt)
{
    std::vector<std::string> v;
    const json st = rt.State();  // keep the temporary alive for the loop
    for (const auto& n : st["stack"])
        v.push_back(n["id"]);
    return v;
}

// The TTD timeline on the strip: a tap seeks to the frame under the finger, a swipe steps a second its way (quick
// swipes the same way double it), chips keep their tap, nothing recorded says so instead of seeking
void TestTimelineTouch()
{
    struct Seeks final : ActionHost
    {
        std::vector<json> seeks;
        ActionResult RunAction(const ActionCall& call) override
        {
            if (call.id == "ttd.seek")
                seeks.push_back(call.Body());
            return {true, "ok"};
        }
        HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override
        {
            return {};
        }
        HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
        {
            return {};
        }
        int Shell(const std::string&, int, std::string&) override
        {
            return 0;
        }
        bool Open(const std::string&) override
        {
            return true;
        }
        bool Keys(const std::string&, const std::string&, std::string&) override
        {
            return true;
        }
        void Navigate(const json&) override {}
        void Brightness(const json&) override {}
        void SleepMs(int) override {}
        void Note(const std::string&) override {}
    };
    const Model& m = *FindModel(kElgatoVid, 0x00C6);
    Device dev(m, std::make_unique<NullTransport>(), "T");
    auto host = std::make_unique<Seeks>();
    Seeks* h = host.get();
    RuntimeOptions opt;
    opt.syncActions = true;
    const json doc = json::parse(R"({ "name": "tl", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home", "strip": { "status": "ttd" }, "items": [] } })");
    Runtime rt(dev, Load(doc), std::move(host), opt);
    rt.SetLive(true);
    rt.SetStripReadout("ttd.position",
                       {{"first", 100}, {"end", 1100}, {"frame", 1100}, {"history", true}, {"state", "detached"}, {"fps", 50}});
    rt.Step(0);
    const Scenario laid = Load(doc);
    Navigator nav(laid);
    const auto [x0, x1] = TimelineSpan(Layout(laid, nav, m), m.lcdLogicalW);
    auto frameOf = [&](size_t i) {  // the seek's frame, wherever the call carries it (params or value)
        if (i >= h->seeks.size())
            return -1LL;
        for (const char* part : {"params", "value"})
            if (const json& b = h->seeks[i]; b.contains(part) && b[part].is_object() && b[part].contains("frame"))
                return b[part]["frame"].get<long long>();
        return -1LL;
    };
    Event tap{EventType::LcdTap};
    tap.x = x0 + (x1 - x0 - 1) / 2;
    tap.y = 50;
    rt.Inject(tap);
    rt.Step(0);
    CHECK(h->seeks.size() == 1 && frameOf(0) == 600);  // the middle of 100..1100
    Event swipe{EventType::LcdSwipe};
    swipe.x = 900, swipe.x2 = 850, swipe.y = swipe.y2 = 50;
    rt.Inject(swipe);
    rt.Step(0);
    rt.Inject(swipe);
    rt.Step(0);
    CHECK(h->seeks.size() == 3 && frameOf(1) == 1050 && frameOf(2) == 1000);  // a second, then two (quick, the same way)
    swipe.x = 850, swipe.x2 = 900;
    rt.Inject(swipe);
    rt.Step(0);
    CHECK(h->seeks.size() == 4 && frameOf(3) == 1100);  // the other way: a second again, clamped at the end
    tap.x = 2;  // the chips: theirs
    rt.Inject(tap);
    rt.Step(0);
    CHECK(h->seeks.size() == 4);
    rt.SetStripReadout("ttd.position", {{"first", 0}, {"end", 0}, {"frame", 0}, {"history", false}});
    tap.x = x0 + 40;
    rt.Inject(tap);
    rt.Step(0);
    CHECK(h->seeks.size() == 4);  // nothing recorded: a message, no seek
}

// A read-only menu (monitoring): its keys do nothing (no action, no press look), the back key still goes back.
// An action the host says cannot run now (the actions.unavailable readout): its key greys out, a press sends nothing
void TestReadonlyAndUnavailable()
{
    const Model& m = *FindModel(kElgatoVid, 0x00C6);
    Device dev(m, std::make_unique<NullTransport>(), "T");
    auto host = std::make_unique<Host>();
    Host* h = host.get();
    RuntimeOptions opt;
    opt.syncActions = true;
    const json doc = json::parse(R"({ "name": "ro", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home", "items": [
        { "id": "speed", "type": "action", "title": "Speed", "at": [0, 1], "do": { "kind": "action", "id": "set.speed" } },
        { "id": "mon", "type": "menu", "title": "Monitor", "at": [0, 2], "readonly": true, "nav": { "back": [0, 0] }, "items": [
          { "id": "poke", "type": "action", "title": "Poke", "at": [1, 1], "do": { "kind": "webapi", "path": "/poke" } } ] }
      ] } })");
    const Scenario sc = Load(doc);
    CHECK(sc.root.items.size() == 2 && sc.root.items[1].readonly);
    CHECK(ToJson(sc)["root"]["items"][1].value("readonly", false));  // written back
    Runtime rt(dev, sc, std::move(host), opt);
    h->rt = &rt;
    auto press = [&](int k) {
        rt.Inject({EventType::KeyDown, k});
        rt.Step(0);
        rt.Inject({EventType::KeyUp, k});
        rt.Step(0);
    };
    rt.Step(0);
    press(2);  // into the read-only menu
    CHECK(StackIds(rt).size() == 2);
    const size_t before = h->log.size();
    press(m.cols + 1);  // [1, 1]: poke
    CHECK(h->log.size() == before);  // nothing sent
    press(0);  // the back key
    CHECK(StackIds(rt).size() == 1);

    const auto actionsBefore = rt.State()["last_actions"].size();
    press(1);
    CHECK(rt.State()["last_actions"].size() == actionsBefore + 1);  // available: the job ran (this host refuses it)
    rt.SetReadout("actions.unavailable", {{"set.speed", "TTD holds a history recorded at 1x"}});
    press(1);
    CHECK(rt.State()["last_actions"].size() == actionsBefore + 1);  // greyed out: no job ran
}
// The daemon ends: the keys go dark (V2: "Fill Key with Color" black, a feature report per key) and the strip is
// written whole with the farewell, so the deck does not stay frozen on a page that no longer answers
void TestShowStopped()
{
    struct Recorder final : Transport
    {
        int keyFills = 0, lcdWrites = 0;
        bool failFeature = false;  // the next feature report times out (the handle is lost)
        int Write(std::span<const std::uint8_t> r) override
        {
            if (r.size() > 1 && r[0] == 0x02 && r[1] != 0x07)  // anything but a key image: the LCD
                ++lcdWrites;
            return static_cast<int>(r.size());
        }
        int SendFeature(std::span<const std::uint8_t> r) override
        {
            if (std::exchange(failFeature, false))
                return -1;
            if (r.size() > 1 && r[0] == 0x03 && r[1] == 0x06)
                ++keyFills;
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
    const Model& m = *FindModel(kElgatoVid, 0x00C6);
    auto t = std::make_unique<Recorder>();
    Recorder* rec = t.get();
    Device dev(m, std::move(t), "T");
    RuntimeOptions opt;
    opt.syncActions = true;
    Runtime rt(dev, Load(Doc()), std::make_unique<Host>(), opt);
    rt.ShowStopped("UNREAL-NG STREAM DECK STOPPED");
    CHECK(rec->keyFills == m.keys);
    CHECK(rec->lcdWrites > 0);

    // A write times out on the way: the deck comes back within the wait and gets the stopped picture replayed
    auto first = std::make_unique<Recorder>();
    first->failFeature = true;
    Recorder* back = nullptr;
    ReconnectOptions ro;
    ro.retry = std::chrono::milliseconds(0);
    auto reopen = [&back](bool) {
        auto again = std::make_unique<Recorder>();
        back = again.get();
        return ReconnectingTransport::Probe{std::move(again), {}};
    };
    Device lossy(m, std::make_unique<ReconnectingTransport>(std::move(first), m, "T", reopen, ro), "T");
    Runtime rt2(lossy, Load(Doc()), std::make_unique<Host>(), opt);
    rt2.ShowStopped("UNREAL-NG STREAM DECK STOPPED");
    CHECK(lossy.Connected());
    CHECK(back != nullptr && back->lcdWrites > 0);
}
// The strip is drawn again only when what it shows changed: a live machine without a history (its frame moving at
// 50 Hz) and readouts the strip never draws leave the key alone; a recording's frame moves it
void TestStripKey()
{
    std::map<std::string, json> readouts{
        {"ttd.position", {{"first", 0}, {"end", 0}, {"frame", 100}, {"history", false}, {"state", "idle"}}},
        {"host.stats", {{"cpu_percent", 19.8}}}};
    StripLive live;
    live.link = StripLive::Link::Live;
    live.readouts = &readouts;
    const std::string idle = live.Key();
    readouts["ttd.position"]["frame"] = 101;
    readouts["host.stats"]["cpu_percent"] = 20.1;
    CHECK(live.Key() == idle);
    readouts["ttd.position"] = {{"first", 0}, {"end", 200}, {"frame", 150}, {"history", true}, {"state", "recording"}};
    const std::string rec = live.Key();
    CHECK(rec != idle);
    readouts["ttd.position"]["frame"] = 151;
    CHECK(live.Key() != rec);
    readouts["run.state"] = {{"state", "paused"}};
    CHECK(live.Key().find("run.state") != std::string::npos);
}
}  // namespace

void TestRuntime()
{
    TestShowStopped();
    TestStripKey();
    std::printf("runtime\n");
    const Model& m = *FindModel(kElgatoVid, 0x00C6);
    Device dev(m, std::make_unique<NullTransport>(), "T");
    auto host = std::make_unique<Host>();
    Host* h = host.get();
    RuntimeOptions opt;
    opt.syncActions = true;
    opt.holdMs = 300;
    Runtime rt(dev, Load(Doc()), std::move(host), opt);
    h->rt = &rt;
    auto press = [&](int k) {
        rt.Inject({EventType::KeyDown, k});
        rt.Step(0);
        rt.Inject({EventType::KeyUp, k});
        rt.Step(0);
    };
    using V = std::vector<std::string>;

    rt.Step(0);
    auto st = rt.GetStats();
    CHECK_EQ(st.keyWrites, 36u);  // every key once
    CHECK_EQ(st.stripWrites, 1u);
    rt.Step(0);
    CHECK_EQ(rt.GetStats().keyWrites, 36u);  // nothing changed, nothing sent

    // an action key: runs, outline on press and off on release = 2 key writes
    press(3 * 9 + 1);
    CHECK(h->log.size() == 1 && h->log[0] == "POST /pause");
    CHECK_EQ(rt.GetStats().keyWrites, 38u);

    // a menu key: only the keys that differ are sent
    const auto before = rt.GetStats().keyWrites;
    press(3 * 9 + 5);
    CHECK((StackIds(rt) == V{"home", "debug"}));
    const auto sent = rt.GetStats().keyWrites - before;
    CHECK(sent >= 8 && sent <= 10);  // 5 home items cleared, step + bp + back key drawn (+ press outline)
    CHECK(rt.State()["page"]["back_key"] == 9);

    // leaf then=back
    press(10);
    CHECK(h->log.back() == "POST /step");
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home"}));

    // back key: press = back, hold = home
    press(3 * 9 + 5);
    press(11);
    CHECK((StackIds(rt) == V{"home", "debug", "bp"}));
    press(9);
    CHECK((StackIds(rt) == V{"home", "debug"}));
    press(11);
    rt.Inject({EventType::KeyDown, 9});
    rt.Step(0);
    std::this_thread::sleep_for(std::chrono::milliseconds(350));
    rt.Inject({EventType::KeyUp, 9});
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home"}));

    // nav action to a node, leaf then=home
    press(3 * 9 + 4);
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug", "bp"}));
    // strip: tap the "Debug" crumb, swipe right = back, long press = home
    const auto segs = CrumbSegments({"Home", "Debug", "Breakpoints"}, m.lcdLogicalW);
    Event tap{EventType::LcdTap};
    tap.x = (segs[1].x0 + segs[1].x1) / 2;
    tap.y = 50;
    rt.Inject(tap);
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug"}));
    Event swipe{EventType::LcdSwipe};
    swipe.x = 100;
    swipe.x2 = 400;
    rt.Inject(swipe);
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home"}));
    rt.Navigate({{"to", "node"}, {"node", "bp.add"}});
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug", "bp"}));
    press(3);  // bp.add, then=home
    rt.Step(0);
    CHECK(h->log.back() == "POST /bp");
    CHECK((StackIds(rt) == V{"home"}));

    // toggle: on / off, the face follows (one key write per state change beyond the outline)
    press(3 * 9 + 2);
    CHECK(h->log.back() == "POST /tape/play" && rt.State()["toggles"]["tape"] == true);
    press(3 * 9 + 2);
    CHECK(h->log.back() == "POST /tape/stop" && rt.State()["toggles"]["tape"] == false);

    // dial turn with $ticks
    Event turn{EventType::DialTurn, 0, -3};
    rt.Inject(turn);
    rt.Step(0);
    CHECK(h->log.back() == R"(POST /seek {"d":-3})");

    // a failure shows a banner on the strip
    const auto strips = rt.GetStats().stripWrites;
    press(3 * 9 + 3);
    CHECK(rt.State()["last_actions"].back()["ok"] == false);
    CHECK(rt.GetStats().stripWrites > strips);

    // pressNode by id; unknown ids are refused
    std::string err;
    CHECK(rt.PressNode("debug"));
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug"}));
    CHECK(!rt.PressNode("pause", &err) && err.find("not on the current page") != std::string::npos);

    // reload: the stack keeps the menus that still exist
    rt.PressNode("bp");
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug", "bp"}));
    json j = Doc();
    j["root"]["items"][4]["items"].erase(1);  // drop "bp"
    j["root"]["items"].erase(3);              // and "go", which points at it
    rt.Reload(Load(j));
    rt.Step(0);
    CHECK((StackIds(rt) == V{"home", "debug"}));
    CHECK(rt.State()["scenario"] == "rt" && rt.State()["path"].size() == 2);

    // tickets: a request is applied by the next step; a runtime gone first ends the wait at once
    {
        Device dev2(m, std::make_unique<NullTransport>(), "T2");
        auto rt2 = std::make_unique<Runtime>(dev2, Load(Doc()), std::make_unique<Host>(), opt);
        CHECK(rt2->Posted().Wait(std::chrono::milliseconds(0)));  // nothing posted yet
        rt2->Navigate({{"to", "node"}, {"node", "bp"}});
        const Runtime::Ticket t = rt2->Posted();
        CHECK(!t.Wait(std::chrono::milliseconds(0)));
        rt2->Step(0);
        CHECK(t.Wait(std::chrono::milliseconds(0)));
        CHECK(rt2->PressNode("bp.add"));
        const Runtime::Ticket pending = rt2->Posted();
        rt2.reset();
        const auto t0 = std::chrono::steady_clock::now();
        CHECK(!pending.Wait(std::chrono::seconds(5)));
        CHECK(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(1));
    }
    TestTimelineTouch();
    TestReadonlyAndUnavailable();
}
