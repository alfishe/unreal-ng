#include "sdb/scenarioops.h"

#include <cctype>
#include <functional>
#include <map>
#include <set>
#include <vector>

#include "sdb/format.h"

namespace sdb::scn::ops
{

namespace
{

void ForEach(json& n, const std::function<void(json&)>& f)
{
    f(n);
    if (n.contains("items") && n["items"].is_array())
        for (auto& c : n["items"])
            ForEach(c, f);
}
void ForEach(const json& n, const std::function<void(const json&)>& f)
{
    f(n);
    if (n.contains("items") && n["items"].is_array())
        for (const auto& c : n["items"])
            ForEach(c, f);
}

std::set<std::string> AllIds(const json& doc)
{
    std::set<std::string> ids;
    if (doc.contains("root"))
        ForEach(doc["root"], [&](const json& n) {
            if (n.contains("id") && n["id"].is_string())
                ids.insert(n["id"].get<std::string>());
        });
    return ids;
}

std::string Unique(const std::set<std::string>& taken, const std::string& base)
{
    if (!taken.count(base))
        return base;
    for (int i = 2;; ++i)
        if (auto c = sdb::Format("{}-{}", base, i); !taken.count(c))
            return c;
}

void RetargetIn(json& j, const std::map<std::string, std::string>& renames)
{
    if (j.is_object())
    {
        if (j.value("kind", "") == "nav" && j.contains("node") && j["node"].is_string())
            if (auto it = renames.find(j["node"].get<std::string>()); it != renames.end())
                j["node"] = it->second;
        for (auto& [k, v] : j.items())
            RetargetIn(v, renames);
    }
    else if (j.is_array())
    {
        for (auto& v : j)
            RetargetIn(v, renames);
    }
}

// Every node of `sub` gets an id that is not in `taken` (which grows), derived from its id or title; nav targets
// inside `sub` follow the renames.
void Uniquify(json& sub, std::set<std::string>& taken)
{
    std::map<std::string, std::string> renames;
    ForEach(sub, [&](json& n) {
        const std::string old = n.contains("id") && n["id"].is_string() ? n["id"].get<std::string>() : std::string();
        const std::string base = !old.empty() ? old : Slug(n.value("title", n.value("type", "item")));
        const std::string id = Unique(taken, base);
        if (!old.empty() && id != old)
            renames[old] = id;
        n["id"] = id;
        taken.insert(id);
        if (n.value("type", "") == "menu" && !n.contains("items"))
            n["items"] = json::array();
    });
    if (!renames.empty())
        RetargetIn(sub, renames);
}

std::optional<Slot> SlotOf(const json& n)
{
    if (!n.contains("at") || !n["at"].is_array() || n["at"].size() != 2)
        return std::nullopt;
    return Slot{n["at"][0].get<int>(), n["at"][1].get<int>()};
}

bool IsMenu(const json* n)
{
    return n && n->value("type", "") == "menu";
}

bool InGrid(const Model& m, Slot s)
{
    return KeyIndex(m, s.row, s.col).has_value();
}

void Retarget(json& doc, const std::string& from, const std::string& to)
{
    std::function<void(json&)> walk = [&](json& j) {
        if (j.is_object())
        {
            if (j.value("kind", "") == "nav" && j.value("node", "") == from)
                j["node"] = to;
            for (auto& [k, v] : j.items())
                walk(v);
        }
        else if (j.is_array())
        {
            for (auto& v : j)
                walk(v);
        }
    };
    walk(doc["root"]);
}

}  // namespace

json NewScenario(const Model& m, const std::string& name)
{
    std::string model;
    for (const char* n :
         {"original", "classic", "mk2", "scissor", "xl", "xl2", "neo", "plus", "plusxl", "mini", "mini2", "pedal"})
        if (FindModelByName(n) == &m)
        {
            model = n;
            break;
        }
    if (model.empty())
        model = sdb::Format("0x{:04X}", m.pid);
    return {{"format", "streamdeck-scenario"},
            {"version", kFormatVersion},
            {"name", name},
            {"model", model},
            {"root", {{"id", "home"}, {"type", "menu"}, {"title", "Home"}, {"items", json::array()}}}};
}

json* FindNode(json& doc, std::string_view id)
{
    json* hit = nullptr;
    if (doc.contains("root"))
        ForEach(doc["root"], [&](json& n) {
            if (!hit && n.value("id", "") == id)
                hit = &n;
        });
    return hit;
}
const json* FindNode(const json& doc, std::string_view id)
{
    return FindNode(const_cast<json&>(doc), id);
}

json* ParentOf(json& doc, std::string_view id)
{
    json* hit = nullptr;
    if (doc.contains("root"))
        ForEach(doc["root"], [&](json& n) {
            if (hit || !n.contains("items"))
                return;
            for (auto& c : n["items"])
                if (c.value("id", "") == id)
                    hit = &n;
        });
    return hit;
}

std::string UniqueId(const json& doc, const std::string& base)
{
    return Unique(AllIds(doc), base.empty() ? "item" : base);
}

std::string Slug(const std::string& title)
{
    std::string s;
    for (char c : title)
    {
        if (std::isalnum(static_cast<unsigned char>(c)))
            s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        else if (c == '.' || c == '_')
            s += c;
        else if (!s.empty() && s.back() != '-')
            s += '-';
    }
    while (!s.empty() && s.back() == '-')
        s.pop_back();
    return s.empty() ? "item" : s;
}

const Model* ModelOf(const json& doc)
{
    return FindModelByName(doc.value("model", "plusxl"));
}

std::optional<Slot> ReservedSlot(const json& doc, std::string_view menu)
{
    auto s = ParseLoose(doc);
    if (!s)
        return std::nullopt;
    const auto path = PathTo(s->root, menu);
    if (path.empty())
        return std::nullopt;
    return ResolveNav(*s, path).backKey;
}

const json* NodeAt(const json& doc, std::string_view menu, Slot at)
{
    const json* m = FindNode(doc, menu);
    if (!IsMenu(m))
        return nullptr;
    for (const auto& c : (*m)["items"])
        if (SlotOf(c) == at)
            return &c;
    return nullptr;
}

std::optional<Slot> FreeSlot(const json& doc, std::string_view menu)
{
    const Model* m = ModelOf(doc);
    if (!m || !IsMenu(FindNode(doc, menu)))
        return std::nullopt;
    const auto reserved = ReservedSlot(doc, menu);
    for (int r = 0; r < m->rows; ++r)
        for (int c = 0; c < m->cols; ++c)
        {
            const Slot s{r, c};
            if (reserved && *reserved == s)
                continue;
            if (!NodeAt(doc, menu, s))
                return s;
        }
    return std::nullopt;
}

Result InsertNode(json& doc, std::string_view parent, std::optional<Slot> at, json node)
{
    json* p = FindNode(doc, parent);
    if (!p)
        return Result::Fail("no node '" + std::string(parent) + "'");
    if (!IsMenu(p))
        return Result::Fail("'" + std::string(parent) + "' is not a menu");
    if (!node.is_object())
        return Result::Fail("a node must be an object");
    const std::string type = node.value("type", "");
    if (type != "menu" && type != "action" && type != "include")
        return Result::Fail("type must be menu, action or include");
    const Model* m = ModelOf(doc);
    if (!at)
        at = FreeSlot(doc, parent);
    if (!at)
        return Result::Fail("'" + std::string(parent) + "' has no free key");
    if (!m || !InGrid(*m, *at))
        return Result::Fail(sdb::Format("slot [{}, {}] is outside the grid", at->row, at->col));
    if (const json* o = NodeAt(doc, parent, *at))
        return Result::Fail(sdb::Format("slot [{}, {}] holds '{}'", at->row, at->col, o->value("id", "")));
    if (auto rs = ReservedSlot(doc, parent); rs && *rs == *at)
        return Result::Fail(sdb::Format("slot [{}, {}] is the back key", at->row, at->col));
    auto taken = AllIds(doc);
    Uniquify(node, taken);
    node["at"] = json::array({at->row, at->col});
    const std::string id = node["id"];
    (*p)["items"].push_back(std::move(node));
    return {true, {}, id};
}

Result RemoveNode(json& doc, std::string_view id)
{
    json* p = ParentOf(doc, id);
    if (!p)
        return Result::Fail(FindNode(doc, id) ? "the root cannot be removed" : "no node '" + std::string(id) + "'");
    auto& items = (*p)["items"];
    for (size_t i = 0; i < items.size(); ++i)
        if (items[i].value("id", "") == id)
        {
            items.erase(i);
            return {true, {}, std::string(id)};
        }
    return Result::Fail("no node '" + std::string(id) + "'");
}

Result MoveNode(json& doc, std::string_view id, std::string_view parent, Slot at, bool swap)
{
    json* from = ParentOf(doc, id);
    json* to = FindNode(doc, parent);
    if (!from)
        return Result::Fail("cannot move '" + std::string(id) + "'");
    if (!IsMenu(to))
        return Result::Fail("'" + std::string(parent) + "' is not a menu");
    {  // a menu cannot move into itself or its descendants
        const json* n = FindNode(doc, id);
        bool inside = false;
        ForEach(*n, [&](const json& c) {
            if (c.value("id", "") == parent)
                inside = true;
        });
        if (inside)
            return Result::Fail("a menu cannot move into itself");
    }
    const Model* m = ModelOf(doc);
    if (!m || !InGrid(*m, at))
        return Result::Fail(sdb::Format("slot [{}, {}] is outside the grid", at.row, at.col));
    if (auto rs = ReservedSlot(doc, parent); rs && *rs == at)
        return Result::Fail(sdb::Format("slot [{}, {}] is the back key", at.row, at.col));
    json* moving = FindNode(doc, id);
    const auto oldSlot = SlotOf(*moving);
    const std::string fromId = from->value("id", "");
    if (const json* occ = NodeAt(doc, parent, at); occ && occ->value("id", "") != id)
    {
        if (!swap)
            return Result::Fail(sdb::Format("slot [{}, {}] holds '{}'", at.row, at.col, occ->value("id", "")));
        const std::string occId = occ->value("id", "");
        json* occNode = FindNode(doc, occId);
        if (!oldSlot)
            return Result::Fail("cannot swap: '" + std::string(id) + "' has no slot");
        (*occNode)["at"] = json::array({oldSlot->row, oldSlot->col});
        if (fromId != std::string(parent))
        {  // the occupant goes to the old menu
            json copy = *occNode;
            RemoveNode(doc, occId);
            (*FindNode(doc, fromId))["items"].push_back(std::move(copy));
        }
    }
    moving = FindNode(doc, id);
    (*moving)["at"] = json::array({at.row, at.col});
    if (fromId != std::string(parent))
    {
        json copy = *moving;
        RemoveNode(doc, id);
        (*FindNode(doc, parent))["items"].push_back(std::move(copy));
    }
    return {true, {}, std::string(id)};
}

Result UpdateNode(json& doc, std::string_view id, const json& patch)
{
    json* n = FindNode(doc, id);
    if (!n)
        return Result::Fail("no node '" + std::string(id) + "'");
    if (!patch.is_object())
        return Result::Fail("the patch must be an object");
    json p = patch;
    std::string newId(id);
    if (p.contains("id"))
    {
        const std::string want = p["id"].is_string() ? p["id"].get<std::string>() : std::string();
        if (want.empty())
            return Result::Fail("id must be a non-empty string");
        if (want != id)
        {
            auto taken = AllIds(doc);
            taken.erase(std::string(id));
            newId = Unique(taken, want);
        }
        p.erase("id");
    }
    if (p.contains("at"))
        return Result::Fail("use move to change 'at'");
    n->merge_patch(p);
    if (newId != id)
    {
        (*n)["id"] = newId;
        Retarget(doc, std::string(id), newId);
    }
    return {true, {}, newId};
}

Result DuplicateNode(json& doc, std::string_view id, std::optional<Slot> at)
{
    const json* n = FindNode(doc, id);
    json* p = ParentOf(doc, id);
    if (!n || !p)
        return Result::Fail("cannot duplicate '" + std::string(id) + "'");
    json copy = *n;
    copy.erase("at");
    return InsertNode(doc, p->value("id", ""), at, std::move(copy));
}

json ExtractSubtree(const json& doc, std::string_view id, std::vector<std::string>* outside)
{
    const json* n = FindNode(doc, id);
    if (!n)
        return nullptr;
    json root = *n;
    root.erase("at");
    if (root.value("type", "") != "menu")
    {  // an action: wrapped, keeping its slot
        json item = *n;
        root = {{"id", std::string(id) + "-fragment"},
                {"type", "menu"},
                {"title", n->value("title", std::string(id))},
                {"fragment", true},
                {"items", json::array({item})}};
    }
    if (outside)
    {
        std::set<std::string> inside;
        ForEach(root, [&](const json& x) { inside.insert(x.value("id", "")); });
        std::function<void(const json&)> walk = [&](const json& j) {
            if (j.is_object())
            {
                if (j.value("kind", "") == "nav" && j.value("to", "") == "node" && !inside.count(j.value("node", "")))
                    outside->push_back(j.value("node", ""));
                for (const auto& [k, v] : j.items())
                    walk(v);
            }
            else if (j.is_array())
                for (const auto& v : j)
                    walk(v);
        };
        walk(root);
    }
    json out{{"format", "streamdeck-scenario"},
             {"version", kFormatVersion},
             {"name", n->value("title", std::string(id))},
             {"model", doc.value("model", "plusxl")}};
    if (doc.contains("settings"))
        out["settings"] = doc["settings"];
    out["root"] = root;
    return out;
}

Result ImportSubtree(json& doc, std::string_view parent, std::optional<Slot> at, const json& fragment, ImportMode mode,
                     std::string_view node)
{
    if (!fragment.is_object() || !fragment.contains("root"))
        return Result::Fail("not a scenario document (no root)");
    const json* src = node.empty() ? &fragment["root"] : FindNode(fragment, node);
    if (!src)
        return Result::Fail("no node '" + std::string(node) + "' in the imported document");
    json sub = *src;
    sub.erase("at");
    if (mode == ImportMode::Menu)
    {
        if (sub.value("fragment", false) && sub["items"].size() == 1)
        {
            sub = sub["items"][0];
            sub.erase("at");
        }  // a wrapped action comes back as itself
        sub.erase("fragment");
        return InsertNode(doc, parent, at, std::move(sub));
    }
    if (!sub.contains("items"))
        return Result::Fail("only a menu's items can be merged");
    // one uniquify over all items so links between them follow renames
    json items = sub["items"];
    json holder{{"id", ""}, {"type", "menu"}, {"items", items}};
    auto taken = AllIds(doc);
    taken.insert("");
    Uniquify(holder, taken);
    std::string last;
    for (auto& it : holder["items"])
    {
        auto want = SlotOf(it);
        const bool free = want && ModelOf(doc) && InGrid(*ModelOf(doc), *want) && !NodeAt(doc, parent, *want) &&
                          !(ReservedSlot(doc, parent) == want);
        it.erase("at");
        const std::string wantedId = it["id"];
        auto r = InsertNode(doc, parent, free ? want : std::nullopt, it);
        if (!r.ok)
            return r;
        last = r.id;
        (void)wantedId;
    }
    return {true, {}, last};
}

Result LinkNode(json& doc, std::string_view id, const std::string& file, const std::string& node)
{
    json* n = FindNode(doc, id);
    if (!n || !ParentOf(doc, id))
        return Result::Fail("cannot link '" + std::string(id) + "' (the root stays)");
    json inc{{"id", std::string(id)}, {"type", "include"}, {"file", file}};
    if (!node.empty())
        inc["node"] = node;
    if (n->contains("at"))
        inc["at"] = (*n)["at"];
    if (n->contains("title"))
        inc["title"] = (*n)["title"];
    *n = inc;
    return {true, {}, std::string(id)};
}

Result InlineNode(json& doc, std::string_view id, json replacement)
{
    json* n = FindNode(doc, id);
    if (!n || !ParentOf(doc, id))
        return Result::Fail("no node '" + std::string(id) + "'");
    const json at = n->contains("at") ? (*n)["at"] : json();
    // ids: the replacement keeps the node's id; its subtree gets unique ones
    auto taken = AllIds(doc);
    ForEach(*n, [&](const json& x) { taken.erase(x.value("id", "")); });
    replacement["id"] = std::string(id);
    taken.erase(std::string(id));
    Uniquify(replacement, taken);
    replacement.erase("included_from");
    ForEach(replacement, [](json& x) { x.erase("included_from"); });
    if (!at.is_null())
        replacement["at"] = at;
    *n = std::move(replacement);
    return {true, {}, std::string(id)};
}

}  // namespace sdb::scn::ops
