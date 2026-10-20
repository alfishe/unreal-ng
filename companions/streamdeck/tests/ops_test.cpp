// Editor operations on the scenario JSON: every op leaves a document that still parses.
#include <string>

#include "check.h"
#include "sdb/scenario.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
bool Parses(const json& doc)
{
    std::vector<Issue> issues;
    const bool ok = Parse(doc, issues).has_value();
    if (!ok)
        for (auto& i : issues)
            std::printf("    %s: %s\n", i.node.c_str(), i.message.c_str());
    return ok;
}
json MakeAction(const std::string& title, const std::string& path = "/pause")
{
    return {{"type", "action"}, {"title", title}, {"do", {{"kind", "webapi"}, {"method", "POST"}, {"path", path}}}};
}
json Menu(const std::string& title)
{
    return {{"type", "menu"}, {"title", title}, {"items", json::array()}};
}
Slot At(const json* n)
{
    return n ? Slot{(*n)["at"][0].get<int>(), (*n)["at"][1].get<int>()} : Slot{};
}
}  // namespace

void TestOps()
{
    std::printf("scenario ops\n");
    json doc = ops::NewScenario(*FindModelByName("plusxl"), "demo");
    CHECK(Parses(doc));
    CHECK(doc["root"]["id"] == "home" && doc["model"] == "plusxl");
    CHECK(ops::Slug("Step Over!") == "step-over" && ops::Slug("") == "item");

    // insert: ids from titles, unique; first free slot; explicit slot; occupied slot refused
    auto r = ops::InsertNode(doc, "home", std::nullopt, MakeAction("Pause"));
    CHECK(r.ok && r.id == "pause");
    CHECK(At(ops::FindNode(doc, "pause")) == (Slot{0, 0}));
    r = ops::InsertNode(doc, "home", std::nullopt, MakeAction("Pause"));
    CHECK(r.ok && r.id == "pause-2" && At(ops::FindNode(doc, "pause-2")) == (Slot{0, 1}));
    r = ops::InsertNode(doc, "home", Slot{3, 5}, Menu("Debug"));
    CHECK(r.ok && r.id == "debug");
    CHECK(!ops::InsertNode(doc, "home", Slot{3, 5}, MakeAction("X")).ok);
    CHECK(!ops::InsertNode(doc, "home", Slot{4, 0}, MakeAction("X")).ok);     // outside the 4x9 grid
    CHECK(!ops::InsertNode(doc, "pause", std::nullopt, MakeAction("X")).ok);  // not a menu
    CHECK(Parses(doc));

    // inside a submenu the back key's slot (0,0 on a key-crumbs device, explicit here) is skipped
    CHECK(ops::UpdateNode(doc, "debug", {{"nav", {{"back", {0, 0}}}}}).ok);
    CHECK(ops::ReservedSlot(doc, "debug") == (Slot{0, 0}));
    r = ops::InsertNode(doc, "debug", std::nullopt, MakeAction("Step", "/step"));
    CHECK(r.ok && At(ops::FindNode(doc, r.id)) == (Slot{0, 1}));
    CHECK(!ops::InsertNode(doc, "debug", Slot{0, 0}, MakeAction("Y")).ok);
    // a subtree with clashing ids is renamed on insert
    json sub = Menu("Debug");
    sub["id"] = "debug";
    sub["items"].push_back(MakeAction("Pause"));
    sub["items"][0]["id"] = "pause";
    sub["items"][0]["at"] = {1, 1};
    r = ops::InsertNode(doc, "home", Slot{3, 6}, sub);
    CHECK(r.ok && r.id == "debug-2");
    CHECK(ops::FindNode(doc, "pause-3") != nullptr);
    CHECK(Parses(doc));

    // nodeAt / freeSlot
    CHECK(ops::NodeAt(doc, "home", {3, 5}) && (*ops::NodeAt(doc, "home", {3, 5}))["id"] == "debug");
    CHECK(ops::NodeAt(doc, "home", {2, 2}) == nullptr);
    CHECK(ops::FreeSlot(doc, "home") == (Slot{0, 2}));

    // move within a menu, swap, into a submenu, not into itself
    CHECK(ops::MoveNode(doc, "pause", "home", {1, 0}, false).ok);
    CHECK(At(ops::FindNode(doc, "pause")) == (Slot{1, 0}));
    CHECK(!ops::MoveNode(doc, "pause", "home", {0, 1}, false).ok);  // occupied by pause-2
    CHECK(ops::MoveNode(doc, "pause", "home", {0, 1}, true).ok);    // swap
    CHECK(At(ops::FindNode(doc, "pause")) == (Slot{0, 1}) && At(ops::FindNode(doc, "pause-2")) == (Slot{1, 0}));
    CHECK(ops::MoveNode(doc, "pause-2", "debug", {2, 2}, false).ok);
    CHECK((*ops::ParentOf(doc, "pause-2"))["id"] == "debug");
    CHECK(!ops::MoveNode(doc, "debug", "debug", {1, 1}, false).ok);
    CHECK(!ops::MoveNode(doc, "home", "debug", {1, 1}, false).ok);
    CHECK(Parses(doc));

    // update: merge patch, rename follows nav targets, duplicate ids refused
    CHECK(ops::InsertNode(
              doc, "home", Slot{2, 0},
              {{"type", "action"}, {"title", "Go debug"}, {"do", {{"kind", "nav"}, {"to", "node"}, {"node", "debug"}}}})
              .ok);
    r = ops::UpdateNode(doc, "debug", {{"id", "dbg"}, {"title", "Debugger"}, {"face", {{"icon", "bug"}}}});
    CHECK(r.ok && r.id == "dbg");
    CHECK((*ops::FindNode(doc, "go-debug"))["do"]["node"] == "dbg");
    CHECK((*ops::FindNode(doc, "dbg"))["face"]["icon"] == "bug" && (*ops::FindNode(doc, "dbg"))["title"] == "Debugger");
    r = ops::UpdateNode(doc, "dbg", {{"id", "pause"}});
    CHECK(r.ok && r.id == "pause-4");                                           // made unique
    CHECK(ops::UpdateNode(doc, "pause", {{"face", {{"label", nullptr}}}}).ok);  // null removes
    CHECK(Parses(doc));

    // duplicate, remove
    r = ops::DuplicateNode(doc, "pause-4", std::nullopt);
    CHECK(r.ok && r.id != "pause-4" && ops::FindNode(doc, r.id));
    CHECK(Parses(doc));
    CHECK(ops::RemoveNode(doc, r.id).ok && !ops::FindNode(doc, r.id));
    CHECK(!ops::RemoveNode(doc, "home").ok);
    CHECK(!ops::RemoveNode(doc, "ghost").ok);
    CHECK(Parses(doc));
}
