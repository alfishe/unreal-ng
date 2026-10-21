// Scenario format v2 (docs/inprogress/2026-10-19-streamdeck-integration/scenarios.md): parse, validate, serialize,
// v1 -> v2 conversion, the action kind (call mapping, the REST fallback), path roots, the catalogue check, the runtime's
// held actions, state-bound faces, contexts on menus and the activity flash; the generator's fixture output.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include "check.h"
#include "sdb/actionexec.h"
#include "sdb/actions.h"
#include "sdb/catalogue.h"
#include "sdb/datadir.h"
#include "sdb/runtime.h"
#include "sdb/scenario.h"
#include "sdb/scenarioio.h"

using namespace sdb;
using namespace sdb::scn;
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

// Records the calls of the action kind; everything else is a no-op
struct CallHost final : ActionHost
{
    std::vector<ActionCall> calls;
    std::vector<std::string> webapi;
    bool fail = false;
    HttpResponse Webapi(const std::string& m, const std::string& p, const json&, bool) override
    {
        webapi.push_back(m + " " + p);
        HttpResponse r;
        r.status = 200;
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
    void Navigate(const json&) override {}
    void Brightness(const json&) override {}
    void SleepMs(int) override {}
    void Note(const std::string&) override {}
    ActionResult RunAction(const ActionCall& c) override
    {
        calls.push_back(c);
        return {!fail, "action " + c.id};
    }
    [[nodiscard]] std::vector<std::string> Ids() const
    {
        std::vector<std::string> v;
        for (const auto& c : calls)
            v.push_back(c.id + (c.value.is_null() ? "" : " " + c.value.dump()));
        return v;
    }
};

std::optional<Scenario> ParseDoc(const json& j, std::vector<Issue>& issues)
{
    issues.clear();
    return Parse(j, issues);
}
bool HasIssue(const std::vector<Issue>& issues, const std::string& text, bool error)
{
    return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) {
        return i.error == error && i.message.find(text) != std::string::npos;
    });
}

json Example()
{
    // scenarios.md §8, with a timecode menu and a held key
    return json::parse(R"({
      "format": "streamdeck-scenario", "version": 2,
      "name": "unreal-ng control", "model": "plusxl",
      "settings": { "brightness": 70, "target": "follow",
                    "activity": { "flash_keys": true, "ignore": ["ttd.seek_by"] } },
      "root": {
        "id": "home", "type": "menu", "title": "Home", "context": "play",
        "strip": { "content": "title", "status": "ttd", "target": true },
        "items": [
          { "id": "run", "type": "action", "at": [0, 1],
            "face": { "image": "$data/streamdeck/themes/unrealng/keys/pause.png" },
            "face_on": { "image": "$data/streamdeck/themes/unrealng/keys/play.png" },
            "state": "run.state.paused",
            "do": { "kind": "action", "id": "run.pause_toggle" } },
          { "id": "turbo", "type": "action", "at": [0, 2], "face": { "label": "TURBO" },
            "do": { "kind": "action", "id": "run.turbo_hold", "hold": true } },
          { "id": "elite", "type": "action", "at": [1, 1], "face": { "image": "$scenario/thumbnails/elite.png" },
            "do": { "kind": "multi", "steps": [
              { "kind": "action", "id": "mach.start", "value": { "model": "PENTAGON", "ram_kb": 128, "replace": true } },
              { "kind": "action", "id": "media.open", "value": { "path": "$testdata/loaders/tap/elite.tap" } } ] } },
          { "id": "ttd", "type": "menu", "title": "Time travel", "context": "ttd", "at": [0, 5],
            "dials": [ { "index": 0, "label": "FRAMES",
              "turn":  { "kind": "action", "id": "ttd.seek_by",
                         "value": { "d": "$ticks", "rate": "$rate", "unit": "frames", "min": 1, "max": 100 } },
              "press": { "kind": "action", "id": "run.pause_toggle" } } ],
            "items": [
              { "id": "ttd.timecode", "type": "menu", "title": "Time code", "context": "ttd.timecode", "at": [1, 1],
                "items": [] } ] }
        ] } })");
}
}  // namespace

void TestScenarioV2()
{
    std::printf("scenario v2\n");
    std::vector<Issue> issues;

    // ---- parse and round trip
    {
        auto s = ParseDoc(Example(), issues);
        CHECK(s.has_value());
        CHECK(std::none_of(issues.begin(), issues.end(), [](const Issue& i) { return i.error; }));
        CHECK(s && s->version == 2);
        CHECK(s && s->root.context == "play");
        CHECK(s && s->root.strip.target && *s->root.strip.target);
        CHECK(s && s->settings.activity.ignore == std::vector<std::string>{"ttd.seek_by"});
        const Node* run = s ? Find(s->root, "run") : nullptr;
        CHECK(run && run->state == "run.state.paused" && run->faceOn && run->action.kind == "action");
        const json back = ToJson(*s);
        CHECK(back["version"] == 2 && back["format"] == "streamdeck-scenario");
        CHECK(back["root"]["context"] == "play" && back["root"]["strip"]["target"] == true);
        CHECK(back["root"]["items"][0]["state"] == "run.state.paused");
        CHECK(!back["settings"].contains("activity"));  // flash_keys and ignore ttd.seek_by are the defaults (Q-3)
        auto again = ParseDoc(back, issues);
        CHECK(again && ToJson(*again) == back);  // stable
    }

    // ---- versions: v1 loads as v1 (its kinds without warnings), a newer one is refused, a v1 kind in v2 warns
    {
        json v1 = json::parse(R"({ "version": 1, "model": "plusxl", "settings": { "webapi": "http://h:1" },
          "root": { "id": "home", "type": "menu", "dials": [ { "index": 0, "turn": { "kind": "ttd_seek", "min": 1 },
            "press": { "kind": "run_toggle" } } ], "items": [] } })");
        auto s = ParseDoc(v1, issues);
        CHECK(s && s->version == 1 && s->settings.webapi == "http://h:1");
        CHECK(issues.empty());
        CHECK(ToJson(*s)["version"] == 1);
        json v2 = v1;
        v2["version"] = 2;
        s = ParseDoc(v2, issues);
        CHECK(s.has_value());  // warnings only
        CHECK(HasIssue(issues, "ttd_seek is a v1 kind", false) && HasIssue(issues, "run_toggle is a v1 kind", false));
        CHECK(HasIssue(issues, "settings.webapi is v1", false));
        json v3 = v1;
        v3["version"] = 3;
        CHECK(!ParseDoc(v3, issues) && HasIssue(issues, "newer version", true));
        v3["version"] = 0;
        CHECK(!ParseDoc(v3, issues));
        json none = v1;
        none.erase("version");
        s = ParseDoc(none, issues);
        CHECK(s && s->version == kFormatVersion);  // a file without a version: the current one
    }

    // ---- validation of the v2 fields
    {
        auto bad = [&](const std::function<void(json&)>& edit, const std::string& text) {
            json d = Example();
            edit(d);
            const bool refused = !ParseDoc(d, issues);
            const bool said = HasIssue(issues, text, true);
            if (!refused || !said)
                std::printf("    expected error '%s'\n", text.c_str());
            return refused && said;
        };
        auto item = [](json& d, int i) -> json& { return d["root"]["items"][i]; };
        CHECK(bad([&](json& d) { item(d, 0)["do"].erase("id"); }, "action needs 'id'"));
        CHECK(bad([&](json& d) { item(d, 0)["do"]["id"] = "pause"; }, "is not a catalogue id"));
        CHECK(bad([&](json& d) { item(d, 1)["do"]["hold"] = "yes"; }, "hold must be true or false"));
        CHECK(bad([&](json& d) { item(d, 0)["do"]["value"] = nullptr; }, "value must be"));
        CHECK(bad([&](json& d) { item(d, 0)["do"]["params"] = 3; }, "params must be an object"));
        CHECK(bad([&](json& d) { item(d, 0)["state"] = "paused"; }, "not a readout path"));
        CHECK(bad([&](json& d) { item(d, 0)["face"]["image"] = "$nowhere/a.png"; }, "unknown path root '$nowhere'"));
        CHECK(bad([&](json& d) { item(d, 2)["do"]["steps"][1]["value"]["path"] = "$tst/x.tap"; }, "unknown path root"));
        CHECK(bad([&](json& d) { d["root"]["context"] = "Play Mode"; }, "context 'Play Mode'"));
        CHECK(bad([&](json& d) { d["root"]["context"] = 5; }, "context must be a string"));
        CHECK(bad([&](json& d) { d["settings"]["target"] = "pinned"; }, "settings.target"));
        CHECK(bad([&](json& d) { d["settings"]["target"] = {{"view", 0}}; }, "settings.target"));
        CHECK(bad([&](json& d) { d["settings"]["activity"]["strip"] = "loud"; }, "settings.activity.strip"));
        CHECK(bad([&](json& d) { d["settings"]["activity"]["seconds"] = 0; }, "settings.activity.seconds"));
        CHECK(bad([&](json& d) { d["root"]["strip"]["status"] = "clock"; }, "strip.status"));
        CHECK(bad([&](json& d) { d["root"]["strip"]["target"] = "yes"; }, "strip.target"));
        CHECK(bad([&](json& d) { item(d, 1)["activity"] = 1; }, "activity must be true or false"));
        // warnings: an absolute path, a state without face_on
        json d = Example();
        item(d, 1)["face"]["image"] = "/tmp/turbo.png";
        item(d, 1)["state"] = "toggle.run.turbo_hold";
        auto s = ParseDoc(d, issues);
        CHECK(s && HasIssue(issues, "absolute path", false) && HasIssue(issues, "state without face_on", false));
        d = Example();
        d["settings"]["target"] = {{"view", 2}};
        CHECK(ParseDoc(d, issues).has_value());
        CHECK(IsReadoutPath("toggle.feature.TurboSound") && IsReadoutPath("audio.mute.muted"));
        CHECK(!IsReadoutPath("run") && !IsReadoutPath(".a.b") && !IsReadoutPath("a..b") && !IsReadoutPath("Run.x"));
    }

    // ---- v1 -> v2
    {
        const json v1 = json::parse(R"({ "version": 1, "name": "old", "model": "plusxl",
          "settings": { "webapi": "http://10.0.0.5:8090", "emulator": "unreal-qt", "companion_file": "/c.json",
                        "brightness": 40 },
          "root": { "id": "home", "type": "menu",
            "dials": [ { "index": 0, "turn": { "kind": "ttd_seek", "unit": "seconds", "min": 0.02, "max": 2, "fps": 48.83 },
                         "press": { "kind": "run_toggle" } },
                       { "index": 1, "turn": { "kind": "dir", "left": { "kind": "webapi", "path": "/ttd/step-back" },
                         "right": { "kind": "first", "try": [ { "kind": "webapi", "path": "/ttd/step-forward" },
                                                             { "kind": "webapi", "path": "/run_frames", "body": { "count": 1 } } ] } } } ],
            "items": [
              { "id": "p", "type": "action", "at": [0, 1], "do": { "kind": "webapi", "path": "/pause" } },
              { "id": "g", "type": "action", "at": [0, 2], "do": { "kind": "multi", "steps": [
                  { "kind": "webapi", "path": "/api/v1/emulator/start", "body": { "model": "PENTAGON", "ram_size": 128, "replace": true } },
                  { "kind": "delay", "ms": 600 },
                  { "kind": "webapi", "path": "/snapshot/load", "body": { "path": "$testdata/a.sna" } },
                  { "kind": "webapi", "path": "/disk/B/insert", "body": { "path": "x.trd", "autostart": true } },
                  { "kind": "webapi", "path": "/tape/play" },
                  { "kind": "webapi", "path": "/coverage", "body": { "on": true } } ] } },
              { "id": "q", "type": "action", "at": [0, 3], "do": { "kind": "webapi", "method": "GET", "path": "/state" } }
            ] } })");
        std::vector<std::string> notes;
        const json v2 = ConvertToV2(v1, &notes);
        CHECK(v2["version"] == 2 && v2["format"] == "streamdeck-scenario");
        CHECK(!v2["settings"].contains("webapi") && !v2["settings"].contains("emulator") &&
              !v2["settings"].contains("companion_file") && v2["settings"]["brightness"] == 40);
        const json& d0 = v2["root"]["dials"][0];
        CHECK(d0["press"] == json({{"kind", "action"}, {"id", "run.pause_toggle"}}));
        CHECK(d0["turn"]["id"] == "ttd.seek_by" && d0["turn"]["value"]["d"] == "$ticks" &&
              d0["turn"]["value"]["unit"] == "seconds" && d0["turn"]["value"]["max"] == 2);
        const json& d1 = v2["root"]["dials"][1]["turn"];
        CHECK(d1["left"]["id"] == "ttd.step_back_frame" && d1["right"]["try"][0]["id"] == "ttd.step_forward_frame" &&
              d1["right"]["try"][1] == json({{"kind", "action"}, {"id", "run.frames"}, {"value", {{"count", 1}}}}));
        const json& items = v2["root"]["items"];
        CHECK(items[0]["do"] == json({{"kind", "action"}, {"id", "run.pause"}}));
        const json& steps = items[1]["do"]["steps"];
        CHECK(steps[0]["id"] == "mach.start" && steps[0]["value"]["ram_kb"] == 128 && steps[0]["value"]["replace"] == true);
        CHECK(steps[1]["kind"] == "delay");
        CHECK(steps[2]["id"] == "media.open" && steps[2]["value"]["path"] == "$testdata/a.sna");
        CHECK(steps[3]["value"]["drive"] == "B" && steps[3]["value"]["autostart"] == true);
        CHECK(steps[4]["value"]["op"] == "play");
        CHECK(steps[5]["kind"] == "webapi");    // no action: the REST escape hatch
        CHECK(items[2]["do"]["kind"] == "webapi");  // a GET stays
        auto noted = [&](const std::string& t) {
            return std::any_of(notes.begin(), notes.end(), [&](const std::string& n) { return n.find(t) != std::string::npos; });
        };
        CHECK(noted("settings.webapi") && noted("settings.emulator") && noted("companion_file"));
        CHECK(noted("1 run_toggle") && noted("1 ttd_seek") && noted("2 webapi steps kept") && noted("POST /coverage"));
        auto s = ParseDoc(v2, issues);
        CHECK(s && s->version == 2);
        CHECK(std::none_of(issues.begin(), issues.end(), [](const Issue& i) { return i.error; }));
        CHECK(ConvertToV2(v2) == v2);  // v2 stays as it is
    }

    // ---- the call a step makes
    {
        ActionCall c = MakeActionCall(json{{"kind", "action"}, {"id", "run.pause_toggle"}});
        CHECK(c.id == "run.pause_toggle" && c.value.is_null() && c.Body() == json::object());
        c = MakeActionCall(json{{"id", "run.turbo_hold"}, {"hold", true}}, 1);
        CHECK(c.Body() == json({{"value", true}}));
        c = MakeActionCall(json{{"id", "run.turbo_hold"}, {"hold", true}}, -1);
        CHECK(c.Body() == json({{"value", false}}));
        c = MakeActionCall(json::parse(R"({"id":"ttd.seek_by","value":{"d":-3,"rate":12.5,"unit":"frames","min":1}})"));
        CHECK(c.Body() == json::parse(R"({"value":{"delta":-3,"rate":12.5},"params":{"unit":"frames","min":1}})"));
        c = MakeActionCall(json::parse(R"({"id":"ttd.seek_by","value":{"d":2,"rate":"$rate"}})"));  // no rate: none
        CHECK(c.Body() == json::parse(R"({"value":{"delta":2}})"));
        c = MakeActionCall(json::parse(R"({"id":"ctx.set","value":"ttd"})"));
        CHECK(c.Body() == json::parse(R"({"value":"ttd"})"));
        c = MakeActionCall(json::parse(R"({"id":"input.type","value":{"text":"LOAD","delay_frames":3}})"));
        CHECK(c.Body() == json::parse(R"({"value":"LOAD","params":{"delay_frames":3}})"));
        c = MakeActionCall(json::parse(R"({"id":"mach.start","value":{"model":"PENTAGON","ram_kb":128}})"));
        CHECK(c.Body() == json::parse(R"({"params":{"model":"PENTAGON","ram_kb":128}})"));  // SD6: press params
        c = MakeActionCall(json::parse(R"({"id":"set.speed","value":{"value":4},"params":{"value":8,"x":1}})"));
        CHECK(c.Body() == json::parse(R"({"params":{"value":8,"x":1}})"));
        c = MakeActionCall(json::parse(R"({"id":"ttd.scrub_back","value":0.5})"));
        CHECK(c.Body() == json::parse(R"({"value":0.5})"));
    }

    // ---- the REST fallback
    {
        std::vector<std::pair<std::string, json>> posts;
        HttpResponse answer;
        answer.status = 200;
        answer.body = R"({"ok":true,"action":"run.pause_toggle","message":"Paused"})";
        auto exec = MakeRestActionExecutor(
            [&](const std::string& path, const json& body) {
                posts.emplace_back(path, body);
                return answer;
            },
            [] { return std::string("emu-7"); });
        ActionResult r = exec->Run(MakeActionCall(json{{"id", "run.pause_toggle"}}));
        CHECK(r.ok && r.message.find("Paused") != std::string::npos);
        CHECK(posts.size() == 1 && posts[0].first == "/api/v1/controllers/actions/run.pause_toggle" &&
              posts[0].second == json({{"instance", "emu-7"}}));
        answer.status = 409;
        answer.body = R"({"ok":false,"error":"not_now","message":"needs a pause"})";
        r = exec->Run(MakeActionCall(json{{"id", "run.step_in"}}));
        CHECK(!r.ok && r.message.find("409") != std::string::npos && r.message.find("not_now: needs a pause") != std::string::npos);
        answer.status = 0;
        answer.error = "connection refused";
        r = exec->Run(MakeActionCall(json{{"id", "run.step_in"}}));
        CHECK(!r.ok && r.message.find("connection refused") != std::string::npos);
        auto noInstance = MakeRestActionExecutor([&](const std::string& p, const json& b) {
            posts.emplace_back(p, b);
            HttpResponse ok;
            ok.status = 200;
            return ok;
        });
        CHECK(noInstance->Run(MakeActionCall(json{{"id", "mach.nmi"}})).ok && posts.back().second == json::object());
        CHECK(!noInstance->Run(ActionCall{}).ok);
    }

    // ---- Execute: substitution, path roots of params, the default host
    {
        CallHost host;
        const fs::path dir = fs::temp_directory_path() / "sdb-v2-scenario";
        json step = json::parse(
            R"({"kind":"multi","steps":[{"kind":"action","id":"media.open","value":{"path":"$scenario/a.tap"}},
                                         {"kind":"action","id":"ttd.seek_by","value":{"d":"$ticks","rate":"$rate"}}]})");
        auto r = Execute({"multi", step}, host, json{{"ticks", -2}, {"rate", 7.0}, {"__dir", dir.generic_string()}});
        CHECK(r.ok && host.calls.size() == 2);
        CHECK(host.calls.size() == 2 && host.calls[0].params["path"] == (dir / "a.tap").lexically_normal().generic_string());
        CHECK(host.calls.size() == 2 && host.calls[1].value == json({{"delta", -2}, {"rate", 7.0}}));
        struct Bare final : ActionHost
        {
            HttpResponse Webapi(const std::string&, const std::string&, const json&, bool) override { return {}; }
            HttpResponse Http(const std::string&, const std::string&, const json&, const std::string&) override { return {}; }
            int Shell(const std::string&, int, std::string&) override { return 0; }
            bool Open(const std::string&) override { return true; }
            bool Keys(const std::string&, const std::string&, std::string&) override { return true; }
            void Navigate(const json&) override {}
            void Brightness(const json&) override {}
            void SleepMs(int) override {}
            void Note(const std::string&) override {}
        } bare;
        CHECK(!Execute({"action", json{{"kind", "action"}, {"id", "run.pause"}}}, bare).ok);
        const std::string was = DefaultHost();
        SetDefaultHost("http://10.1.1.1:9000");
        CHECK(Settings{}.webapi == "http://10.1.1.1:9000");
        SetDefaultHost(was);
    }

    // ---- path roots
    {
        const fs::path t = fs::temp_directory_path() / "sdb-v2-roots";
        fs::remove_all(t);
        fs::create_directories(t / "data" / "streamdeck");
        fs::create_directories(t / "testdata" / "loaders");
        data::SetRoot(t / "data");
        data::SetUserRoot(t / "home" / ".unreal-ng");
        CHECK(data::TestdataRoot() == (t / "testdata").lexically_normal());
        CHECK(data::Resolve("$testdata/loaders/a.tap") == (t / "testdata" / "loaders" / "a.tap").generic_string());
        CHECK(data::Resolve("$user/streamdeck/x.json") == (t / "home" / ".unreal-ng" / "streamdeck" / "x.json").generic_string());
        CHECK(data::Resolve("$scenario/thumbnails/a.png", t / "sc") == (t / "sc" / "thumbnails" / "a.png").generic_string());
        CHECK(data::Resolve("$scenario/a.png") == "$scenario/a.png");  // no folder: as written
        CHECK(data::Resolve("thumbs/a.png", t / "sc") == (t / "sc" / "thumbs" / "a.png").generic_string());
        CHECK(data::Resolve("../a.png", t / "sc") == (t / "a.png").lexically_normal().generic_string());
        CHECK(data::Resolve("thumbs/a.png") == "thumbs/a.png");
        CHECK(data::Resolve((t / "abs.png").generic_string(), t / "sc") == (t / "abs.png").generic_string());
        CHECK(data::Resolve("$database/x.png", t / "sc") == "$database/x.png");
        CHECK(data::RootOf("$testdata/x") == "testdata" && data::RootOf("$user") == "user" && data::RootOf("a/b").empty());
        CHECK(data::PathRoots().size() == 5);
        // a relative picture of a scenario loads from its folder, not the working directory
        {
            fs::create_directories(t / "sc");
            Rgb px(2, 2, 200, 10, 10);
            const auto bmp = EncodeBmp(px);
            std::ofstream(t / "sc" / "red.bmp", std::ios::binary)
                .write(reinterpret_cast<const char*>(bmp.data()), static_cast<std::streamsize>(bmp.size()));
            CHECK(LoadImageRef("red.bmp", t / "sc").has_value());
            CHECK(LoadImageRef("$scenario/red.bmp", t / "sc").has_value());
            CHECK(!LoadImageRef("red.bmp").has_value());
        }
        data::SetUserRoot({});
        data::SetRoot({});
        fs::remove_all(t);
    }

    // ---- the catalogue
    {
        const json reply = json::parse(R"({"ok":true,"instance":"e1","actions":[
          {"id":"run.pause_toggle","label":"Pause / continue","group":"Run","kind":"press","held":false,"gui":false,
           "available":true},
          {"id":"run.turbo_hold","label":"Turbo while held","group":"Run","kind":"press","held":true,"gui":false},
          {"id":"ttd.seek_by","label":"Seek by","group":"Time travel","kind":"delta","held":false,"gui":false},
          {"id":"ctx.set","label":"Set the surface context","group":"Context","kind":"text"},
          {"id":"mach.start","label":"Start a machine","group":"Capture and machine","kind":"press","params":"model"},
          {"label":"no id"}]})");
        auto c = cat::Parse(reply);
        CHECK(c && c->actions.size() == 5 && c->Find("mach.start") && c->Find("mach.start")->params == "model");
        CHECK(c && c->Find("run.turbo_hold")->held && !c->Find("nope"));
        const json shipped = cat::ToJson(*c);
        CHECK(shipped["format"] == cat::kFormat && !shipped["actions"][0].contains("available"));
        CHECK(cat::Parse(shipped)->actions.size() == 5);
        CHECK(!cat::Parse(json(3)).has_value());
        json d = Example();
        d["root"]["items"][1]["do"].erase("hold");                                       // held without hold
        d["root"]["items"][0]["do"]["id"] = "run.nothing";                               // unknown
        d["root"]["items"][3]["dials"][0]["press"]["value"] = json{{"d", 1}};            // a delta on a press
        auto s = ParseDoc(d, issues);
        CHECK(s.has_value());
        const auto w = cat::Check(*s, *c);
        CHECK(HasIssue(w, "'run.nothing' is not in the host's catalogue", false));
        CHECK(HasIssue(w, "acts on the release too", false));
        CHECK(HasIssue(w, "a dial delta (d) does not fit", false));
        CHECK(HasIssue(w, "'media.open' is not in", false));  // not in this small catalogue
        s = ParseDoc(Example(), issues);
        const auto clean = cat::Check(*s, *c);
        CHECK(clean.size() == 1);  // media.open only
        // a held action with a hold? param is a tap unless its params hold it
        cat::Catalogue keys;
        keys.actions.push_back({"input.key", "Key", "Input", "press", true, false, "key, hold? (false: a tap)"});
        json kd = json::parse(R"({"model":"plusxl","root":{"id":"home","type":"menu","items":[
          {"id":"tap","type":"action","at":[0,0],"do":{"kind":"action","id":"input.key","value":{"key":"a"}}},
          {"id":"held","type":"action","at":[0,1],"do":{"kind":"action","id":"input.key","value":{"key":"a","hold":true}}},
          {"id":"both","type":"action","at":[0,2],"do":{"kind":"action","id":"input.key","hold":true,
                                                      "value":{"key":"a","hold":true}}}]}})");
        s = ParseDoc(kd, issues);
        const auto kw = s ? cat::Check(*s, keys) : std::vector<Issue>{};
        CHECK(kw.size() == 1 && kw[0].node == "held");
    }

    // ---- the runtime: held actions, state-bound faces, contexts, the activity flash
    {
        const Model& m = *FindModelByName("plusxl");
        Device dev(m, std::make_unique<NullTransport>(), "V2");
        auto hostPtr = std::make_unique<CallHost>();
        CallHost* host = hostPtr.get();
        RuntimeOptions opt;
        opt.syncActions = true;
        std::vector<Issue> is;
        auto sc = Parse(Example(), is);
        Runtime rt(dev, *sc, std::move(hostPtr), opt);
        rt.Step(0);
        // entering the deck: the root's context (Run calls NoteMenu; a step does once a menu changes)
        auto key = [&](int row, int col) { return row * m.cols + col; };
        auto pageKey = [&](const std::string& id) {
            const json page = rt.CurrentPage();  // alive for the loop
            for (const auto& k : page["keys"])
                if (k.value("id", "") == id)
                    return k;
            return json();
        };
        // the held key: press on KeyDown, release on KeyUp
        host->calls.clear();
        rt.Inject({EventType::KeyDown, key(0, 2)});
        rt.Step(0);
        CHECK(host->Ids() == std::vector<std::string>{"run.turbo_hold true"});
        rt.Inject({EventType::KeyUp, key(0, 2)});
        rt.Step(0);
        CHECK(host->Ids() == (std::vector<std::string>{"run.turbo_hold true", "run.turbo_hold false"}));
        // state-bound face: flips locally while no readout comes; a readout wins
        CHECK(pageKey("run")["on"] == false && pageKey("run")["state"] == "run.state.paused");
        rt.Inject({EventType::KeyDown, key(0, 1)});
        rt.Step(0);
        rt.Inject({EventType::KeyUp, key(0, 1)});
        rt.Step(0);
        CHECK(pageKey("run")["on"] == true);
        rt.SetReadout("run.state.paused", false);
        CHECK(pageKey("run")["on"] == false);
        rt.SetReadout("run.state.paused", true);
        CHECK(pageKey("run")["on"] == true && rt.State()["readouts"]["run.state.paused"] == true);
        // contexts: entering "ttd" sets it, its submenu sets ttd.timecode, home goes back to play
        host->calls.clear();
        rt.Inject({EventType::KeyDown, key(0, 5)});
        rt.Step(0);
        rt.Inject({EventType::KeyUp, key(0, 5)});
        rt.Step(0);
        CHECK(host->Ids() == std::vector<std::string>{"ctx.set \"ttd\""});
        CHECK(rt.Context() == "ttd" && rt.CurrentPage()["context"] == "ttd");
        rt.Inject({EventType::KeyDown, key(1, 1)});
        rt.Step(0);
        rt.Inject({EventType::KeyUp, key(1, 1)});
        rt.Step(0);
        CHECK(rt.Context() == "ttd.timecode");
        rt.Navigate(json{{"to", "home"}});
        rt.Step(0);
        CHECK(rt.Context() == "play" && host->calls.size() == 3 && host->calls.back().value == "play");
        // a ctx from the core: to the nearest menu declaring it, not sent back
        host->calls.clear();
        rt.OnContext("ttd.timecode");
        rt.Step(0);
        CHECK(rt.State()["page"]["menu"] == "ttd.timecode" && host->calls.empty());
        rt.OnContext("play");
        rt.Step(0);
        CHECK(rt.State()["page"]["menu"] == "home" && host->calls.empty());
        rt.OnContext("unknown");
        rt.Step(0);
        CHECK(rt.State()["page"]["menu"] == "home");
        // a dial's hold-less press and turn go as calls; the turn carries delta + rate and the curve's params
        host->calls.clear();
        rt.Navigate(json{{"to", "node"}, {"node", "ttd"}});
        rt.Step(0);
        host->calls.clear();
        rt.Inject({EventType::DialTurn, 0, 3});
        rt.Step(0);
        CHECK(host->calls.size() == 1 && host->calls[0].id == "ttd.seek_by" && host->calls[0].value["delta"] == 3 &&
              host->calls[0].params["max"] == 100);
        // activity: a key bound to the id flashes (re-drawn pressed), an ignored id does nothing
        rt.Navigate(json{{"to", "home"}});
        rt.Step(0);
        rt.Step(0);
        const auto before = rt.GetStats().keyWrites;
        rt.ShowActivity("ttd.seek_by");
        rt.Step(0);
        CHECK(rt.GetStats().keyWrites == before);
        rt.ShowActivity("run.pause_toggle");
        rt.Step(0);
        CHECK(rt.GetStats().keyWrites == before + 1);
    }

    // ---- the generator's fixture output (tools/tests/fixture.deck.json, from fixture.spec.json): valid v2 with no
    // warning, release-clean (ACC-5), every action in the fixture catalogue, every $data path in the source tree
    {
        const fs::path root = SDB_TEST_SOURCE_DATA;
        const fs::path tools = SDB_TEST_TOOLS_DIR;
        data::SetRoot(root);
        std::string err;
        auto doc = io::LoadFile(tools / "tests" / "fixture.deck.json", &err);
        CHECK(doc.has_value());
        if (doc)
        {
            auto s = ParseDoc(*doc, issues);
            for (const auto& i : issues)
                std::printf("    %s %s: %s\n", i.error ? "error" : "warning", i.node.c_str(), i.message.c_str());
            CHECK(s && s->version == 2 && issues.empty());
            const std::string text = doc->dump();
            for (const char* kind : {"\"shell\"", "\"open\"", "\"keys\"", "\"webapi\"", "\"ttd_seek\"", "\"run_toggle\""})
                CHECK(text.find(std::string("\"kind\":") + kind) == std::string::npos);
            auto c = cat::Load(tools / "tests" / "fixture.catalogue.json", &err);
            CHECK(c.has_value());
            if (s && c)
            {
                const auto w = cat::Check(*s, *c);
                for (const auto& i : w)
                    std::printf("    catalogue %s: %s\n", i.node.c_str(), i.message.c_str());
                CHECK(w.empty());
            }
            std::function<void(const json&)> walk = [&](const json& j) {
                if (j.is_string())
                {
                    const std::string v = j.get<std::string>();
                    if (v.rfind("$data/", 0) == 0)
                    {
                        const bool there = fs::exists(data::Resolve(v.substr(0, v.find('#'))));
                        if (!there)
                            std::printf("    missing %s\n", v.c_str());
                        CHECK(there);
                    }
                }
                else if (j.is_object() || j.is_array())
                    for (const auto& x : j)
                        walk(x);
            };
            walk(*doc);
            CHECK(s && s->root.strip.status == "ttd");  // the TTD timeline: a strip readout
        }
        // the shipped catalogue is one too
        auto shipped = cat::Load(root / "streamdeck" / "catalogue.json", &err);
        CHECK(shipped && shipped->actions.size() > 40 && shipped->Find("run.pause_toggle"));
        data::SetRoot({});
    }
}
