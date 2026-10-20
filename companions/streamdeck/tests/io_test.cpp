// Scenario documents in JSON / YAML / CSV / TSV: every format round-trips to the same scenario.
#include <filesystem>
#include <string>

#include "check.h"
#include "sdb/scenario.h"
#include "sdb/scenarioio.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
json SampleDoc()
{
    return json::parse(R"({ "format": "streamdeck-scenario", "version": 1, "name": "io, \"quoted\"", "model": "plusxl",
      "settings": { "brightness": 55, "navigation": { "back": [3, 0] } },
      "root": { "id": "home", "type": "menu", "title": "Home",
        "strip": { "content": "text", "text": "UNREAL, NG" },
        "dials": [ { "index": 0, "label": "SEEK", "turn": { "kind": "webapi", "path": "/seek", "body": { "d": "$ticks" } } } ],
        "items": [
          { "id": "pause", "type": "action", "title": "Pause, now", "at": [0, 1],
            "face": { "label": "PAUSE", "icon": "pause", "bg": "000000", "image": "pictures/a b.png" },
            "do": { "kind": "webapi", "method": "POST", "path": "/pause", "control": false } },
          { "id": "wait", "type": "action", "title": "Wait", "at": [0, 2], "do": { "kind": "delay", "ms": 250 } },
          { "id": "true", "type": "action", "title": "123", "at": [0, 3], "do": { "kind": "shell", "command": "echo \"a\tb\"; echo x,y" } },
          { "id": "debug", "type": "menu", "title": "Debug", "at": [1, 0], "nav": { "back": [3, 8], "auto_exit": 20 },
            "items": [
              { "id": "multi", "type": "action", "title": "Multi", "at": [0, 0],
                "do": { "kind": "multi", "steps": [ { "kind": "delay", "ms": 5 }, { "kind": "nav", "to": "home" } ] }, "then": "back" },
              { "id": "sub", "type": "menu", "title": "Sub", "at": [0, 1],
                "items": [ { "id": "sub.go", "type": "action", "title": "Go", "at": [2, 2], "do": { "kind": "nav", "to": "node", "node": "debug" } } ] }
            ] },
          { "id": "tape", "type": "action", "title": "Tape", "at": [2, 4], "face_on": { "label": "ON" },
            "do": { "kind": "toggle", "on": { "kind": "webapi", "path": "/tape/play" }, "off": { "kind": "webapi", "path": "/tape/stop" } } }
        ] } })");
}

json Normalized(const json& doc)
{
    std::vector<Issue> issues;
    auto s = Parse(doc, issues);
    for (auto& i : issues)
        std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
    return s ? ToJson(*s) : json();
}
}  // namespace

void TestIo()
{
    std::printf("scenario io\n");
    const json doc = SampleDoc();
    const json want = Normalized(doc);
    CHECK(!want.is_null());
    for (auto f : {io::Format::Json, io::Format::Yaml, io::Format::Csv, io::Format::Tsv})
    {
        const std::string text = io::Serialize(doc, f);
        std::string err;
        auto back = io::Deserialize(text, f, &err);
        CHECK(back.has_value());
        if (!back)
        {
            std::printf("    %s: %s\n%s\n", io::FormatName(f), err.c_str(), text.c_str());
            continue;
        }
        const bool same = Normalized(*back) == want;
        CHECK(same);
        if (!same)
            std::printf("    %s differs:\n%s\n--- got\n%s\n", io::FormatName(f), want.dump(1).c_str(),
                        Normalized(*back).dump(1).c_str());
    }
    // YAML keeps strings that look like numbers / booleans as strings
    auto y = io::FromYaml(io::ToYaml(
        json{{"bg", "000000"}, {"t", "true"}, {"n", 5}, {"f", 1.5}, {"b", false}, {"z", nullptr}, {"a", {1, "2"}}}));
    CHECK(y && (*y)["bg"] == "000000" && (*y)["t"] == "true" && (*y)["n"] == 5 && (*y)["f"] == 1.5 &&
          (*y)["b"] == false && (*y)["z"].is_null());
    CHECK(y && (*y)["a"] == json::array({1, "2"}));
    // hand-written YAML
    auto hy = io::Deserialize(
        "name: hand\nmodel: classic\nroot:\n  id: home\n  type: menu\n  items:\n    - id: a\n      type: action\n      "
        "at: [0, 1]\n      do: {kind: open, target: 'https://x'}\n",
        io::Format::Yaml);
    CHECK(hy && !Normalized(*hy).is_null() && (*hy)["root"]["items"][0]["do"]["target"] == "https://x");
    // hand-written CSV: only the simple columns, quoted commas
    const std::string csv =
        "id,parent,type,title,row,col,label,icon,bg,fg,kind,method,arg,then,extra\n"
        "demo,,scenario,demo,,,,,,,classic,,,,\n"
        "home,,menu,Home,,,,,,,,,,,\n"
        "p,home,action,\"Pause, now\",0,1,PAUSE,pause,4D4400,,webapi,POST,/pause,,\n"
        "m,home,menu,More,1,1,MORE,gear,,,,,,,\n"
        "w,m,action,Wait,0,1,,,,,delay,,300,back,\n";
    std::string err;
    auto hc = io::Deserialize(csv, io::Format::Csv, &err);
    CHECK(hc.has_value());
    if (hc)
    {
        const json n = Normalized(*hc);
        CHECK(!n.is_null() && n["model"] == "classic");
        CHECK((*hc)["root"]["items"][0]["title"] == "Pause, now" &&
              (*hc)["root"]["items"][0]["do"]["path"] == "/pause");
        CHECK((*hc)["root"]["items"][1]["items"][0]["do"]["ms"] == 300 &&
              (*hc)["root"]["items"][1]["items"][0]["then"] == "back");
    }
    CHECK(!io::Deserialize("id,parent\nx,ghost\n", io::Format::Csv, &err) && err.find("type") != std::string::npos);
    CHECK(!io::Deserialize("id,parent,type\na,,menu\nb,,menu\n", io::Format::Csv, &err) &&
          err.find("two roots") != std::string::npos);
    CHECK(!io::Deserialize("a: [1,\n", io::Format::Yaml, &err));
    // files: the extension picks the format; saving is atomic
    const auto dir = std::filesystem::temp_directory_path() / "sdb-io-test";
    std::filesystem::create_directories(dir);
    for (const char* ext : {"json", "yaml", "yml", "csv", "tsv"})
    {
        const auto file = dir / (std::string("s.") + ext);
        CHECK(io::SaveFile(file, doc, &err));
        auto back = io::LoadFile(file, &err);
        CHECK(back && Normalized(*back) == want);
    }
    CHECK(io::FormatOf("x.YML") == io::Format::Yaml && !io::FormatOf("x.txt"));
    std::filesystem::remove_all(dir);
}
