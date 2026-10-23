// Value faces (scenarios.md §4.2): the template formatter, thresholds, parsing / validation / writing (JSON and
// CSV), the readout paths they need, the picture, and the runtime drawing a key again only when its text changed.
#include "sdb/valueface.h"

#include <algorithm>
#include <functional>

#include "check.h"
#include "sdb/runtime.h"
#include "sdb/scenarioio.h"
#include "sdb/scenariorender.h"
#include "sdb/surfacereadouts.h"

using namespace sdb;
using namespace sdb::scn;

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

struct QuietHost final : ActionHost
{
    int calls = 0;
    HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override
    {
        ++calls;
        HttpResponse r;
        r.status = 200;
        return r;
    }
    HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
    {
        return {};
    }
    ActionResult RunAction(const ActionCall&) override
    {
        ++calls;
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

json Doc()
{
    return json::parse(R"({ "format": "streamdeck-scenario", "version": 2, "name": "vf", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "title": "Home",
        "items": [
          { "id": "cpu", "type": "action", "title": "CPU", "at": [0, 1],
            "face": { "label": "CPU", "value": { "readout": "host.stats.cpu_percent", "format": "{:.0f}%",
                                                 "warn": 80, "alert": 95 } } },
          { "id": "fps", "type": "action", "title": "FPS", "at": [0, 2],
            "face": { "glyph": "run.play", "value": { "readout": "emulator.stats", "format": "{fps:.0f} FPS" } },
            "do": { "kind": "action", "id": "run.pause_toggle" } },
          { "id": "plain", "type": "action", "title": "Reset", "at": [0, 3], "do": { "kind": "action", "id": "mach.reset" } }
        ] } })");
}

bool HasIssue(const std::vector<Issue>& issues, const std::string& text, bool error)
{
    for (const auto& i : issues)
        if (i.error == error && i.message.find(text) != std::string::npos)
            return true;
    return false;
}

std::optional<Scenario> ParseWith(const std::function<void(json&)>& edit, std::vector<Issue>& issues)
{
    json d = Doc();
    edit(d);
    issues.clear();
    auto s = Parse(d, issues);
    if (s)
        for (const auto& i : Validate(*s))
            issues.push_back(i);
    return s;
}

int Tinted(const Rgb& im, draw::Color c)
{
    int n = 0;
    for (int y = 0; y < im.h; ++y)
        for (int x = 0; x < im.w; ++x)
        {
            const auto* p = im.At(x, y);
            if (std::abs(p[0] - c.r) < 8 && std::abs(p[1] - c.g) < 8 && std::abs(p[2] - c.b) < 8)
                ++n;
        }
    return n;
}
}  // namespace

void TestValueFaces()
{
    // ---- the template
    CHECK(FormatValue("{:.0f}%", 12.6) == "13%");
    CHECK(FormatValue("{fps:.1f} FPS", json{{"fps", 49.96}}) == "50.0 FPS");
    CHECK(FormatValue("", 3) == "3" && FormatValue("", 2.5) == "2.5" && FormatValue("", 2.0) == "2");
    CHECK(FormatValue("{}", true) == "ON" && FormatValue("{}", false) == "OFF" && FormatValue("{}", "48K") == "48K");
    CHECK(FormatValue("{:b}", 1536) == "1.5K" && FormatValue("{:b}", 356515840) == "340M" &&
          FormatValue("{:b}", 512) == "512B" && FormatValue("{:.2b}", 1536) == "1.50K");
    CHECK(FormatValue("{:k}", 1234) == "1.2k" && FormatValue("{:k}", 950) == "950");
    CHECK(FormatValue("{slots:#}", json{{"slots", {1, 2, 3}}}) == "3");
    CHECK(FormatValue("{recording?REC|IDLE}", json{{"recording", true}}) == "REC" &&
          FormatValue("{recording?REC|IDLE}", json{{"recording", false}}) == "IDLE");
    CHECK(FormatValue("{frame_time_us.avg:.0f}US", json{{"frame_time_us", {{"avg", 19968.4}}}}) == "19968US");
    CHECK(FormatValue("{slots.1.name}", json{{"slots", {{{"name", "A"}}, {{"name", "B"}}}}}) == "B");
    CHECK(FormatValue("{missing}", json::object()) == "--" && FormatValue("{:.0f}%", nullptr) == "--%");
    CHECK(FormatValue("{{x}} {}", 1) == "{x} 1");
    CHECK(FormatValue("{:04d}", 42) == "0042" && FormatValue("{:+d}", 3) == "+3" && FormatValue("{:x}", 255) == "ff");
    CHECK(FormatValue("{:%}", 0.5) == "50%");
    CHECK(FormatValue("{active}/{total}", json{{"active", 2}, {"total", 5}}) == "2/5");
    CHECK(FormatValue("{a}\n{b}", json{{"a", 1}, {"b", 2}}) == "1\n2");
    CHECK(CheckValueFormat("{:.0f}%").empty() && CheckValueFormat("").empty() && CheckValueFormat("{a?x|}").empty());
    CHECK(!CheckValueFormat("{:q}").empty() && !CheckValueFormat("{").empty() && !CheckValueFormat("}").empty() &&
          !CheckValueFormat("{a?b}").empty() && !CheckValueFormat("{a b}").empty() && !CheckValueFormat("{:.f}").empty());
    CHECK(FormatValue("{:q}", 1) == "{:q}");  // a bad template shows as it is

    // ---- thresholds: up (warn < alert) and down (warn > alert: speed falling is bad)
    {
        FaceValue up{"host.stats.cpu_percent", "{:.0f}%", 80.0, 95.0};
        CHECK(ShowValue(up, 50).level == 0 && ShowValue(up, 85).level == 1 && ShowValue(up, 99).level == 2);
        CHECK(ShowValue(up, nullptr).level == 0 && ShowValue(up, nullptr).text == "--%");
        FaceValue down{"emulator.stats", "{speed_percent:.0f}%", 90.0, 50.0};
        CHECK(ShowValue(down, json{{"speed_percent", 100}}).level == 0 &&
              ShowValue(down, json{{"speed_percent", 80}}).level == 1 &&
              ShowValue(down, json{{"speed_percent", 40}}).level == 2);
        CHECK(ValueNumber("{fps} at {speed_percent}", json{{"fps", 50}, {"speed_percent", 99}}) == 50.0);
    }

    // ---- readout paths
    {
        CHECK(SplitReadoutPath("host.stats.cpu_percent").name == "host.stats" &&
              SplitReadoutPath("host.stats.cpu_percent").field == "cpu_percent");
        CHECK(SplitReadoutPath("emulator.stats.frame_time_us.avg").field == "frame_time_us.avg");
        CHECK(SplitReadoutPath("machine.previous.model").name == "machine.previous" &&
              SplitReadoutPath("machine.model").name == "machine");
        CHECK(SplitReadoutPath("audio.source.ay1.a").name == "audio.source.ay1.a");
        CHECK(IsKnownReadoutPath("debug.breakpoints.total") && IsKnownReadoutPath("toggle.mach.turbo_toggle") &&
              IsKnownReadoutPath("cap.record") && !IsKnownReadoutPath("instances.count") &&
              !IsKnownReadoutPath("host.stat.cpu"));
        CHECK(ReadoutField(SplitReadoutPath("emulator.stats.frame_time_us.avg"),
                           json{{"frame_time_us", {{"avg", 20}}}}) == 20);
    }

    // ---- parse, validate, write
    {
        std::vector<Issue> issues;
        auto s = ParseWith([](json&) {}, issues);
        CHECK(s && issues.empty());
        if (!s)
            return;
        const Node* cpu = Find(s->root, "cpu");
        CHECK(cpu && cpu->face.value && cpu->face.value->readout == "host.stats.cpu_percent" &&
              cpu->face.value->format == "{:.0f}%" && cpu->face.value->warn == 80.0 && cpu->face.value->alert == 95.0);
        CHECK(cpu && cpu->action.kind.empty());  // display only: no "do"
        const json back = ToJson(*s);
        CHECK(back["root"]["items"][0]["face"]["value"] == Doc()["root"]["items"][0]["face"]["value"]);
        CHECK(!back["root"]["items"][0].contains("do"));
        std::vector<Issue> again;
        auto s2 = Parse(back, again);
        CHECK(s2 && Find(s2->root, "cpu")->face.value == cpu->face.value);
        // CSV: the value goes in the extra column, as a glyph does
        const std::string csv = io::Serialize(back, io::Format::Csv);
        CHECK(csv.find("host.stats.cpu_percent") != std::string::npos);
        auto fromCsv = io::Deserialize(csv, io::Format::Csv);
        std::vector<Issue> ci;
        auto s3 = fromCsv ? Parse(*fromCsv, ci) : std::nullopt;
        CHECK(s3 && Find(s3->root, "cpu") && Find(s3->root, "cpu")->face.value == cpu->face.value &&
              Find(s3->root, "fps")->face.value->format == "{fps:.0f} FPS");

        auto item = [](json& d, int i) -> json& { return d["root"]["items"][i]; };
        CHECK(!ParseWith([&](json& d) { item(d, 0)["face"]["value"]["format"] = "{:q}"; }, issues) ||
              HasIssue(issues, "value.format: unknown spec", true));
        CHECK(HasIssue(issues, "value.format: unknown spec", true));
        ParseWith([&](json& d) { item(d, 0)["face"]["value"]["readout"] = "Host stats"; }, issues);
        CHECK(HasIssue(issues, "is not a readout path", true));
        auto w = ParseWith([&](json& d) { item(d, 0)["face"]["value"]["readout"] = "instances.count"; }, issues);
        CHECK(w && HasIssue(issues, "is no readout the core publishes", false));
        ParseWith([&](json& d) { item(d, 0)["face"]["value"] = json{{"format", "{}"}}; }, issues);
        CHECK(HasIssue(issues, "face.value must be", true));
        ParseWith([&](json& d) { item(d, 0)["face"]["value"]["warn"] = "high"; }, issues);
        CHECK(HasIssue(issues, "warn must be a number", true));
        ParseWith([&](json& d) { item(d, 0)["face"]["value"]["colour"] = "red"; }, issues);
        CHECK(HasIssue(issues, "unknown field 'colour'", true));
        ParseWith([&](json& d) { item(d, 0)["face"].erase("value"); }, issues);
        CHECK(HasIssue(issues, "needs \"do\"", true));  // only a value key goes without

        // what the scenario needs from the core: the readouts and the paths of its value faces
        const SurfaceNeeds needs = NeedsOf(*s);
        auto has = [](const std::vector<std::string>& v, const std::string& x) {
            return std::find(v.begin(), v.end(), x) != v.end();
        };
        CHECK(has(needs.readouts, "host.stats") && has(needs.readouts, "emulator.stats"));
        CHECK(has(needs.paths, "host.stats.cpu_percent") && has(needs.paths, "emulator.stats"));
    }

    // ---- the picture: the text changes the face, the level tints it, the preview shows "--"
    {
        Face f;
        f.label = "CPU";
        f.value = FaceValue{"host.stats.cpu_percent", "{:.0f}%", 80.0, 95.0};
        const ValueShown low{"12%", 0}, hot{"97%", 2}, warm{"85%", 1};
        const Rgb a = RenderFace(f, "CPU", false, false, 8, 112, {}, {}, &low);
        const Rgb b = RenderFace(f, "CPU", false, false, 8, 112, {}, {}, &hot);
        const Rgb c = RenderFace(f, "CPU", false, false, 8, 112, {}, {}, &warm);
        const Rgb same = RenderFace(f, "CPU", false, false, 8, 112, {}, {}, &low);
        CHECK(a.w == 112 && a.h == 112 && a.px == same.px && a.px != b.px);
        CHECK(Tinted(a, {205, 70, 58}) == 0 && Tinted(b, {205, 70, 58}) > 20 && Tinted(c, {220, 170, 60}) > 20);
        const Rgb preview = RenderFace(f, "CPU", false, false, 8, 112);
        const ValueShown dashes{"--%", 0};
        CHECK(preview.px == RenderFace(f, "CPU", false, false, 8, 112, {}, {}, &dashes).px);
        CHECK(RenderFace(f, "CPU", true, false, 8, 112, {}, {}, &low).px != a.px);  // pressed
    }

    // ---- a poll asks for the page's paths at once (ActionHost::Readouts; the default asks Readout for each)
    {
        struct OneHost final : ActionHost
        {
            HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override
            {
                return {};
            }
            HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override
            {
                return {};
            }
            json Readout(const std::string& path) override
            {
                return path == "host.stats.threads" ? json(23) : json();
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
        } one;
        CHECK(one.Readouts({"host.stats.threads", "emulator.stats.fps"}) == json({{"host.stats.threads", 23}}));
    }

    // ---- the runtime: redrawn only when the text changes; a display-only key runs nothing
    {
        const Model& m = *FindModelByName("plusxl");
        Device dev(m, std::make_unique<NullTransport>(), "VF");
        auto hostPtr = std::make_unique<QuietHost>();
        QuietHost* host = hostPtr.get();
        RuntimeOptions opt;
        opt.syncActions = true;
        std::vector<Issue> is;
        auto sc = Parse(Doc(), is);
        Runtime rt(dev, *sc, std::move(hostPtr), opt);
        rt.Step(0);
        auto writes = [&] { return rt.GetStats().keyWrites; };
        auto pageValue = [&](const std::string& id) {
            const json page = rt.CurrentPage();
            for (const auto& k : page["keys"])
                if (k.value("id", "") == id)
                    return k.value("value", json());
            return json();
        };
        std::size_t w0 = writes();
        rt.Step(0);
        CHECK(writes() == w0);  // nothing changed
        CHECK(pageValue("cpu")["text"] == "--%");
        rt.SetReadout("host.stats.cpu_percent", 12.3);
        rt.Step(0);
        CHECK(writes() == w0 + 1 && pageValue("cpu")["text"] == "12%" && pageValue("cpu")["level"] == 0);
        rt.SetReadout("host.stats.cpu_percent", 12.4);  // the same text: no write
        rt.Step(0);
        CHECK(writes() == w0 + 1);
        rt.SetReadout("emulator.stats", json{{"fps", 50.1}, {"speed_percent", 100}, {"frame", 10}});
        rt.Step(0);
        CHECK(writes() == w0 + 2 && pageValue("fps")["text"] == "50 FPS");
        rt.SetReadout("emulator.stats", json{{"fps", 49.9}, {"speed_percent", 99}, {"frame", 11}});
        rt.Step(0);
        CHECK(writes() == w0 + 2);  // other fields moved, the text did not
        rt.SetReadout("host.stats.cpu_percent", 85);
        rt.Step(0);
        CHECK(writes() == w0 + 3 && pageValue("cpu")["level"] == 1);
        CHECK(rt.State()["readouts"]["host.stats.cpu_percent"] == 85);
        // the display-only key: a press runs nothing; the value key with an action runs it
        const auto actions = rt.GetStats().actions;
        rt.Inject({EventType::KeyDown, 1});
        rt.Inject({EventType::KeyUp, 1});
        rt.Step(0);
        CHECK(rt.GetStats().actions == actions && host->calls == 0);
        rt.Inject({EventType::KeyDown, 2});
        rt.Inject({EventType::KeyUp, 2});
        rt.Step(0);
        CHECK(rt.GetStats().actions == actions + 1);
        CHECK(pageValue("plain").is_null());
    }
}
