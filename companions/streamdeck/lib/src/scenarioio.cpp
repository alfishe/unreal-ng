#include "sdb/scenarioio.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

#include "fkYAML.hpp"
#include "rapidcsv.h"
#include "sdb/scenario.h"

namespace sdb::scn::io
{

namespace
{

const std::vector<std::string> kColumns = {"id", "parent", "type", "title",  "row", "col",  "label", "icon",
                                           "bg", "fg",     "kind", "method", "arg", "then", "tags",  "extra"};

// The main argument of each action kind, shown in the "arg" column.
const std::map<std::string, std::string> kArg = {{"webapi", "path"}, {"http", "url"},        {"shell", "command"},
                                                 {"open", "target"}, {"keys", "combo"},      {"nav", "to"},
                                                 {"delay", "ms"},    {"brightness", "value"}};

std::string Lower(std::string s)
{
    for (auto& c : s)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string CellOf(const json& v)
{
    return v.is_null() ? std::string() : v.is_string() ? v.get<std::string>() : v.dump();
}

json NumberOrString(const std::string& s)
{
    if (s.empty())
        return s;
    char* end = nullptr;
    const long long v = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == '\0')
        return v;
    return s;
}

// ---------------------------------------------------------------- CSV / TSV
std::string WriteTable(const json& doc, char sep)
{
    rapidcsv::Document d("", rapidcsv::LabelParams(0, -1), rapidcsv::SeparatorParams(sep, false, false, true, true));
    for (size_t c = 0; c < kColumns.size(); ++c)
        d.InsertColumn<std::string>(c, {}, kColumns[c]);
    std::vector<std::vector<std::string>> rows;
    {
        json extra = json::object();
        if (doc.contains("settings"))
            extra["settings"] = doc["settings"];
        for (const auto& [k, v] : doc.items())
            if (k != "settings" && k != "root" && k != "name" && k != "model" && k != "format" && k != "version")
                extra[k] = v;
        const std::string name = doc.value("name", "");
        rows.push_back({name, "", "scenario", name, "", "", "", "", "", "", doc.value("model", ""), "", "", "", "",
                        extra.empty() ? "" : extra.dump()});
    }
    std::function<void(const json&, const std::string&)> walk = [&](const json& n, const std::string& parent) {
        json rest = n;
        std::vector<std::string> r(kColumns.size());
        r[0] = n.value("id", "");
        r[1] = parent;
        r[2] = n.value("type", "");
        r[3] = n.value("title", "");
        for (const char* k : {"id", "type", "title", "items"})
            rest.erase(k);
        if (n.contains("at") && n["at"].is_array() && n["at"].size() == 2)
        {
            r[4] = n["at"][0].dump();
            r[5] = n["at"][1].dump();
            rest.erase("at");
        }
        if (rest.contains("face") && rest["face"].is_object())
        {
            json& f = rest["face"];
            const char* cols[] = {"label", "icon", "bg", "fg"};
            for (int i = 0; i < 4; ++i)
                if (f.contains(cols[i]))
                {
                    r[static_cast<size_t>(6 + i)] = CellOf(f[cols[i]]);
                    f.erase(cols[i]);
                }
            if (f.empty())
                rest.erase("face");
        }
        if (rest.contains("do") && rest["do"].is_object())
        {
            json& d = rest["do"];
            r[10] = d.value("kind", "");
            d.erase("kind");
            if (d.contains("method"))
            {
                r[11] = CellOf(d["method"]);
                d.erase("method");
            }
            if (auto it = kArg.find(r[10]);
                it != kArg.end() && d.contains(it->second) && !d[it->second].is_object() && !d[it->second].is_array())
            {
                r[12] = CellOf(d[it->second]);
                d.erase(it->second);
            }
            if (d.empty())
                rest.erase("do");
        }
        if (rest.contains("then"))
        {
            r[13] = CellOf(rest["then"]);
            rest.erase("then");
        }
        if (rest.contains("tags") && rest["tags"].is_array())
        {
            std::string t;
            for (const auto& x : rest["tags"])
                t += (t.empty() ? "" : ";") + CellOf(x);
            r[14] = t;
            rest.erase("tags");
        }
        if (!rest.empty())
            r[15] = rest.dump();
        rows.push_back(std::move(r));
        if (n.contains("items"))
            for (const auto& c : n["items"])
                walk(c, n.value("id", ""));
    };
    if (doc.contains("root"))
        walk(doc["root"], "");
    for (size_t i = 0; i < rows.size(); ++i)
        d.InsertRow<std::string>(i, rows[i]);
    std::ostringstream out;
    d.Save(out);
    return out.str();
}

std::optional<json> ReadTable(const std::string& text, char sep, std::string* error)
{
    std::istringstream in(text);
    rapidcsv::Document d;
    try
    {
        d = rapidcsv::Document(in, rapidcsv::LabelParams(0, -1),
                               rapidcsv::SeparatorParams(sep, false, false, true, true));
    }
    catch (const std::exception& e)
    {
        if (error)
            *error = e.what();
        return std::nullopt;
    }
    const auto names = d.GetColumnNames();
    std::map<std::string, size_t> col;
    for (size_t i = 0; i < names.size(); ++i)
        col[Lower(names[i])] = i;
    for (const char* need : {"id", "parent", "type"})
        if (!col.count(need))
        {
            if (error)
                *error = std::string("missing column '") + need + "'";
            return std::nullopt;
        }
    auto cell = [&](size_t row, const char* name) -> std::string {
        auto it = col.find(name);
        if (it == col.end())
            return {};
        const auto r = d.GetRow<std::string>(row);
        return it->second < r.size() ? r[it->second] : std::string();
    };
    json doc{{"format", "streamdeck-scenario"}, {"version", kFormatVersion}};
    std::map<std::string, json> nodes;
    std::vector<std::pair<std::string, std::string>> order;  // (id, parent) in file order
    std::string rootId;
    for (size_t i = 0; i < d.GetRowCount(); ++i)
    {
        const std::string type = cell(i, "type");
        json extra = json::object();
        if (const std::string x = cell(i, "extra"); !x.empty())
        {
            extra = json::parse(x, nullptr, false);
            if (extra.is_discarded() || !extra.is_object())
            {
                if (error)
                    *error = "row " + std::to_string(i + 2) + ": extra is not a JSON object";
                return std::nullopt;
            }
        }
        if (type == "scenario")
        {
            doc["name"] = cell(i, "title").empty() ? cell(i, "id") : cell(i, "title");
            if (!cell(i, "kind").empty())
                doc["model"] = cell(i, "kind");
            for (const auto& [k, v] : extra.items())
                doc[k] = v;
            continue;
        }
        if (type.empty() && cell(i, "id").empty())
            continue;  // blank line
        json n = extra;
        n["id"] = cell(i, "id");
        n["type"] = type;
        if (!cell(i, "title").empty())
            n["title"] = cell(i, "title");
        if (!cell(i, "row").empty() && !cell(i, "col").empty())
        {
            const json r = NumberOrString(cell(i, "row")), c = NumberOrString(cell(i, "col"));
            if (!r.is_number() || !c.is_number())
            {
                if (error)
                    *error = "row " + std::to_string(i + 2) + ": row / col must be numbers";
                return std::nullopt;
            }
            n["at"] = {r, c};
        }
        const char* faceCols[] = {"label", "icon", "bg", "fg"};
        for (const char* f : faceCols)
            if (!cell(i, f).empty())
                n["face"][f] = cell(i, f);
        if (const std::string kind = cell(i, "kind"); !kind.empty())
        {
            json& a = n["do"];
            if (!a.is_object())
                a = json::object();
            a["kind"] = kind;
            if (!cell(i, "method").empty())
                a["method"] = cell(i, "method");
            if (auto it = kArg.find(kind); it != kArg.end() && !cell(i, "arg").empty())
                a[it->second] =
                    (kind == "delay" || kind == "brightness") ? NumberOrString(cell(i, "arg")) : json(cell(i, "arg"));
        }
        if (!cell(i, "then").empty())
            n["then"] = cell(i, "then");
        if (const std::string t = cell(i, "tags"); !t.empty())
        {
            json tags = json::array();
            std::string cur;
            for (char c : t + ";")
            {
                if (c == ';' || c == ',')
                {
                    if (!cur.empty())
                        tags.push_back(cur);
                    cur.clear();
                }
                else if (!(cur.empty() && c == ' '))
                    cur += c;
            }
            n["tags"] = tags;
        }
        if (type == "menu")
            n["items"] = json::array();
        const std::string id = n["id"], parent = cell(i, "parent");
        if (nodes.count(id))
        {
            if (error)
                *error = "duplicate id '" + id + "'";
            return std::nullopt;
        }
        nodes[id] = std::move(n);
        order.emplace_back(id, parent);
        if (parent.empty())
        {
            if (!rootId.empty())
            {
                if (error)
                    *error = "two roots: '" + rootId + "' and '" + id + "'";
                return std::nullopt;
            }
            rootId = id;
        }
    }
    if (rootId.empty())
    {
        if (error)
            *error = "no root row (a menu with an empty parent)";
        return std::nullopt;
    }
    // attach children bottom-up so each subtree is complete before it is copied into its parent
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        const auto& [id, parent] = *it;
        if (parent.empty())
            continue;
        auto p = nodes.find(parent);
        if (p == nodes.end() || !p->second.contains("items"))
        {
            if (error)
                *error = "'" + id + "': parent '" + parent + "' is not a menu row";
            return std::nullopt;
        }
        p->second["items"].insert(p->second["items"].begin(), nodes[id]);
    }
    doc["root"] = nodes[rootId];
    return doc;
}

// ---------------------------------------------------------------- YAML
fkyaml::node YamlOf(const json& j)
{
    switch (j.type())
    {
        case json::value_t::object: {
            fkyaml::node m = fkyaml::node::mapping();
            for (const auto& [k, v] : j.items())
                m[k] = YamlOf(v);
            return m;
        }
        case json::value_t::array: {
            fkyaml::node s = fkyaml::node::sequence();
            for (const auto& v : j)
                s.as_seq().push_back(YamlOf(v));
            return s;
        }
        case json::value_t::string:
            return fkyaml::node(j.get<std::string>());
        case json::value_t::boolean:
            return fkyaml::node(j.get<bool>());
        case json::value_t::number_integer:
            return fkyaml::node(j.get<std::int64_t>());
        case json::value_t::number_unsigned:
            return fkyaml::node(static_cast<std::int64_t>(j.get<std::uint64_t>()));
        case json::value_t::number_float:
            return fkyaml::node(j.get<double>());
        default:
            return fkyaml::node(nullptr);
    }
}

json JsonOf(const fkyaml::node& n)
{
    if (n.is_mapping())
    {
        json o = json::object();
        for (const auto& kv : n.map_items())
        {
            const auto& key = kv.key();
            o[key.is_string() ? key.get_value<std::string>() : fkyaml::node::serialize(key)] = JsonOf(kv.value());
        }
        return o;
    }
    if (n.is_sequence())
    {
        json a = json::array();
        for (const auto& v : n.as_seq())
            a.push_back(JsonOf(v));
        return a;
    }
    if (n.is_string())
        return n.get_value<std::string>();
    if (n.is_boolean())
        return n.get_value<bool>();
    if (n.is_integer())
        return n.get_value<std::int64_t>();
    if (n.is_float_number())
        return n.get_value<double>();
    return nullptr;
}

}  // namespace

namespace
{
struct Resolver
{
    std::vector<std::string>* files;
    std::vector<std::string> stack;  // canonical files being resolved (cycle detection)

    static void PrefixIds(json& n, const std::string& prefix, std::map<std::string, std::string>& renames, bool root)
    {
        if (!root && n.contains("id") && n["id"].is_string())
        {
            const std::string nid = prefix + "." + n["id"].get<std::string>();
            renames[n["id"].get<std::string>()] = nid;
            n["id"] = nid;
        }
        if (n.contains("items"))
            for (auto& c : n["items"])
                PrefixIds(c, prefix, renames, false);
    }
    static void Retarget(json& j, const std::map<std::string, std::string>& renames)
    {
        if (j.is_object())
        {
            if (j.value("kind", "") == "nav" && j.contains("node") && j["node"].is_string())
                if (auto it = renames.find(j["node"].get<std::string>()); it != renames.end())
                    j["node"] = it->second;
            for (auto& [k, v] : j.items())
                Retarget(v, renames);
        }
        else if (j.is_array())
            for (auto& v : j)
                Retarget(v, renames);
    }
    static const json* FindIn(const json& n, const std::string& id)
    {
        if (n.value("id", "") == id)
            return &n;
        if (n.contains("items"))
            for (const auto& c : n["items"])
                if (const json* f = FindIn(c, id))
                    return f;
        return nullptr;
    }

    // Resolves includes inside node `n` (in a document from `dir`).
    bool ResolveNode(json& n, const std::filesystem::path& dir, std::string& err)
    {
        if (n.value("type", "") == "include")
        {
            if (stack.size() >= 8)
            {
                err = "includes nested deeper than 8";
                return false;
            }
            std::filesystem::path f = n.value("file", "");
            if (f.empty())
            {
                err = "include '" + n.value("id", "") + "' has no file";
                return false;
            }
            if (f.is_relative())
                f = dir / f;
            std::error_code ec;
            const auto canon = std::filesystem::weakly_canonical(f, ec).string();
            for (const auto& s : stack)
                if (s == canon)
                {
                    err = "include cycle: " + canon;
                    return false;
                }
            std::string e;
            auto sub = LoadFile(f, &e);
            if (!sub)
            {
                err = "include '" + n.value("id", "") + "': " + e;
                return false;
            }
            if (files)
                files->push_back(canon);
            if (!sub->contains("root"))
            {
                err = f.string() + ": no root";
                return false;
            }
            stack.push_back(canon);
            json& root = (*sub)["root"];
            const bool ok = ResolveNode(root, f.parent_path(), err);
            stack.pop_back();
            if (!ok)
                return false;
            const std::string want = n.value("node", "");
            const json* src = want.empty() ? &root : FindIn(root, want);
            if (!src)
            {
                err = "include '" + n.value("id", "") + "': no node '" + want + "' in " + f.string();
                return false;
            }
            json out = *src;
            if (out.value("fragment", false) && out.contains("items") && out["items"].size() == 1)
                out = out["items"][0];  // a wrapped action
            out.erase("fragment");
            const std::string id = n.value("id", out.value("id", "included"));
            std::map<std::string, std::string> renames;
            renames[out.value("id", "")] = id;
            PrefixIds(out, id, renames, true);
            out["id"] = id;
            Retarget(out, renames);
            if (n.contains("at"))
                out["at"] = n["at"];
            else
                out.erase("at");
            if (n.contains("title"))
                out["title"] = n["title"];
            if (n.contains("face") && n["face"].is_object())
            {
                if (!out.contains("face"))
                    out["face"] = json::object();
                out["face"].merge_patch(n["face"]);
            }
            if (n.contains("tags"))
                out["tags"] = n["tags"];
            std::function<void(json&)> mark = [&](json& x) {
                x["included_from"] = canon;
                if (x.contains("items"))
                    for (auto& c : x["items"])
                        mark(c);
            };
            mark(out);
            n = std::move(out);
            return true;
        }
        if (n.contains("items"))
            for (auto& c : n["items"])
                if (!ResolveNode(c, dir, err))
                    return false;
        return true;
    }
};
}  // namespace

std::optional<json> ResolveIncludes(const json& doc, const std::filesystem::path& baseDir, std::string* error,
                                    std::vector<std::string>* files)
{
    if (!doc.is_object() || !doc.contains("root"))
        return doc;
    json out = doc;
    Resolver r{files, {}};
    std::string err;
    if (!r.ResolveNode(out["root"], baseDir, err))
    {
        if (error)
            *error = err;
        return std::nullopt;
    }
    return out;
}

void StripIncludeMarks(json& node)
{
    if (!node.is_object())
        return;
    node.erase("included_from");
    if (node.contains("items") && node["items"].is_array())
        for (auto& c : node["items"])
            StripIncludeMarks(c);
}

std::optional<json> LoadResolved(const std::filesystem::path& file, std::string* error, std::vector<std::string>* files)
{
    auto d = LoadFile(file, error);
    if (!d)
        return std::nullopt;
    std::error_code ec;
    const auto canon = std::filesystem::weakly_canonical(file, ec);
    Resolver r{files, {canon.string()}};
    if (files)
        files->push_back(canon.string());
    std::string err;
    if (d->contains("root") && !r.ResolveNode((*d)["root"], canon.parent_path(), err))
    {
        if (error)
            *error = err;
        return std::nullopt;
    }
    return d;
}

std::string ToYaml(const json& j)
{
    return fkyaml::node::serialize(YamlOf(j));
}

std::optional<json> FromYaml(const std::string& text, std::string* error)
{
    try
    {
        return JsonOf(fkyaml::node::deserialize(text));
    }
    catch (const std::exception& e)
    {
        if (error)
            *error = std::string("YAML: ") + e.what();
        return std::nullopt;
    }
}

std::optional<Format> FormatByName(std::string_view n)
{
    const std::string s = Lower(std::string(n));
    if (s == "json")
        return Format::Json;
    if (s == "yaml" || s == "yml")
        return Format::Yaml;
    if (s == "csv")
        return Format::Csv;
    if (s == "tsv" || s == "tab")
        return Format::Tsv;
    return std::nullopt;
}

std::optional<Format> FormatOf(const std::filesystem::path& file)
{
    std::string ext = file.extension().string();
    if (!ext.empty() && ext[0] == '.')
        ext.erase(0, 1);
    return FormatByName(ext);
}

const char* FormatName(Format f)
{
    switch (f)
    {
        case Format::Json:
            return "json";
        case Format::Yaml:
            return "yaml";
        case Format::Csv:
            return "csv";
        case Format::Tsv:
            return "tsv";
    }
    return "?";
}

std::string Serialize(const json& doc, Format f)
{
    switch (f)
    {
        case Format::Json:
            return doc.dump(2) + "\n";
        case Format::Yaml:
            return ToYaml(doc);
        case Format::Csv:
            return WriteTable(doc, ',');
        case Format::Tsv:
            return WriteTable(doc, '\t');
    }
    return {};
}

std::optional<json> Deserialize(const std::string& text, Format f, std::string* error)
{
    switch (f)
    {
        case Format::Json: {
            json j = json::parse(text, nullptr, false);
            if (j.is_discarded())
            {
                if (error)
                    *error = "not valid JSON";
                return std::nullopt;
            }
            return j;
        }
        case Format::Yaml:
            return FromYaml(text, error);
        case Format::Csv:
            return ReadTable(text, ',', error);
        case Format::Tsv:
            return ReadTable(text, '\t', error);
    }
    return std::nullopt;
}

std::optional<json> LoadFile(const std::filesystem::path& file, std::string* error)
{
    std::ifstream in(file, std::ios::binary);
    if (!in)
    {
        if (error)
            *error = "cannot open " + file.string();
        return std::nullopt;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    std::string e;
    auto j = Deserialize(ss.str(), FormatOf(file).value_or(Format::Json), &e);
    if (!j && error)
        *error = file.string() + ": " + e;
    return j;
}

bool SaveFile(const std::filesystem::path& file, const json& doc, std::string* error)
{
    const auto tmp = file.string() + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error)
                *error = "cannot write " + tmp;
            return false;
        }
        out << Serialize(doc, FormatOf(file).value_or(Format::Json));
        if (!out)
        {
            if (error)
                *error = "write failed: " + tmp;
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, file, ec);  // atomic replace: a watching runtime never reads half a file
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }
    return true;
}

}  // namespace sdb::scn::io
