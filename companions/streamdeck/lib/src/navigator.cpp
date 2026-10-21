#include "sdb/navigator.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>

namespace sdb::scn
{

Navigator::Navigator(const Scenario& s) : _s(&s), _stack{&s.root} {}

std::vector<std::string> Navigator::Ids() const
{
    std::vector<std::string> v;
    for (const Node* n : _stack)
        v.push_back(n->id);
    return v;
}

bool Navigator::Enter(const Node& menu)
{
    if (!menu.IsMenu())
        return false;
    if (_s->settings.stack)
        _stack.push_back(&menu);
    else
        _stack = {&menu};
    return true;
}

bool Navigator::Back()
{
    if (_stack.size() < 2)
        return false;
    _stack.pop_back();
    return true;
}

void Navigator::Home()
{
    _stack = {&_s->root};
}

bool Navigator::PopTo(size_t depth)
{
    if (depth >= _stack.size())
        return false;
    _stack.resize(depth + 1);
    return true;
}

bool Navigator::GoTo(std::string_view id)
{
    auto path = PathTo(_s->root, id);
    if (path.empty())
        return false;
    if (!path.back()->IsMenu())
        path.pop_back();
    if (_s->settings.stack)
        _stack = std::move(path);
    else
        _stack = {path.back()};
    return true;
}

void Navigator::Rebind(const Scenario& s)
{
    Rebind(s, Ids());
}

void Navigator::Rebind(const Scenario& s, const std::vector<std::string>& old)
{
    _s = &s;
    _stack = {&s.root};
    if (!s.settings.stack)
    {
        if (const Node* n = old.empty() ? nullptr : Find(s.root, old.back()); n && n->IsMenu())
            _stack = {n};
        return;
    }
    for (size_t i = 1; i < old.size(); ++i)
    {
        const Node* next = nullptr;
        for (const auto& c : _stack.back()->items)
            if (c.IsMenu() && c.id == old[i])
                next = &c;
        if (!next)
            break;
        _stack.push_back(next);
    }
}

Page Layout(const Scenario& s, const Navigator& nav, const Model& m)
{
    Page p;
    p.keys.assign(m.keys, nullptr);
    p.dials.assign(m.dials, nullptr);
    const Node& cur = nav.Current();
    for (const auto& c : cur.items)
        if (auto k = KeyIndex(m, c.at.row, c.at.col); k && *k < m.keys)
            p.keys[static_cast<size_t>(*k)] = &c;
    const ResolvedNav rn = ResolveNav(s, nav.Stack());
    p.crumbs = rn.crumbs;
    p.autoExit = rn.autoExit;
    if (rn.backKey)
        if (auto k = KeyIndex(m, rn.backKey->row, rn.backKey->col))
            p.backKey = *k;
    const auto& st = nav.Stack();
    for (auto it = st.rbegin(); it != st.rend(); ++it)
    {
        for (const auto& d : (*it)->dials)
            if (d.index >= 0 && d.index < m.dials && !p.dials[static_cast<size_t>(d.index)])
                p.dials[static_cast<size_t>(d.index)] = &d;
        if ((*it)->nav.dials == "own" || !s.settings.inheritDials)
            break;
    }
    p.strip = &cur.strip;
    for (const Node* n : st)
        p.path.push_back(n->title);
    return p;
}

json DescribePage(const Scenario& s, const Navigator& nav, const Model& m, const Page& p,
                  const std::map<std::string, bool>& toggles)
{
    (void)s;
    json path = json::array();
    for (const Node* n : nav.Stack())
        path.push_back({{"id", n->id}, {"title", n->title}});
    json keys = json::array();
    for (int k = 0; k < static_cast<int>(p.keys.size()); ++k)
    {
        const int row = m.cols ? k / m.cols : 0, col = m.cols ? k % m.cols : 0;
        if (k == p.backKey)
        {
            const std::string parent = nav.Stack().size() >= 2 ? nav.Stack()[nav.Stack().size() - 2]->title : "";
            keys.push_back({{"key", k},
                            {"row", row},
                            {"col", col},
                            {"role", "back"},
                            {"title", "Back to " + parent},
                            {"hold", "home"}});
            continue;
        }
        const Node* n = p.keys[static_cast<size_t>(k)];
        if (!n)
            continue;
        json e{{"key", k},         {"row", row},  {"col", col},
               {"role", "item"},   {"id", n->id}, {"type", n->IsMenu() ? "menu" : "action"},
               {"title", n->title}};
        if (!n->face.label.empty())
            e["label"] = n->face.label;
        if (!n->tags.empty())
            e["tags"] = n->tags;
        if (!n->IsMenu())
        {
            e["kind"] = n->action.kind;
            if (n->then != "stay")
                e["then"] = n->then;
            if (n->action.kind == "toggle")
                e["on"] = toggles.count(n->id) && toggles.at(n->id);
        }
        else
        {
            e["items"] = n->items.size();
        }
        keys.push_back(e);
    }
    json dials = json::array();
    for (size_t i = 0; i < p.dials.size(); ++i)
        if (const Dial* d = p.dials[i])
        {
            json e{{"index", i}};
            if (!d->label.empty())
                e["label"] = d->label;
            if (d->turn)
                e["turn"] = d->turn->kind;
            if (d->press)
                e["press"] = d->press->kind;
            dials.push_back(e);
        }
    json out{{"menu", nav.Current().id},
             {"path", path},
             {"breadcrumbs", ToString(p.crumbs)},
             {"back_key", p.backKey},
             {"can_back", nav.Depth() > 0},
             {"keys", keys},
             {"dials", dials},
             {"model", std::string(m.name)},
             {"grid", {m.rows, m.cols}}};
    if (p.strip)
        out["strip"] = {{"content", p.strip->content}, {"text", p.strip->text}};
    if (p.autoExit > 0)
        out["auto_exit"] = p.autoExit;
    return out;
}

json ListNodes(const json& doc, const json& filter)
{
    json out = json::array();
    if (!doc.contains("root"))
        return out;
    const std::string tag = filter.value("tag", ""), type = filter.value("type", ""), kind = filter.value("kind", ""),
                      menu = filter.value("menu", "");
    std::string text = filter.value("text", "");
    for (auto& c : text)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    auto lower = [](std::string s) {
        for (auto& c : s)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    };
    std::function<void(const json&, std::vector<std::string>&)> walk = [&](const json& n,
                                                                           std::vector<std::string>& path) {
        const std::string id = n.value("id", ""), parent = path.empty() ? std::string() : path.back();
        const std::string ntype = n.value("type", ""),
                          nkind = n.contains("do") ? n["do"].value("kind", "") : std::string();
        std::vector<std::string> tags;
        if (n.contains("tags") && n["tags"].is_array())
            for (const auto& t : n["tags"])
                if (t.is_string())
                    tags.push_back(t.get<std::string>());
        bool ok = (tag.empty() || std::find(tags.begin(), tags.end(), tag) != tags.end()) &&
                  (type.empty() || ntype == type) && (kind.empty() || nkind == kind) &&
                  (menu.empty() || parent == menu) &&
                  (text.empty() || lower(id).find(text) != std::string::npos ||
                   lower(n.value("title", "")).find(text) != std::string::npos);
        if (ok)
        {
            json e{
                {"id", id}, {"type", ntype}, {"title", n.value("title", id)}, {"menu", parent}, {"depth", path.size()}};
            if (n.contains("at"))
                e["at"] = n["at"];
            if (!nkind.empty())
                e["kind"] = nkind;
            if (!tags.empty())
                e["tags"] = tags;
            if (ntype == "menu")
                e["items"] = n.value("items", json::array()).size();
            out.push_back(e);
        }
        path.push_back(id);
        for (const auto& c : n.value("items", json::array()))
            walk(c, path);
        path.pop_back();
    };
    std::vector<std::string> path;
    walk(doc["root"], path);
    return out;
}

}  // namespace sdb::scn
