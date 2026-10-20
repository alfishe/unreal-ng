// Scenario model: parse / validate / round trip, navigation resolution, the navigator, page layout.
#include "sdb/scenario.h"

#include <algorithm>
#include <string>

#include "check.h"
#include "sdb/navigator.h"
#include "sdb/scenariorender.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{

json Sample()
{
    return json::parse(R"({
      "format": "streamdeck-scenario", "version": 1, "name": "test", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home",
        "dials": [ { "index": 0, "label": "VOL", "turn": { "kind": "http", "url": "http://x/vol" } } ],
        "items": [
          { "id": "pause", "type": "action", "title": "Pause", "at": [3, 1],
            "face": { "icon": "pause", "label": "PAUSE", "bg": "4D4400" },
            "do": { "kind": "webapi", "method": "POST", "path": "/pause" } },
          { "id": "debug", "type": "menu", "title": "Debug", "at": [3, 5],
            "nav": { "back": [1, 0], "auto_exit": 30 },
            "dials": [ { "index": 1, "label": "STEP", "press": { "kind": "webapi", "path": "/step" } } ],
            "items": [
              { "id": "debug.step", "type": "action", "title": "Step", "at": [1, 1],
                "do": { "kind": "webapi", "method": "POST", "path": "/step" }, "then": "back" },
              { "id": "bp", "type": "menu", "title": "Breakpoints", "at": [1, 2],
                "nav": { "dials": "own" },
                "items": [
                  { "id": "bp.add", "type": "action", "title": "Add", "at": [0, 3],
                    "do": { "kind": "multi", "steps": [ { "kind": "delay", "ms": 10 }, { "kind": "nav", "to": "home" } ] } }
                ] }
            ] }
        ] } })");
}

json Classic()
{
    json j = Sample();
    j["model"] = "classic";  // 3x5, no dials, no touch strip
    j["root"].erase("dials");
    j["root"]["items"][0]["at"] = {2, 1};
    j["root"]["items"][1]["at"] = {2, 4};
    j["root"]["items"][1].erase("dials");
    j["root"]["items"][1]["nav"].erase("back");
    return j;
}

bool HasIssue(const std::vector<Issue>& v, const std::string& node, const std::string& fragment)
{
    return std::any_of(v.begin(), v.end(),
                       [&](const Issue& i) { return i.node == node && i.message.find(fragment) != std::string::npos; });
}

std::optional<Scenario> ParseWith(const json& j, std::vector<Issue>& issues)
{
    issues.clear();
    return Parse(j, issues);
}

}  // namespace

void TestScenario()
{
    std::printf("scenario\n");
    std::vector<Issue> issues;
    auto s = ParseWith(Sample(), issues);
    CHECK(s.has_value());
    CHECK(issues.empty());
    if (!s)
    {
        for (auto& i : issues)
            std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
        return;
    }
    CHECK(s->name == "test");
    CHECK(s->model && s->model->pid == 0x00C6);
    CHECK(s->settings.stack && s->settings.crumbs == Crumbs::Auto);
    CHECK(s->root.id == "home" && s->root.items.size() == 2);
    const Node* debug = Find(s->root, "debug");
    CHECK(debug && debug->IsMenu() && debug->nav.back.mode == Back::Mode::At && debug->nav.back.at == (Slot{1, 0}));
    CHECK(debug && debug->nav.autoExit == 30);
    const Node* step = Find(s->root, "debug.step");
    CHECK(step && !step->IsMenu() && step->then == "back" && step->action.kind == "webapi");
    const Node* pause = Find(s->root, "pause");
    CHECK(pause && pause->face.bg.r == 0x4D && pause->face.label == "PAUSE" && pause->then == "stay");
    CHECK(PathTo(s->root, "bp.add").size() == 4);
    CHECK(PathTo(s->root, "nope").empty());

    // round trip
    auto again = ParseWith(ToJson(*s), issues);
    CHECK(again.has_value());
    CHECK(again && ToJson(*again) == ToJson(*s));

    // validation errors
    auto bad = [&](auto&& mutate, const std::string& node, const std::string& fragment) {
        json j = Sample();
        mutate(j);
        auto r = ParseWith(j, issues);
        const bool found = HasIssue(issues, node, fragment);
        CHECK(!r.has_value());
        CHECK(found);
        if (!found)
        {
            std::printf("    expected %s: %s, got:\n", node.c_str(), fragment.c_str());
            for (auto& i : issues)
                std::printf("      %s: %s\n", i.node.c_str(), i.message.c_str());
        }
    };
    bad([](json& j) { j["root"]["items"][1]["items"][0]["id"] = "pause"; }, "pause", "duplicate id");
    bad([](json& j) { j["root"]["items"][0]["at"] = {4, 0}; }, "pause", "outside");
    bad([](json& j) { j["root"]["items"][0]["at"] = {3, 5}; }, "debug", "as 'pause'");
    bad([](json& j) { j["root"]["items"][1]["items"][0]["at"] = {1, 0}; }, "debug.step", "back key");
    bad([](json& j) { j["root"]["items"][1]["items"][1]["items"][0]["at"] = {1, 0}; }, "bp.add",
        "back key");  // inherited
    bad([](json& j) { j["root"]["items"][0]["do"]["kind"] = "teleport"; }, "pause", "unknown action kind");
    bad([](json& j) { j["root"]["items"][0]["do"].erase("path"); }, "pause", "path");
    bad([](json& j) { j["root"]["items"][0]["do"] = {{"kind", "nav"}, {"to", "node"}, {"node", "ghost"}}; }, "pause",
        "ghost");
    bad(
        [](json& j) {
            json a = {{"kind", "delay"}, {"ms", 1}};
            for (int i = 0; i < kMaxMultiDepth + 1; ++i)
                a = {{"kind", "multi"}, {"steps", json::array({a})}};
            j["root"]["items"][0]["do"] = a;
        },
        "pause", "nested");
    bad([](json& j) { j["root"]["dials"][0]["index"] = 6; }, "home", "dial");
    bad([](json& j) { j["root"]["items"][0].erase("at"); }, "pause", "at");
    bad([](json& j) { j["model"] = "toaster"; }, "", "model");
    bad([](json& j) { j["root"]["type"] = "action"; }, "home", "root");
    bad([](json& j) { j["root"]["items"][1]["nav"]["breadcrumbs"] = "sideways"; }, "debug", "breadcrumbs");

    // navigation resolution: + XL -> crumbs on the strip
    auto path = [&](const Scenario& sc, const char* id) { return PathTo(sc.root, id); };
    auto rn = ResolveNav(*s, path(*s, "home"));
    CHECK(rn.crumbs == Crumbs::Strip && !rn.backKey && rn.autoExit == 0);
    rn = ResolveNav(*s, path(*s, "debug"));
    CHECK(rn.crumbs == Crumbs::Strip && rn.backKey && *rn.backKey == (Slot{1, 0}) &&
          rn.autoExit == 30);  // explicit back
    rn = ResolveNav(*s, path(*s, "bp"));
    CHECK(rn.backKey && *rn.backKey == (Slot{1, 0}) && rn.autoExit == 30);  // inherited
    // classic -> crumbs on a key at settings.back
    auto c = ParseWith(Classic(), issues);
    CHECK(c.has_value());
    if (c)
    {
        rn = ResolveNav(*c, path(*c, "debug"));
        CHECK(rn.crumbs == Crumbs::Key && rn.backKey && *rn.backKey == (Slot{0, 0}));
        CHECK(!ResolveNav(*c, path(*c, "home")).backKey);
    }
    // a menu can drop the back key, or pick crumbs itself
    {
        json j = Sample();
        j["root"]["items"][1]["nav"]["back"] = nullptr;
        j["root"]["items"][1]["nav"]["breadcrumbs"] = "key";
        auto t = ParseWith(j, issues);
        CHECK(t.has_value());
        if (t)
        {
            rn = ResolveNav(*t, path(*t, "debug"));
            CHECK(rn.crumbs == Crumbs::Key && !rn.backKey);
        }
    }
    // stack navigation off: no crumbs, no back key
    {
        json j = Sample();
        j["settings"]["navigation"]["stack"] = false;
        auto t = ParseWith(j, issues);
        CHECK(t.has_value() && !t->settings.stack);
        if (t)
        {
            rn = ResolveNav(*t, path(*t, "debug"));
            CHECK(rn.crumbs == Crumbs::None && !rn.backKey);
        }
    }

    // navigator
    Navigator nav(*s);
    CHECK(nav.Depth() == 0 && nav.Current().id == "home");
    CHECK(!nav.Back());
    CHECK(nav.Enter(*debug) && nav.Depth() == 1);
    CHECK(!nav.Enter(*step));  // actions are not entered
    CHECK(nav.Enter(*Find(s->root, "bp")) && nav.Depth() == 2 && nav.Current().id == "bp");
    CHECK(nav.Back() && nav.Current().id == "debug");
    nav.Home();
    CHECK(nav.Depth() == 0);
    CHECK(nav.GoTo("bp.add") && nav.Current().id == "bp" && nav.Depth() == 2);
    CHECK(nav.PopTo(1) && nav.Current().id == "debug");
    CHECK(!nav.PopTo(5));
    CHECK(!nav.GoTo("ghost") && nav.Current().id == "debug");
    CHECK(nav.GoTo("bp") && (nav.Ids() == std::vector<std::string>{"home", "debug", "bp"}));
    {  // reload without "bp": keep home > debug
        json j = Sample();
        j["root"]["items"][1]["items"].erase(1);
        auto t = ParseWith(j, issues);
        CHECK(t.has_value());
        if (t)
        {
            nav.Rebind(*t);
            CHECK((nav.Ids() == std::vector<std::string>{"home", "debug"}));
            nav.Rebind(*s);  // back to the original scenario for the layout checks
        }
    }
    {  // stack off: entering replaces
        json j = Sample();
        j["settings"]["navigation"]["stack"] = false;
        auto t = ParseWith(j, issues);
        if (t)
        {
            Navigator flat(*t);
            CHECK(flat.Enter(*Find(t->root, "debug")) && flat.Depth() == 0 && flat.Current().id == "debug");
            flat.Home();
            CHECK(flat.Current().id == "home");
        }
    }

    // page layout
    Navigator pn(*s);
    const Model& m = *s->model;
    Page p = Layout(*s, pn, m);
    CHECK(p.keys.size() == 36 && p.dials.size() == 6);
    CHECK(p.keys[3 * 9 + 1] == Find(s->root, "pause") && p.keys[3 * 9 + 5] == debug);
    CHECK(p.backKey == -1 && p.crumbs == Crumbs::Strip);
    CHECK(p.dials[0] && p.dials[0]->label == "VOL" && !p.dials[1]);
    CHECK((p.path == std::vector<std::string>{"Home"}));
    pn.Enter(*debug);
    p = Layout(*s, pn, m);
    CHECK(p.keys[1 * 9 + 1] == step && p.keys[3 * 9 + 1] == nullptr);
    CHECK(p.backKey == 9 && p.autoExit == 30);
    CHECK(p.dials[0] && p.dials[0]->label == "VOL" && p.dials[1] && p.dials[1]->label == "STEP");  // inherits dial 0
    CHECK((p.path == std::vector<std::string>{"Home", "Debug"}));
    pn.GoTo("bp");
    p = Layout(*s, pn, m);
    CHECK(!p.dials[0] && !p.dials[1]);  // "own": nothing inherited
    CHECK(p.keys[3] == Find(s->root, "bp.add"));
}

void TestScenarioRender()
{
    std::printf("scenario render\n");
    CHECK(DisplayText("Step over é") == "STEP OVER  ");
    const std::vector<std::string> path{"Home", "Debug", "Breakpoints"};
    auto segs = CrumbSegments(path, 1200);
    CHECK(segs.size() == 3);
    bool ordered = true;
    for (size_t i = 0; i < segs.size(); ++i)
    {
        ordered &= segs[i].x0 < segs[i].x1 && segs[i].x1 <= 1200 * 3 / 5 + 40;
        if (i)
            ordered &= segs[i].x0 > segs[i - 1].x1;
        ordered &= segs[i].depth == static_cast<int>(i);
    }
    CHECK(ordered);
    for (const auto& s : segs)
        CHECK_EQ(CrumbHit(path, 1200, (s.x0 + s.x1) / 2), s.depth);
    CHECK_EQ(CrumbHit(path, 1200, 2), 0);      // the left edge belongs to Home
    CHECK_EQ(CrumbHit(path, 1200, 1190), -1);  // the content area
    // a deep path collapses its middle, keeps the root and the current menu
    std::vector<std::string> deep;
    for (int i = 0; i < 9; ++i)
        deep.push_back("LEVEL NUMBER " + std::to_string(i));
    segs = CrumbSegments(deep, 800);
    CHECK(segs.front().depth == 0 && segs.back().depth == 8);
    CHECK(std::any_of(segs.begin(), segs.end(), [](const Segment& s) { return s.depth == -1 && s.text == ".."; }));
    CHECK(segs.back().x1 <= 800);
    // pictures
    Face f;
    f.label = "Pause";
    f.icon = "pause";
    const Rgb k = RenderFace(f, "x", false, false, 8);
    CHECK(k.w == kFaceSize && k.h == kFaceSize);
    const Rgb b = RenderBackKey("Debug", 2, true, 8);
    CHECK(b.w == kFaceSize && b.At(72, 72)[0] != 0);
    Page p;
    p.crumbs = Crumbs::Strip;
    p.path = path;
    Strip st;
    st.content = "text";
    st.text = "Debugger";
    p.strip = &st;
    Dial d;
    d.label = "Frames";
    p.dials = {&d, nullptr};
    const Rgb strip = RenderStrip(p, 1200, 100, 200, {});
    CHECK(strip.w == 1200 && strip.h == 100);
    // the current crumb is highlighted (blue chip)
    const auto cur = CrumbSegments(path, 1200).back();
    const auto* px = strip.At(cur.x0 + 4, 30);
    CHECK(px[2] > 150 && px[0] < 40);
    // auto exit countdown: the bar shrinks with the time left
    p.autoExit = 5;
    const Rgb full = RenderStrip(p, 1200, 100, 200, {}, 5.0), half = RenderStrip(p, 1200, 100, 200, {}, 2.5);
    CHECK(full.At(1100, 2)[0] > 200 && half.At(1100, 2)[0] < 60 && half.At(100, 2)[0] > 200);
    CHECK(strip.At(1100, 2)[0] < 60);  // no countdown without a time
    st.text = "BACK IN $left S";       // the text counts down too
    CHECK(RenderStrip(p, 1200, 100, 200, {}, 4.2).px != RenderStrip(p, 1200, 100, 200, {}, 2.2).px);
}

void TestTagsAndPages()
{
    std::printf("tags + page description\n");
    json j = json::parse(R"({ "name": "t", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home",
        "dials": [ { "index": 2, "label": "VOL", "turn": { "kind": "http", "url": "http://x" } } ],
        "items": [
          { "id": "a", "type": "action", "title": "Alpha", "tags": ["debug", "fast"], "at": [0, 1], "face": { "label": "A" }, "do": { "kind": "webapi", "path": "/a" }, "then": "home" },
          { "id": "m", "type": "menu", "title": "More", "tags": "debug, menus", "at": [1, 1], "nav": { "back": [0, 0] },
            "items": [ { "id": "t", "type": "action", "title": "Tog", "at": [2, 2], "do": { "kind": "toggle", "on": { "kind": "delay", "ms": 1 }, "off": { "kind": "delay", "ms": 1 } } } ] }
        ] } })");
    std::vector<Issue> issues;
    auto s = Parse(j, issues);
    CHECK(s.has_value());
    if (!s)
        return;
    CHECK((Find(s->root, "m")->tags == std::vector<std::string>{"debug", "menus"}));  // "a, b" string form
    CHECK(ToJson(*s)["root"]["items"][1]["tags"] == json::array({"debug", "menus"}));
    // listNodes filters
    const json doc = ToJson(*s);
    CHECK(ListNodes(doc).size() == 4);
    CHECK(ListNodes(doc, {{"tag", "debug"}}).size() == 2);
    CHECK(ListNodes(doc, {{"type", "menu"}}).size() == 2);
    CHECK(ListNodes(doc, {{"kind", "toggle"}})[0]["id"] == "t");
    CHECK(ListNodes(doc, {{"menu", "m"}})[0]["menu"] == "m");
    CHECK(ListNodes(doc, {{"text", "alp"}})[0]["id"] == "a");
    // describePage
    Navigator nav(*s);
    const Model& m = *s->model;
    json p = DescribePage(*s, nav, m, Layout(*s, nav, m));
    CHECK(p["menu"] == "home" && p["can_back"] == false && p["breadcrumbs"] == "strip");
    CHECK(p["keys"].size() == 2 && p["keys"][0]["id"] == "a" && p["keys"][0]["key"] == 1 &&
          p["keys"][0]["then"] == "home");
    CHECK(p["keys"][0]["tags"] == json::array({"debug", "fast"}) && p["keys"][1]["type"] == "menu" &&
          p["keys"][1]["items"] == 1);
    CHECK(p["dials"].size() == 1 && p["dials"][0]["index"] == 2 && p["dials"][0]["turn"] == "http");
    nav.GoTo("m");
    p = DescribePage(*s, nav, m, Layout(*s, nav, m), {{"t", true}});
    CHECK(p["keys"][0]["role"] == "back" && p["keys"][0]["key"] == 0 && p["back_key"] == 0);
    CHECK(p["keys"][1]["id"] == "t" && p["keys"][1]["on"] == true);
    CHECK(p["path"].size() == 2 && p["path"][1]["id"] == "m");
}
