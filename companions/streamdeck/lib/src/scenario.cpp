#include "sdb/scenario.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <map>
#include <mutex>
#include <set>

#include "sdb/actions.h"
#include "sdb/datadir.h"
#include "sdb/format.h"

namespace sdb::scn
{

namespace
{

std::optional<draw::Color> Hex(const json& j)
{
    if (!j.is_string())
        return std::nullopt;
    std::string h = j.get<std::string>();
    if (!h.empty() && h[0] == '#')
        h.erase(0, 1);
    if (h.size() != 6 || h.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
        return std::nullopt;
    const auto v = std::stoul(h, nullptr, 16);
    return draw::Color{static_cast<std::uint8_t>(v >> 16), static_cast<std::uint8_t>(v >> 8),
                       static_cast<std::uint8_t>(v)};
}
std::string Hex(draw::Color c)
{
    return sdb::Format("{:02X}{:02X}{:02X}", c.r, c.g, c.b);
}

std::optional<Slot> ParseSlot(const json& j)
{
    if (!j.is_array() || j.size() != 2 || !j[0].is_number_integer() || !j[1].is_number_integer())
        return std::nullopt;
    return Slot{j[0].get<int>(), j[1].get<int>()};
}
json SlotJson(Slot s)
{
    return json::array({s.row, s.col});
}

struct Parser
{
    std::vector<Issue>& issues;
    bool failed = false;
    void Error(const std::string& node, const std::string& msg)
    {
        issues.push_back({node, msg, true});
        failed = true;
    }

    Face ParseFace(const json& j, const std::string& id)
    {
        Face f;
        if (!j.is_object())
            return f;
        f.label = j.value("label", "");
        f.icon = j.value("icon", "");
        f.svg = j.value("svg", "");
        f.image = j.value("image", "");
        f.fit = j.value("fit", "contain");
        if (j.contains("bg"))
        {
            if (auto c = Hex(j["bg"]))
                f.bg = *c;
            else
                Error(id, "face.bg is not a RRGGBB colour");
        }
        if (j.contains("fg"))
        {
            if (auto c = Hex(j["fg"]))
                f.fg = *c;
            else
                Error(id, "face.fg is not a RRGGBB colour");
        }
        return f;
    }

    std::optional<Action> ParseAction(const json& j, const std::string& id)
    {
        if (!j.is_object() || !j.contains("kind") || !j["kind"].is_string())
        {
            Error(id, "an action needs an object with \"kind\"");
            return std::nullopt;
        }
        return Action{j["kind"].get<std::string>(), j};
    }

    Node ParseNode(const json& j, bool root)
    {
        Node n;
        if (!j.is_object())
        {
            Error("", "a node must be an object");
            return n;
        }
        n.id = j.value("id", "");
        n.title = j.value("title", n.id);
        if (j.contains("tags"))
        {
            const json& t = j["tags"];
            if (t.is_string())
            {  // "a, b" accepted
                std::string cur;
                for (char c : t.get<std::string>() + ",")
                {
                    if (c == ',' || c == ';')
                    {
                        while (!cur.empty() && cur.back() == ' ')
                            cur.pop_back();
                        if (!cur.empty())
                            n.tags.push_back(cur);
                        cur.clear();
                    }
                    else if (!(cur.empty() && c == ' '))
                        cur += c;
                }
            }
            else if (t.is_array())
            {
                for (const auto& x : t)
                {
                    if (x.is_string() && !x.get<std::string>().empty())
                        n.tags.push_back(x.get<std::string>());
                    else
                        Error(n.id, "tags must be non-empty strings");
                }
            }
            else
                Error(n.id, "tags must be an array of strings");
        }
        const std::string type = j.value("type", "");
        if (type == "menu")
            n.type = Node::Type::Menu;
        else if (type == "action")
            n.type = Node::Type::Action;
        else if (type == "include")
        {  // a link to a subtree in another file: resolved by io::resolveIncludes before playing
            n.type = Node::Type::Action;
            n.action = Action{"include", j};
            if (!j.contains("file"))
                Error(n.id, "include needs 'file'");
        }
        else
            Error(n.id, "type must be \"menu\", \"action\" or \"include\"");
        if (root && n.type != Node::Type::Menu)
            Error(n.id, "the root must be a menu");
        if (!root && j.contains("at"))
            n.at = ParseSlot(j["at"]).value_or(Slot{});
        n.face = ParseFace(j.value("face", json::object()), n.id);
        if (n.IsMenu())
        {
            if (j.contains("context"))
            {
                if (j["context"].is_string())
                    n.context = j["context"].get<std::string>();
                else
                    Error(n.id, "context must be a string");
            }
            for (const auto& it : j.value("items", json::array()))
                n.items.push_back(ParseNode(it, false));
            for (const auto& d : j.value("dials", json::array()))
            {
                Dial dl;
                dl.index = d.value("index", -1);
                dl.label = d.value("label", "");
                if (d.contains("turn"))
                    dl.turn = ParseAction(d["turn"], n.id);
                if (d.contains("press"))
                    dl.press = ParseAction(d["press"], n.id);
                n.dials.push_back(dl);
            }
            if (const auto st = j.value("strip", json::object()); st.is_object())
            {
                n.strip.content = st.value("content", "title");
                n.strip.text = st.value("text", "");
                n.strip.image = st.value("image", "");
                n.strip.status = st.value("status", "");
                n.strip.fps = st.value("fps", 50.0);
                if (st.contains("target"))
                {
                    if (st["target"].is_boolean())
                        n.strip.target = st["target"].get<bool>();
                    else
                        Error(n.id, "strip.target must be true or false");
                }
                static const std::set<std::string> statuses{"ttd", "run", "tape", "target"};
                if (!n.strip.status.empty() && !statuses.count(n.strip.status))
                    Error(n.id, "strip.status: ttd, run, tape or target (or none)");
                if (n.strip.fps <= 0)
                    Error(n.id, "strip.fps must be > 0");
                static const std::set<std::string> kinds{"breadcrumbs", "title", "text", "image", "none"};
                if (!kinds.count(n.strip.content))
                    Error(n.id, "strip.content must be breadcrumbs, title, text, image or none");
            }
            if (const auto nv = j.value("nav", json::object()); nv.is_object())
            {
                if (nv.contains("breadcrumbs"))
                {
                    const auto c = CrumbsByName(nv.value("breadcrumbs", ""));
                    if (c)
                        n.nav.crumbs = *c;
                    else
                        Error(n.id, "bad breadcrumbs '" + nv["breadcrumbs"].dump() + "' (inherit, strip, key, none)");
                }
                if (nv.contains("back"))
                {
                    if (nv["back"].is_null() || nv["back"] == false)
                        n.nav.back.mode = Back::Mode::None;  // false: settable by a merge patch
                    else if (auto s = ParseSlot(nv["back"]))
                        n.nav.back = {Back::Mode::At, *s};
                    else
                        Error(n.id, "nav.back must be [row, col] or null");
                }
                n.nav.autoExit = nv.value("auto_exit", -1);
                n.nav.dials = nv.value("dials", "inherit");
                if (n.nav.dials != "inherit" && n.nav.dials != "own")
                    Error(n.id, "nav.dials must be inherit or own");
            }
        }
        else if (type == "include")
        {
            // placeholder; nothing else to read
        }
        else
        {
            if (j.contains("do"))
            {
                if (auto a = ParseAction(j["do"], n.id))
                    n.action = *a;
            }
            else
                Error(n.id, "an action node needs \"do\"");
            n.then = j.value("then", "stay");
            if (n.then != "stay" && n.then != "back" && n.then != "home")
                Error(n.id, "then must be stay, back or home");
            if (j.contains("face_on"))
                n.faceOn = ParseFace(j["face_on"], n.id);
            if (j.contains("state"))
            {
                if (j["state"].is_string())
                    n.state = j["state"].get<std::string>();
                else
                    Error(n.id, "state must be a readout path (a string)");
            }
        }
        if (j.contains("activity"))
        {
            if (j["activity"].is_boolean())
                n.activity = j["activity"].get<bool>();
            else
                Error(n.id, "activity must be true or false");
        }
        return n;
    }
};

// ---------------------------------------------------------------- validation
struct Validator
{
    const Scenario& s;
    std::vector<Issue> out;
    std::set<std::string> ids;

    void Error(const std::string& node, const std::string& msg)
    {
        out.push_back({node, msg, true});
    }

    void Collect(const Node& n, std::map<std::string, int>& seen)
    {
        if (n.id.empty())
            Error(n.title, "missing id");
        else if (++seen[n.id] == 2)
            Error(n.id, "duplicate id '" + n.id + "'");
        ids.insert(n.id);
        for (const auto& c : n.items)
            Collect(c, seen);
    }

    void Warn(const std::string& node, const std::string& msg)
    {
        out.push_back({node, msg, false});
    }

    // A scenario path (scenarios.md §3): a known root, else a relative path (the scenario's folder) or, with a
    // warning, an absolute one
    void CheckPath(const std::string& node, const std::string& field, std::string path)
    {
        if (path.empty())
            return;
        if (const size_t hash = path.find('#'); hash != std::string::npos)
            path.erase(hash);  // #xywh=...
        if (path[0] == '$')
        {
            const std::string root = data::RootOf(path);
            const auto& roots = data::PathRoots();
            if (std::find(roots.begin(), roots.end(), root) == roots.end())
                Error(node, field + ": unknown path root '$" + root + "' ($data, $icons, $testdata, $user, $scenario)");
            return;
        }
        const std::filesystem::path p(path);
        if (p.is_absolute() || p.has_root_directory() || p.has_root_name())
            Warn(node, field + ": absolute path '" + path + "' (use $data, $testdata, $user or $scenario)");
    }
    void CheckFace(const Face& f, const std::string& node, const std::string& what)
    {
        CheckPath(node, what + ".image", f.image);
        CheckPath(node, what + ".svg", f.svg);
    }
    // "$root/..." strings inside an action's value (media.open path, snapshot.save path, ...)
    void CheckValuePaths(const json& v, const std::string& node)
    {
        if (v.is_string())
        {
            const std::string t = v.get<std::string>();
            if (t.size() > 2 && t[0] == '$' && t.find_first_of("/\\") != std::string::npos)
                CheckPath(node, "value", t);
        }
        else if (v.is_object() || v.is_array())
            for (const auto& x : v)
                CheckValuePaths(x, node);
    }

    void CheckAction(const Action& a, const std::string& id, int depth)
    {
        if (depth > kMaxMultiDepth)
        {
            Error(id, sdb::Format("multi actions nested deeper than {}", kMaxMultiDepth));
            return;
        }
        const json& j = a.spec;
        auto need = [&](const char* field) {
            if (!j.contains(field))
                Error(id, a.kind + " needs '" + field + "'");
        };
        auto sub = [&](const json& step) {
            if (!step.is_object() || !step.contains("kind"))
            {
                Error(id, a.kind + ": every step needs \"kind\"");
                return;
            }
            CheckAction(Action{step.value("kind", ""), step}, id, depth + 1);
        };
        if (a.kind == "action")
        {
            const std::string aid = j.contains("id") && j["id"].is_string() ? j["id"].get<std::string>() : "";
            if (aid.empty())
                Error(id, "action needs 'id' (a catalogue id, e.g. run.pause_toggle)");
            else if (aid.find('.') == std::string::npos || aid.front() == '.' || aid.back() == '.' ||
                     aid.find_first_of(" \t/") != std::string::npos)
                Error(id, "action id '" + aid + "' is not a catalogue id (group.name)");
            if (j.contains("hold") && !j["hold"].is_boolean())
                Error(id, "action: hold must be true or false");
            if (j.contains("params") && !j["params"].is_object())
                Error(id, "action: params must be an object");
            if (j.contains("value"))
            {
                const json& v = j["value"];
                if (v.is_null() || v.is_array())
                    Error(id, "action: value must be an object, a string, a number or a boolean");
                CheckValuePaths(v, id);
            }
        }
        else if (a.kind == "webapi")
            need("path");
        else if (a.kind == "http")
            need("url");
        else if (a.kind == "delay")
            need("ms");
        else if (a.kind == "shell")
            need("command");
        else if (a.kind == "open")
            need("target");
        else if (a.kind == "keys")
        {
            if (!j.contains("combo") && !j.contains("text"))
                Error(id, "keys needs 'combo' or 'text'");
            std::string why;
            if (j.contains("combo") && j["combo"].is_string() && !j["combo"].get<std::string>().empty() &&
                j["combo"].get<std::string>().find('$') == std::string::npos &&
                !ParseKeyCombo(j["combo"].get<std::string>(), &why))
                Error(id, "keys: " + why);
        }
        else if (a.kind == "include")
            Error(id, "include of '" + j.value("file", "") + "' is not resolved (load the file with its includes)");
        else if (a.kind == "brightness")
        {
            if (!j.contains("value") && !j.contains("delta"))
                Error(id, "brightness needs 'value' or 'delta'");
        }
        else if (a.kind == "multi")
        {
            if (!j.contains("steps") || !j["steps"].is_array())
                Error(id, "multi needs 'steps' (array)");
            else
                for (const auto& st : j["steps"])
                    sub(st);
        }
        else if (a.kind == "ttd_seek")
        {  // a dial: frames a detent, min..max with the turning speed
            if (s.version >= 2)
                Warn(id, "ttd_seek is a v1 kind (kept by a shim): use the action ttd.seek_by");
            const bool secs = j.value("unit", "frames") == "seconds";
            const double lo = j.value("min", 1.0), hi = j.value("max", lo);
            if (secs ? lo <= 0 || hi < lo : lo < 1 || hi < lo)
                Error(id, secs ? "ttd_seek (seconds) needs 0 < min <= max" : "ttd_seek needs 1 <= min <= max");
            if (j.contains("unit") && !secs && j.value("unit", "") != "frames")
                Error(id, "ttd_seek unit: frames or seconds");
        }
        else if (a.kind == "run_toggle")
        {  // no fields
            if (s.version >= 2)
                Warn(id, "run_toggle is a v1 kind (kept by a shim): use the action run.pause_toggle");
        }
        else if (a.kind == "first")
        {
            if (!j.contains("try") || !j["try"].is_array() || j["try"].empty())
                Error(id, "first needs 'try' (array of actions)");
            else
                for (const auto& st : j["try"])
                    sub(st);
        }
        else if (a.kind == "dir")
        {  // dial turns: left / right
            if (!j.contains("left") && !j.contains("right"))
                Error(id, "dir needs 'left' and / or 'right'");
            for (const char* f : {"left", "right"})
                if (j.contains(f))
                    sub(j[f]);
        }
        else if (a.kind == "toggle")
        {
            for (const char* f : {"on", "off"})
            {
                if (!j.contains(f))
                    Error(id, std::string("toggle needs '") + f + "'");
                else
                    sub(j[f]);
            }
        }
        else if (a.kind == "nav")
        {
            const std::string to = j.value("to", "");
            if (to == "node")
            {
                const std::string target = j.value("node", "");
                if (!ids.count(target))
                    Error(id, "nav target '" + target + "' does not exist");
            }
            else if (to == "scenario")
                need("file");
            else if (to != "back" && to != "home")
                Error(id, "nav.to must be back, home, node or scenario");
        }
        else
            Error(id, "unknown action kind '" + a.kind + "'");
    }

    void Menu(const Node& n, std::vector<const Node*>& path)
    {
        path.push_back(&n);
        CheckFace(n.face, n.id, "face");
        CheckPath(n.id, "strip.image", n.strip.image);
        if (!n.context.empty() && n.context.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") !=
                                      std::string::npos)
            Error(n.id, "context '" + n.context + "': lower-case letters, digits, '.', '_', '-'");
        const Model& m = *s.model;
        const ResolvedNav rn = ResolveNav(s, path);
        std::map<std::pair<int, int>, std::string> used;
        for (const auto& c : n.items)
        {
            if (!c.at.Valid())
                Error(c.id, "missing or bad 'at' [row, col]");
            else if (!KeyIndex(m, c.at.row, c.at.col))
                Error(c.id, sdb::Format("slot [{}, {}] is outside the {}x{} grid of {}", c.at.row, c.at.col, m.rows,
                                        m.cols, m.name));
            else
            {
                auto [it, fresh] = used.emplace(std::make_pair(c.at.row, c.at.col), c.id);
                if (!fresh)
                    Error(c.id, sdb::Format("same slot [{}, {}] as '{}'", c.at.row, c.at.col, it->second));
                if (rn.backKey && *rn.backKey == c.at)
                    Error(c.id, sdb::Format("slot [{}, {}] is under the back key of '{}'", c.at.row, c.at.col, n.id));
            }
        }
        std::set<int> dialSeen;
        for (const auto& d : n.dials)
        {
            if (m.dials == 0)
                Error(n.id, std::string("dial bindings on ") + std::string(m.name) + ", which has no dials");
            else if (d.index < 0 || d.index >= m.dials)
                Error(n.id, sdb::Format("dial index {} outside 0..{}", d.index, m.dials - 1));
            else if (!dialSeen.insert(d.index).second)
                Error(n.id, sdb::Format("dial {} bound twice", d.index));
            if (d.turn)
                CheckAction(*d.turn, n.id, 0);
            if (d.press)
                CheckAction(*d.press, n.id, 0);
        }
        for (const auto& c : n.items)
        {
            if (c.IsMenu())
                Menu(c, path);
            else
            {
                CheckAction(c.action, c.id, 0);
                CheckFace(c.face, c.id, "face");
                if (c.faceOn)
                    CheckFace(*c.faceOn, c.id, "face_on");
                if (!c.state.empty() && !IsReadoutPath(c.state))
                    Error(c.id, "state '" + c.state + "' is not a readout path (run.state.paused, toggle.<id>, ...)");
                if (!c.state.empty() && !c.faceOn)
                    Warn(c.id, "state without face_on: the face does not change");
            }
        }
        path.pop_back();
    }
};

}  // namespace

const char* ToString(Crumbs c)
{
    switch (c)
    {
        case Crumbs::Inherit:
            return "inherit";
        case Crumbs::Auto:
            return "auto";
        case Crumbs::Strip:
            return "strip";
        case Crumbs::Key:
            return "key";
        case Crumbs::None:
            return "none";
    }
    return "?";
}

std::optional<Crumbs> CrumbsByName(std::string_view s)
{
    if (s == "inherit")
        return Crumbs::Inherit;
    if (s == "auto")
        return Crumbs::Auto;
    if (s == "strip")
        return Crumbs::Strip;
    if (s == "key")
        return Crumbs::Key;
    if (s == "none")
        return Crumbs::None;
    return std::nullopt;
}

std::span<const std::string_view> ActionKinds()
{
    static constexpr std::string_view kKinds[] = {"action", "webapi",     "http",  "multi", "toggle",
                                                  "first",  "dir",        "delay", "ttd_seek", "run_toggle",
                                                  "shell",  "open",       "keys",  "nav",      "brightness"};
    return kKinds;
}

namespace
{
std::mutex g_hostMutex;
std::string g_defaultHost = "http://127.0.0.1:8090";
}  // namespace

std::string DefaultHost()
{
    std::lock_guard l(g_hostMutex);
    return g_defaultHost;
}

void SetDefaultHost(const std::string& url)
{
    std::lock_guard l(g_hostMutex);
    g_defaultHost = url.empty() ? std::string("http://127.0.0.1:8090") : url;
}

bool IsReadoutPath(std::string_view path)
{
    // two parts at least, each [a-z0-9_-]+ (a part may hold upper case for feature names: toggle.feature.TurboSound)
    if (path.empty() || path.front() == '.' || path.back() == '.' || path.find("..") != std::string_view::npos ||
        path.find('.') == std::string_view::npos)
        return false;
    for (char c : path)
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '_' || c == '-'))
            return false;
    return std::islower(static_cast<unsigned char>(path.front())) != 0;
}

std::optional<Scenario> Parse(const json& doc, std::vector<Issue>& issues)
{
    Parser p{issues};
    if (!doc.is_object())
    {
        p.Error("", "a scenario must be a JSON object");
        return std::nullopt;
    }
    if (doc.contains("format") && doc["format"] != "streamdeck-scenario")
        p.Error("", "format must be \"streamdeck-scenario\"");
    const json& ver = doc.contains("version") ? doc["version"] : json(kFormatVersion);
    if (!ver.is_number_integer() || ver.get<int>() < 1)
        p.Error("", "version must be 1 or 2");
    else if (ver.get<int>() > kFormatVersion)
        p.Error("", "made by a newer version (format " + std::to_string(ver.get<int>()) + ")");
    Scenario s;
    s.version = ver.is_number_integer() ? std::clamp(ver.get<int>(), 1, kFormatVersion) : kFormatVersion;
    s.name = doc.value("name", "");
    const std::string model = doc.value("model", "plusxl");
    s.model = FindModelByName(model);
    if (!s.model)
        p.Error("", "unknown model '" + model + "'");
    const json st = doc.value("settings", json::object());
    s.settings.brightness = st.value("brightness", s.settings.brightness);
    s.settings.webapi = st.value("webapi", s.settings.webapi);
    s.settings.emulator = st.value("emulator", s.settings.emulator);
    s.settings.companionFile = st.value("companion_file", "");
    if (s.version >= 2)
        for (const char* legacy : {"webapi", "emulator", "companion_file"})
            if (st.contains(legacy))
                p.issues.push_back({"", std::string("settings.") + legacy +
                                            " is v1: the host, the target and the token are the daemon's (still used)",
                                    false});
    if (st.contains("target"))
    {
        const json& t = st["target"];
        if (t == "follow" || (t.is_object() && t.size() == 1 && t.contains("view") && t["view"].is_number_integer() &&
                              t["view"].get<int>() >= 1))
            s.settings.target = t;
        else
            p.Error("", "settings.target must be \"follow\" or {\"view\": n} (n >= 1)");
    }
    if (st.contains("activity"))
    {
        const json& a = st["activity"];
        if (!a.is_object())
            p.Error("", "settings.activity must be an object");
        else
        {
            Activity& act = s.settings.activity;
            for (const auto& [k, v] : a.items())
            {
                if (k == "enabled" && v.is_boolean())
                    act.enabled = v.get<bool>();
                else if (k == "flash_keys" && v.is_boolean())
                    act.flashKeys = v.get<bool>();
                else if (k == "strip" && (v == "none" || v == "line"))
                    act.strip = v.get<std::string>();
                else if (k == "seconds" && v.is_number() && v.get<double>() > 0)
                    act.seconds = v.get<double>();
                else if (k == "ignore" && v.is_array() &&
                         std::all_of(v.begin(), v.end(), [](const json& x) { return x.is_string(); }))
                    act.ignore = v.get<std::vector<std::string>>();
                else
                    p.Error("", "settings.activity." + k +
                                    ": enabled / flash_keys (true, false), strip (none, line), seconds (> 0), ignore "
                                    "(action ids)");
            }
        }
    }
    const json nv = st.value("navigation", json::object());
    s.settings.stack = nv.value("stack", true);
    if (nv.contains("breadcrumbs"))
    {
        auto c = CrumbsByName(nv.value("breadcrumbs", ""));
        if (!c || *c == Crumbs::Inherit)
            p.Error("", "settings.navigation.breadcrumbs must be auto, strip, key or none");
        else
            s.settings.crumbs = *c;
    }
    if (nv.contains("back"))
    {
        if (nv["back"].is_null())
            s.settings.back = {Back::Mode::None, {}};
        else if (auto sl = ParseSlot(nv["back"]))
            s.settings.back = {Back::Mode::At, *sl};
        else
            p.Error("", "settings.navigation.back must be [row, col] or null");
    }
    s.settings.homeOnHold = nv.value("home_on_hold", true);
    s.settings.autoExit = nv.value("auto_exit", 0);
    s.settings.inheritDials = nv.value("inherit_dials", true);
    if (!doc.contains("root"))
    {
        p.Error("", "missing root menu");
        return std::nullopt;
    }
    s.root = p.ParseNode(doc["root"], true);
    if (!s.model || p.failed)
        return std::nullopt;
    auto v = Validate(s);
    bool err = false;
    for (auto& i : v)
    {
        err |= i.error;
        issues.push_back(std::move(i));
    }
    if (err)
        return std::nullopt;
    return s;
}

std::optional<Scenario> ParseLoose(const json& doc)
{
    std::vector<Issue> issues;
    Parser p{issues};
    if (!doc.is_object() || !doc.contains("root"))
        return std::nullopt;
    Scenario s;
    s.name = doc.value("name", "");
    s.model = FindModelByName(doc.value("model", "plusxl"));
    if (doc.contains("version") && doc["version"].is_number_integer())
        s.version = std::clamp(doc["version"].get<int>(), 1, kFormatVersion);
    const json nv = doc.value("settings", json::object()).value("navigation", json::object());
    s.settings.stack = nv.value("stack", true);
    if (auto c = CrumbsByName(nv.value("breadcrumbs", "auto")); c && *c != Crumbs::Inherit)
        s.settings.crumbs = *c;
    if (nv.contains("back"))
    {
        if (nv["back"].is_null())
            s.settings.back = {Back::Mode::None, {}};
        else if (auto sl = ParseSlot(nv["back"]))
            s.settings.back = {Back::Mode::At, *sl};
    }
    s.root = p.ParseNode(doc["root"], true);
    if (!s.model)
        return std::nullopt;
    return s;
}

std::vector<Issue> Validate(const Scenario& s)
{
    Validator v{s, {}, {}};
    if (!s.model)
    {
        v.Error("", "no model");
        return v.out;
    }
    std::map<std::string, int> seen;
    v.Collect(s.root, seen);
    std::vector<const Node*> path;
    if (s.root.IsMenu())
        v.Menu(s.root, path);
    return v.out;
}

// ---------------------------------------------------------------- serialization
namespace
{
json FaceJson(const Face& f)
{
    json j = json::object();
    if (!f.label.empty())
        j["label"] = f.label;
    if (!f.icon.empty())
        j["icon"] = f.icon;
    if (!f.svg.empty())
        j["svg"] = f.svg;
    if (!f.image.empty())
        j["image"] = f.image;
    if (f.fit != "contain")
        j["fit"] = f.fit;
    const Face d;
    if (Hex(f.bg) != Hex(d.bg))
        j["bg"] = Hex(f.bg);
    if (Hex(f.fg) != Hex(d.fg))
        j["fg"] = Hex(f.fg);
    return j;
}
}  // namespace

json ToJson(const Node& n)
{
    json j{{"id", n.id}, {"type", n.IsMenu() ? "menu" : "action"}};
    if (n.title != n.id)
        j["title"] = n.title;
    if (!n.tags.empty())
        j["tags"] = n.tags;
    if (n.at.Valid())
        j["at"] = SlotJson(n.at);
    if (auto f = FaceJson(n.face); !f.empty())
        j["face"] = f;
    if (!n.activity)
        j["activity"] = false;
    if (n.IsMenu())
    {
        if (!n.context.empty())
            j["context"] = n.context;
        json nav = json::object();
        if (n.nav.crumbs != Crumbs::Inherit)
            nav["breadcrumbs"] = ToString(n.nav.crumbs);
        if (n.nav.back.mode == Back::Mode::At)
            nav["back"] = SlotJson(n.nav.back.at);
        else if (n.nav.back.mode == Back::Mode::None)
            nav["back"] = nullptr;
        if (n.nav.autoExit >= 0)
            nav["auto_exit"] = n.nav.autoExit;
        if (n.nav.dials != "inherit")
            nav["dials"] = n.nav.dials;
        if (!nav.empty())
            j["nav"] = nav;
        if (n.strip.content != "title" || !n.strip.text.empty() || !n.strip.image.empty() || !n.strip.status.empty() ||
            n.strip.target)
        {
            json st{{"content", n.strip.content}};
            if (!n.strip.text.empty())
                st["text"] = n.strip.text;
            if (!n.strip.image.empty())
                st["image"] = n.strip.image;
            if (!n.strip.status.empty())
            {
                st["status"] = n.strip.status;
                st["fps"] = n.strip.fps;
            }
            if (n.strip.target)
                st["target"] = *n.strip.target;
            j["strip"] = st;
        }
        if (!n.dials.empty())
        {
            json ds = json::array();
            for (const auto& d : n.dials)
            {
                json dj{{"index", d.index}};
                if (!d.label.empty())
                    dj["label"] = d.label;
                if (d.turn)
                    dj["turn"] = d.turn->spec;
                if (d.press)
                    dj["press"] = d.press->spec;
                ds.push_back(dj);
            }
            j["dials"] = ds;
        }
        json items = json::array();
        for (const auto& c : n.items)
            items.push_back(ToJson(c));
        j["items"] = items;
    }
    else
    {
        if (n.action.kind == "include")
            return n.action.spec;  // placeholders round-trip as written
        j["do"] = n.action.spec;
        if (n.then != "stay")
            j["then"] = n.then;
        if (n.faceOn)
            j["face_on"] = FaceJson(*n.faceOn);
        if (!n.state.empty())
            j["state"] = n.state;
    }
    return j;
}

json ToJson(const Scenario& s)
{
    const Settings d;
    json nav = json::object();
    if (!s.settings.stack)
        nav["stack"] = false;
    if (s.settings.crumbs != Crumbs::Auto)
        nav["breadcrumbs"] = ToString(s.settings.crumbs);
    if (s.settings.back.mode == Back::Mode::None)
        nav["back"] = nullptr;
    else if (!(s.settings.back.at == d.back.at))
        nav["back"] = SlotJson(s.settings.back.at);
    if (!s.settings.homeOnHold)
        nav["home_on_hold"] = false;
    if (s.settings.autoExit)
        nav["auto_exit"] = s.settings.autoExit;
    if (!s.settings.inheritDials)
        nav["inherit_dials"] = false;
    json st = json::object();
    if (s.settings.brightness != d.brightness)
        st["brightness"] = s.settings.brightness;
    if (s.settings.webapi != d.webapi)
        st["webapi"] = s.settings.webapi;
    if (s.settings.emulator != d.emulator)
        st["emulator"] = s.settings.emulator;
    if (!s.settings.companionFile.empty())
        st["companion_file"] = s.settings.companionFile;
    if (s.settings.target != d.target)
        st["target"] = s.settings.target;
    {
        const Activity& a = s.settings.activity;
        const Activity da;
        json aj = json::object();
        if (a.enabled != da.enabled)
            aj["enabled"] = a.enabled;
        if (a.flashKeys != da.flashKeys)
            aj["flash_keys"] = a.flashKeys;
        if (a.strip != da.strip)
            aj["strip"] = a.strip;
        if (a.seconds != da.seconds)
            aj["seconds"] = a.seconds;
        if (!a.ignore.empty())
            aj["ignore"] = a.ignore;
        if (!aj.empty())
            st["activity"] = aj;
    }
    if (!nav.empty())
        st["navigation"] = nav;
    json j{{"format", "streamdeck-scenario"}, {"version", s.version}, {"name", s.name}};
    if (s.model)
        j["model"] = ModelToken(*s.model);
    if (!st.empty())
        j["settings"] = st;
    j["root"] = ToJson(s.root);
    return j;
}

// ---------------------------------------------------------------- lookup
const Node* Find(const Node& root, std::string_view id)
{
    if (root.id == id)
        return &root;
    for (const auto& c : root.items)
        if (const Node* f = Find(c, id))
            return f;
    return nullptr;
}

std::vector<const Node*> PathTo(const Node& root, std::string_view id)
{
    if (root.id == id)
        return {&root};
    for (const auto& c : root.items)
    {
        auto p = PathTo(c, id);
        if (!p.empty())
        {
            p.insert(p.begin(), &root);
            return p;
        }
    }
    return {};
}

ResolvedNav ResolveNav(const Scenario& s, const std::vector<const Node*>& path)
{
    ResolvedNav r;
    if (path.empty())
        return r;
    r.autoExit = s.settings.autoExit;
    for (auto it = path.rbegin(); it != path.rend(); ++it)
        if ((*it)->nav.autoExit >= 0)
        {
            r.autoExit = (*it)->nav.autoExit;
            break;
        }
    if (!s.settings.stack)
        return r;  // replace navigation: no path, no back key
    Crumbs c = Crumbs::Inherit;
    for (auto it = path.rbegin(); it != path.rend() && c == Crumbs::Inherit; ++it)
        c = (*it)->nav.crumbs;
    if (c == Crumbs::Inherit)
        c = s.settings.crumbs;
    if (c == Crumbs::Auto)
        c = s.model && s.model->lcdKind == LcdKind::Region ? Crumbs::Strip : Crumbs::Key;
    r.crumbs = c;
    if (path.size() < 2)
        return r;  // no back key at the root
    for (auto it = path.rbegin(); it != path.rend(); ++it)
    {
        const Back& b = (*it)->nav.back;
        if (b.mode == Back::Mode::At)
        {
            r.backKey = b.at;
            return r;
        }
        if (b.mode == Back::Mode::None)
            return r;
    }
    if (c == Crumbs::Key && s.settings.back.mode == Back::Mode::At)
        r.backKey = s.settings.back.at;
    return r;
}

}  // namespace sdb::scn
