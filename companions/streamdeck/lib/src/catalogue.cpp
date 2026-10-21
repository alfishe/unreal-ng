#include "sdb/catalogue.h"

#include <fstream>
#include <functional>

#include "sdb/datadir.h"
#include "sdb/http.h"

namespace sdb::cat
{

const Entry* Catalogue::Find(const std::string& id) const
{
    for (const auto& e : actions)
        if (e.id == id)
            return &e;
    return nullptr;
}

std::optional<Catalogue> Parse(const json& doc)
{
    const json* rows = nullptr;
    if (doc.is_array())
        rows = &doc;
    else if (doc.is_object() && doc.contains("actions") && doc["actions"].is_array())
        rows = &doc["actions"];
    if (!rows)
        return std::nullopt;
    Catalogue c;
    for (const auto& r : *rows)
    {
        if (!r.is_object() || !r.contains("id") || !r["id"].is_string() || r["id"].get<std::string>().empty())
            continue;
        Entry e;
        e.id = r["id"].get<std::string>();
        e.label = r.contains("label") && r["label"].is_string() ? r["label"].get<std::string>() : e.id;
        e.group = r.contains("group") && r["group"].is_string() ? r["group"].get<std::string>() : std::string();
        e.kind = r.contains("kind") && r["kind"].is_string() ? r["kind"].get<std::string>() : "press";
        e.held = r.contains("held") && r["held"].is_boolean() && r["held"].get<bool>();
        e.gui = r.contains("gui") && r["gui"].is_boolean() && r["gui"].get<bool>();
        e.params = r.contains("params") && r["params"].is_string() ? r["params"].get<std::string>() : std::string();
        c.actions.push_back(std::move(e));
    }
    return c;
}

json ToJson(const Catalogue& c)
{
    json rows = json::array();
    for (const auto& e : c.actions)
    {
        json r{{"id", e.id}, {"label", e.label}, {"group", e.group}, {"kind", e.kind}, {"held", e.held}, {"gui", e.gui}};
        if (!e.params.empty())
            r["params"] = e.params;
        rows.push_back(std::move(r));
    }
    return json{{"format", kFormat}, {"version", 1}, {"actions", rows}};
}

std::optional<Catalogue> Load(const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file);
    if (!in)
    {
        if (error)
            *error = "cannot read " + file.string();
        return std::nullopt;
    }
    const json j = json::parse(in, nullptr, false);
    auto c = j.is_discarded() ? std::nullopt : Parse(j);
    if (!c && error)
        *error = file.string() + " is not an action catalogue";
    return c;
}

std::optional<Catalogue> Fetch(const std::string& webapi, std::string* error, int timeoutMs)
{
    const HttpResponse r = HttpRequest(webapi, "GET", "/api/v1/controllers/actions", {}, {}, timeoutMs);
    if (!r.Ok())
    {
        if (error)
            *error = r.status == 0 ? (r.error.empty() ? "no answer from " + webapi : r.error)
                                   : "GET /api/v1/controllers/actions -> " + std::to_string(r.status);
        return std::nullopt;
    }
    const json j = json::parse(r.body, nullptr, false);
    auto c = j.is_discarded() ? std::nullopt : Parse(j);
    if (!c && error)
        *error = "the host's answer is not an action catalogue";
    return c;
}

std::filesystem::path DefaultFile()
{
    const auto root = data::Root();
    return root.empty() ? std::filesystem::path() : root / "streamdeck" / "catalogue.json";
}

std::vector<scn::Issue> Check(const scn::Scenario& s, const Catalogue& c)
{
    std::vector<scn::Issue> out;
    // every action step, through the composition kinds; `dial` = a dial's turn (it sends a delta)
    std::function<void(const json&, const std::string&, bool, int)> step = [&](const json& a, const std::string& node,
                                                                               bool turn, int depth) {
        if (!a.is_object() || depth > scn::kMaxMultiDepth)
            return;
        const std::string kind = a.value("kind", "");
        if (kind == "action")
        {
            const std::string id = a.value("id", "");
            const Entry* e = c.Find(id);
            if (!e)
            {
                out.push_back({node, "action '" + id + "' is not in the host's catalogue", false});
                return;
            }
            const json v = a.contains("value") ? a["value"] : json();
            // a held action with a "hold?" param (input.key, input.joystick) is a tap unless the step holds it
            const bool tap = e->params.find("hold?") != std::string::npos &&
                             !(v.is_object() && v.value("hold", false));
            if (e->held && !a.value("hold", false) && !turn && !tap)
                out.push_back({node, "'" + id + "' acts on the release too: add \"hold\": true", false});
            const bool delta = v.is_object() && v.contains("d");
            if (delta && e->kind != "delta")
                out.push_back({node, "'" + id + "' is a " + e->kind + " action: a dial delta (d) does not fit", false});
            if (turn && e->kind == "delta" && !delta)
                out.push_back({node, "'" + id + "' on a dial turn needs \"value\": {\"d\": \"$ticks\"}", false});
            if (e->kind == "text" && !(v.is_string() || (v.is_object() && v.contains("text"))))
                out.push_back({node, "'" + id + "' is a text action: value is the text", false});
            return;
        }
        for (const char* list : {"steps", "try"})
            if (a.contains(list) && a[list].is_array())
                for (const auto& st : a[list])
                    step(st, node, turn, depth + 1);
        for (const char* f : {"left", "right", "on", "off"})
            if (a.contains(f))
                step(a[f], node, false, depth + 1);  // dir sides run once per detent: a press each
    };
    std::function<void(const scn::Node&)> walk = [&](const scn::Node& n) {
        if (n.IsMenu())
        {
            for (const auto& d : n.dials)
            {
                if (d.turn)
                    step(d.turn->spec, n.id, true, 0);
                if (d.press)
                    step(d.press->spec, n.id, false, 0);
            }
            for (const auto& c2 : n.items)
                walk(c2);
        }
        else
            step(n.action.spec, n.id, false, 0);
    };
    walk(s.root);
    return out;
}

}  // namespace sdb::cat
