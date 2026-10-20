// Subtrees between files: export, import (as a menu / merged items), includes (links) with prefixes, cycles, inlining.
#include <filesystem>
#include <fstream>
#include <string>

#include "check.h"
#include "sdb/scenario.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
bool Parses(const json& doc, std::string* why = nullptr)
{
    std::vector<Issue> issues;
    const bool ok = Parse(doc, issues).has_value();
    if (!ok)
        for (auto& i : issues)
        {
            if (why)
                *why += i.node + ": " + i.message + "; ";
            else
                std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
        }
    return ok;
}
json MainDoc()
{
    return json::parse(R"({ "name": "main", "model": "plusxl",
      "root": { "id": "home", "type": "menu", "items": [
        { "id": "pause", "type": "action", "at": [0, 0], "do": { "kind": "webapi", "path": "/pause" } },
        { "id": "debug", "type": "menu", "at": [1, 0], "nav": { "back": [3, 0] }, "items": [
          { "id": "step", "type": "action", "at": [1, 1], "do": { "kind": "webapi", "path": "/step" } },
          { "id": "bp", "type": "menu", "at": [1, 2], "items": [
            { "id": "bp.clear", "type": "action", "at": [0, 1], "do": { "kind": "nav", "to": "node", "node": "debug" } },
            { "id": "bp.home", "type": "action", "at": [0, 2], "do": { "kind": "nav", "to": "node", "node": "pause" } } ] } ] } ] } })");
}
void Write(const std::filesystem::path& f, const json& j)
{
    std::string e;
    io::SaveFile(f, j, &e);
}
}  // namespace

void TestSubtrees()
{
    std::printf("subtrees\n");
    const json doc = MainDoc();
    CHECK(Parses(doc));

    // export: a menu becomes the root; outside nav targets are reported; the file is a playable scenario
    std::vector<std::string> outside;
    json frag = ops::ExtractSubtree(doc, "debug", &outside);
    CHECK(frag["root"]["id"] == "debug" && !frag["root"].contains("at") && frag["model"] == "plusxl");
    CHECK(outside.size() == 1 && outside[0] == "pause");  // bp.clear -> debug is inside, bp.home -> pause is not
    // an action is wrapped and keeps its slot
    json fa = ops::ExtractSubtree(doc, "step");
    CHECK(fa["root"]["fragment"] == true && fa["root"]["items"][0]["id"] == "step" &&
          fa["root"]["items"][0]["at"] == json::array({1, 1}));

    // import as a menu into the same document: ids clash -> renamed, inner nav follows
    json d = doc;
    auto r = ops::ImportSubtree(d, "home", Slot{2, 2}, frag, ops::ImportMode::Menu);
    CHECK(r.ok && r.id == "debug-2");
    CHECK(ops::FindNode(d, "bp-2") && (*ops::FindNode(d, "bp.clear-2"))["do"]["node"] == "debug-2");
    CHECK((*ops::FindNode(d, "bp.home-2"))["do"]["node"] == "pause");  // outside target unchanged
    CHECK(Parses(d));
    // a wrapped action comes back as the action
    r = ops::ImportSubtree(d, "home", Slot{0, 5}, fa, ops::ImportMode::Menu);
    CHECK(r.ok && (*ops::FindNode(d, r.id))["type"] == "action");
    // merge items: each at its own slot when free
    json d2 = doc;
    r = ops::ImportSubtree(d2, "home", std::nullopt, frag, ops::ImportMode::Items);
    CHECK(r.ok);
    CHECK(ops::NodeAt(d2, "home", {1, 1}) && (*ops::NodeAt(d2, "home", {1, 1}))["id"] == "step-2");
    CHECK(ops::NodeAt(d2, "home", {1, 2}) && (*ops::NodeAt(d2, "home", {1, 2}))["id"] == "bp-2");
    CHECK(Parses(d2));
    CHECK(!ops::ImportSubtree(d2, "home", std::nullopt, fa["root"]["items"][0], ops::ImportMode::Items)
               .ok);  // not a document

    // includes (links)
    const auto dir = std::filesystem::temp_directory_path() / "sdb-include-test";
    std::filesystem::create_directories(dir / "lib");
    Write(dir / "lib" / "debug.deck.yaml", frag);
    json host = json::parse(R"({ "name": "host", "model": "plusxl", "root": { "id": "home", "type": "menu", "items": [
        { "id": "pause", "type": "action", "at": [0, 0], "do": { "kind": "webapi", "path": "/pause" } },
        { "id": "dbg", "type": "include", "file": "lib/debug.deck.yaml", "at": [1, 4], "title": "Debugger", "face": { "bg": "2A0040" } },
        { "id": "dbg2", "type": "include", "file": "lib/debug.deck.yaml", "node": "step", "at": [1, 5] } ] } })");
    std::string why;
    CHECK(!Parses(host, &why) && why.find("not resolved") != std::string::npos);  // placeholders are not playable
    Write(dir / "host.deck.json", host);
    std::vector<std::string> files;
    std::string err;
    auto res = io::LoadResolved(dir / "host.deck.json", &err, &files);
    CHECK(res.has_value());
    if (res)
    {
        CHECK(Parses(*res));
        const json* dbg = ops::FindNode(*res, "dbg");
        CHECK(dbg && (*dbg)["type"] == "menu" && (*dbg)["title"] == "Debugger" && (*dbg)["at"] == json::array({1, 4}) &&
              (*dbg)["face"]["bg"] == "2A0040");
        CHECK(ops::FindNode(*res, "dbg.step") && ops::FindNode(*res, "dbg.bp.clear"));
        CHECK((*ops::FindNode(*res, "dbg.bp.clear"))["do"]["node"] == "dbg");  // inner nav follows the prefix
        CHECK(ops::FindNode(*res, "dbg2") &&
              (*ops::FindNode(*res, "dbg2"))["type"] == "action");  // one file twice, no clash
        CHECK(dbg && dbg->contains("included_from"));
        CHECK(files.size() == 3);
    }
    // nested include + override of the node
    Write(dir / "lib" / "outer.deck.json",
          json::parse(R"({ "model": "plusxl", "root": { "id": "outer", "type": "menu", "items": [
        { "id": "inner", "type": "include", "file": "debug.deck.yaml", "at": [0, 1] } ] } })"));
    json h2 = host;
    h2["root"]["items"][2] =
        json::parse(R"({ "id": "o", "type": "include", "file": "lib/outer.deck.json", "at": [2, 2] })");
    res = io::ResolveIncludes(h2, dir, &err);
    CHECK(res && ops::FindNode(*res, "o.inner.step"));
    // cycle
    Write(
        dir / "lib" / "a.json",
        json::parse(
            R"({ "model": "plusxl", "root": { "id": "a", "type": "menu", "items": [ { "id": "b", "type": "include", "file": "b.json", "at": [0, 0] } ] } })"));
    Write(
        dir / "lib" / "b.json",
        json::parse(
            R"({ "model": "plusxl", "root": { "id": "b", "type": "menu", "items": [ { "id": "a", "type": "include", "file": "a.json", "at": [0, 0] } ] } })"));
    err.clear();
    CHECK(!io::LoadResolved(dir / "lib" / "a.json", &err) && err.find("cycle") != std::string::npos);
    // missing file / node
    json h3 = host;
    h3["root"]["items"][1]["file"] = "lib/ghost.json";
    CHECK(!io::ResolveIncludes(h3, dir, &err) && err.find("dbg") != std::string::npos);
    h3 = host;
    h3["root"]["items"][2]["node"] = "ghost";  // the second include names a node that is not there
    CHECK(!io::ResolveIncludes(h3, dir, &err) && err.find("ghost") != std::string::npos);

    // link a node to a file, inline it back (editable copy)
    json d3 = doc;
    CHECK(ops::LinkNode(d3, "debug", "lib/debug.deck.yaml").ok);
    CHECK((*ops::FindNode(d3, "debug"))["type"] == "include" &&
          (*ops::FindNode(d3, "debug"))["at"] == json::array({1, 0}));
    auto resolved = io::ResolveIncludes(d3, dir, &err);
    CHECK(resolved.has_value());
    if (resolved)
    {
        CHECK(ops::InlineNode(d3, "debug", *ops::FindNode(*resolved, "debug")).ok);
        CHECK((*ops::FindNode(d3, "debug"))["type"] == "menu" &&
              !ops::FindNode(d3, "debug")->contains("included_from"));
        CHECK(Parses(d3));
    }
    std::filesystem::remove_all(dir);
}
