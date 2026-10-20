// Runtime on a mock transport: rendering only what changed, menus, back key (press / hold), strip crumbs, swipe,
// leaf "then", dials, toggles, failures, reload keeping the stack, the state JSON.
#include "sdb/runtime.h"

#include <chrono>
#include <thread>

#include "check.h"
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
    bool Keys(const std::string&, const std::string&) override
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
}  // namespace

void TestRuntime()
{
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
}
