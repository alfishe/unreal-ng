#include "editorapi.h"

#include <QBuffer>
#include <QUndoStack>
#include <QWidget>

#include "../gui.h"
#include "../virtualdeck.h"
#include "designcanvas.h"
#include "editordocument.h"
#include "editorwidget.h"
#include "presets.h"
#include "runcontroller.h"
#include "sdb/http.h"
#include "sdb/navigator.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using namespace sdb;
using namespace sdb::scn;
using automation::Reply;
using automation::Request;

namespace
{
QString Qs(const std::string& s)
{
    return QString::fromStdString(s);
}

std::string Png(const QImage& img)
{
    QByteArray bytes;
    QBuffer buf(&bytes);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return bytes.toStdString();
}

std::optional<Slot> SlotArg(const Request& r, const char* name = "at")
{
    if (!r.body.is_object() || !r.body.contains(name))
        return std::nullopt;
    const json& a = r.body[name];
    if (a.is_array() && a.size() == 2 && a[0].is_number_integer() && a[1].is_number_integer())
        return Slot{a[0].get<int>(), a[1].get<int>()};
    if (a.is_object())
        return Slot{a.value("row", -1), a.value("col", -1)};
    return Slot{};
}

json Status(EditorWidget* e)
{
    EditorDocument* d = e->Document();
    int errors = 0, warnings = 0;
    for (const auto& i : d->Issues())
        (i.error ? errors : warnings)++;
    return {{"path", d->Path().toStdString()},
            {"dirty", d->Dirty()},
            {"name", d->Doc().value("name", "")},
            {"model", d->Doc().value("model", "")},
            {"selected", d->Selected().toStdString()},
            {"canvas_menu", d->CanvasMenu().toStdString()},
            {"can_undo", d->UndoStack()->canUndo()},
            {"can_redo", d->UndoStack()->canRedo()},
            {"issues", {{"errors", errors}, {"warnings", warnings}}},
            {"run",
             {{"running", e->Runner()->Running()},
              {"how", e->Runner()->How().toStdString()},
              {"run_file", e->Runner()->RunFile().toStdString()},
              {"api", "http://127.0.0.1:" + std::to_string(e->Runner()->ApiPort())}}}};
}

json IssuesJson(EditorDocument* d)
{
    json a = json::array();
    for (const auto& i : d->Issues())
        a.push_back({{"node", i.node}, {"message", i.message}, {"severity", i.error ? "error" : "warning"}});
    return a;
}

json CanvasJson(EditorDocument* d)
{
    const auto& s = d->Loose();
    if (!s)
        return {{"error", "the document has no tree"}};
    Navigator nav(*s);
    nav.GoTo(d->CanvasMenu().toStdString());
    const Model& m = d->GetModel();
    json j = DescribePage(*s, nav, m, Layout(*s, nav, m));
    j["selected"] = d->Selected().toStdString();
    return j;
}

// A node change through the undo stack, answered with the node and the issues it caused.
Reply Edit(EditorWidget* e, const QString& what, const std::function<ops::Result(json&)>& op)
{
    return OnGui([&] {
        QString err;
        std::string id;
        EditorDocument* d = e->Document();
        if (!d->Apply(what, op, &err, &id))
            return Reply::Error(422, err.toStdString());
        json out{{"ok", true}, {"id", id}, {"issues", IssuesJson(d)}};
        if (const json* n = ops::FindNode(d->Doc(), id))
            out["node"] = *n;
        return Reply::Ok(out);
    });
}

Reply Proxy(EditorWidget* e, const std::string& method, const std::string& path, const json& body)
{
    // the daemon's automation, whoever started it
    const std::string url = OnGui([e] { return e->Runner()->ApiUrl().toStdString(); });
    auto r = HttpRequest(url, method, path, body.is_null() ? std::string() : body.dump(), {}, 3000);
    if (r.status == 0)
        return Reply::Error(502, "unreal-streamdeck automation at " + url + ": " + r.error, "POST /api/v1/editor/run");
    json b = json::parse(r.body, nullptr, false);
    return {r.status, b.is_discarded() ? json{{"text", r.body}} : b, {}, {}};
}
}  // namespace

void AddEditorRoutes(automation::Api& api, EditorWidget* e, std::function<QWidget*()> host)
{
    const std::vector<std::string> tags{"editor", "scenario"};
    auto add = [&](const char* method, const char* path, const char* summary, std::vector<std::string> extra,
                   std::vector<automation::Param> params, json example, automation::Handler h) {
        std::vector<std::string> t = tags;
        t.insert(t.end(), extra.begin(), extra.end());
        api.Add({method, path, summary, "", t, std::move(params), std::move(example), std::move(h)});
    };

    add("GET", "/api/v1/editor",
        "Editor status: file, dirty, model, selection, the menu on the canvas, issue counts, run state", {"status"}, {},
        nullptr, [e](const Request&) { return OnGui([e] { return Reply::Ok(Status(e)); }); });
    add("POST", "/api/v1/editor/new", "New empty scenario", {"file"},
        {{"model", "body", "string", "plusxl (default), plus, xl, classic, mk2, neo, mini ..."},
         {"name", "body", "string", ""}},
        json{{"model", "plusxl"}, {"name", "demo"}}, [e](const Request& r) {
            const Model* m = FindModelByName(r.Str("model", "plusxl"));
            if (!m)
                return Reply::Error(404, "unknown model");
            return OnGui([&] {
                e->Document()->NewDocument(*m, Qs(r.Str("name", "untitled")));
                return Reply::Ok(Status(e));
            });
        });
    add("POST", "/api/v1/editor/open", "Open a scenario file (.json .yaml .yml .csv .tsv)", {"file"},
        {{"path", "body", "string", "file", true}}, json{{"path", "my.deck.json"}}, [e](const Request& r) {
            return OnGui([&] {
                QString err;
                if (!e->OpenFile(Qs(r.Str("path")), &err))
                    return Reply::Error(422, err.toStdString());
                return Reply::Ok(Status(e));
            });
        });
    add("POST", "/api/v1/editor/save", "Save (to `path`, else the current file; the format follows the extension)",
        {"file"}, {{"path", "body", "string", "file"}}, nullptr, [e](const Request& r) {
            return OnGui([&] {
                if (!r.Has("path") && e->Document()->Path().isEmpty())
                    return Reply::Error(400, "untitled: give a path");
                QString err;
                if (!e->Document()->Save(Qs(r.Str("path")), &err))
                    return Reply::Error(500, err.toStdString());
                return Reply::Ok(Status(e));
            });
        });
    add("POST", "/api/v1/editor/export", "Write a copy in another format (the document keeps its file)",
        {"file", "csv", "yaml"}, {{"path", "body", "string", "file (.json .yaml .csv .tsv)", true}}, nullptr,
        [e](const Request& r) {
            return OnGui([&] {
                QString err;
                if (!e->Document()->ExportTo(Qs(r.Str("path")), &err))
                    return Reply::Error(500, err.toStdString());
                return Reply::Ok({{"exported", r.Str("path")}});
            });
        });
    add("GET", "/api/v1/editor/document", "The whole scenario: JSON, or as text in yaml / csv / tsv",
        {"document", "json", "yaml", "csv"},
        {{"format", "query", "string", "json (default), yaml, csv, tsv", false, {"json", "yaml", "csv", "tsv"}},
         {"resolved", "query", "boolean", "includes replaced by what they link to"}},
        nullptr, [e](const Request& r) {
            const bool res = r.Flag("resolved");
            const json d = OnGui([e, res] { return res ? e->Document()->Resolved() : e->Document()->Doc(); });
            const auto f = io::FormatByName(r.Str("format", "json"));
            if (!f)
                return Reply::Error(400, "format: json, yaml, csv or tsv");
            if (*f == io::Format::Json)
                return Reply::Ok(d);
            return Reply::Ok({{"format", io::FormatName(*f)}, {"text", io::Serialize(d, *f)}});
        });
    add("PUT", "/api/v1/editor/document", "Replace the whole scenario (one undo step): {document} or {format, text}",
        {"document"},
        {{"document", "body", "object", "the scenario JSON"},
         {"format", "body", "string", "yaml / csv / tsv / json for `text`"},
         {"text", "body", "string", ""}},
        nullptr, [e](const Request& r) {
            json d;
            if (r.body.contains("document"))
                d = r.body["document"];
            else
            {
                const auto f = io::FormatByName(r.Str("format", "json"));
                std::string err;
                auto parsed = f ? io::Deserialize(r.Str("text"), *f, &err) : std::nullopt;
                if (!parsed)
                    return Reply::Error(422, err.empty() ? "bad format" : err);
                d = *parsed;
            }
            return OnGui([&] {
                if (!e->Document()->Replace(d))
                    return Reply::Error(422, "not a scenario (needs root)");
                return Reply::Ok({{"issues", IssuesJson(e->Document())}});
            });
        });
    add("GET", "/api/v1/editor/tree", "The scenario tree, compact (id, type, title, at, kind, tags, items)",
        {"tree", "nodes"}, {}, nullptr, [e](const Request&) {
            const json d = OnGui([e] { return e->Document()->Resolved(); });
            std::function<json(const json&)> slim = [&](const json& n) {
                json o{{"id", n.value("id", "")},
                       {"type", n.value("type", "")},
                       {"title", n.value("title", n.value("id", ""))}};
                if (n.contains("at"))
                    o["at"] = n["at"];
                if (n.contains("tags"))
                    o["tags"] = n["tags"];
                if (n.contains("do"))
                    o["kind"] = n["do"].value("kind", "");
                if (n.contains("included_from"))
                    o["included_from"] = n["included_from"];
                if (n.contains("items"))
                {
                    json a = json::array();
                    for (const auto& c : n["items"])
                        a.push_back(slim(c));
                    o["items"] = a;
                }
                return o;
            };
            return Reply::Ok(slim(d["root"]));
        });
    add("GET", "/api/v1/editor/nodes", "Nodes as a flat list, filtered by tag / type / kind / menu / text",
        {"nodes", "tags", "search"},
        {{"tag", "query", "string", "only nodes with this tag"},
         {"type", "query", "string", "menu | action", false, {"menu", "action"}},
         {"kind", "query", "string", "action kind"},
         {"menu", "query", "string", "children of this menu"},
         {"text", "query", "string", "in id / title"}},
        nullptr, [e](const Request& r) {
            json f = json::object();
            for (const char* k : {"tag", "type", "kind", "menu", "text"})
                if (r.Has(k))
                    f[k] = r.Str(k);
            const json d =
                OnGui([e] { return e->Document()->Resolved(); });  // included nodes too ("included_from" marks them)
            return Reply::Ok({{"nodes", ListNodes(d, f)}});
        });
    add("GET", "/api/v1/editor/nodes/{id}", "One node (with its subtree)", {"nodes"}, {}, nullptr,
        [e](const Request& r) {
            return OnGui([&] {
                const json* n = ops::FindNode(e->Document()->Doc(), r.Str("id"));
                if (!n)
                    n = ops::FindNode(e->Document()->Resolved(), r.Str("id"));  // inside an include: read-only
                return n ? Reply::Ok(*n) : Reply::Error(404, "no node '" + r.Str("id") + "'");
            });
        });
    add("POST", "/api/v1/editor/nodes",
        "Add a node to a menu (what dropping a palette item on a key does): {parent, at?, node | preset}",
        {"nodes", "add", "palette"},
        {{"parent", "body", "string", "menu id (default: the menu on the canvas)"},
         {"at", "body", "array", "[row, col]; default the first free key"},
         {"node", "body", "object", "a node: {type: menu|action, title, face, do, tags ...}"},
         {"preset", "body", "string", "a palette preset name (GET /api/v1/editor/presets)"}},
        json{{"parent", "home"}, {"at", {0, 1}}, {"preset", "Pause"}}, [e](const Request& r) {
            json node;
            if (r.Has("preset"))
            {
                const Preset* p = FindPreset(Qs(r.Str("preset")));
                if (!p)
                    return Reply::Error(404, "no preset '" + r.Str("preset") + "'", "GET /api/v1/editor/presets");
                node = p->node;
                if (r.body.contains("node") && r.body["node"].is_object())
                    node.merge_patch(r.body["node"]);  // preset + overrides
            }
            else if (r.body.contains("node"))
                node = r.body["node"];
            else
                return Reply::Error(400, "give 'node' or 'preset'");
            const std::string parent =
                r.Has("parent") ? r.Str("parent") : OnGui([e] { return e->Document()->CanvasMenu().toStdString(); });
            const auto at = SlotArg(r);
            return Edit(e, QStringLiteral("Add"), [&](json& d) { return ops::InsertNode(d, parent, at, node); });
        });
    add("PUT", "/api/v1/editor/nodes/{id}",
        "Change a node: JSON merge patch (null removes a field; a new id renames, nav targets follow)",
        {"nodes", "edit", "patch"},
        {{"patch", "body", "object",
          "e.g. {\"title\": \"Run\", \"face\": {\"bg\": \"004D14\"}, \"tags\": [\"debug\"]}"}},
        json{{"patch", {{"title", "Run"}, {"tags", {"transport"}}}}}, [e](const Request& r) {
            const json p = r.body.contains("patch") ? r.body["patch"] : r.body;
            return Edit(e, QStringLiteral("Edit"), [&](json& d) { return ops::UpdateNode(d, r.Str("id"), p); });
        });
    add("DELETE", "/api/v1/editor/nodes/{id}", "Delete a node and its subtree", {"nodes", "delete"}, {}, nullptr,
        [e](const Request& r) {
            return Edit(e, QStringLiteral("Delete"), [&](json& d) { return ops::RemoveNode(d, r.Str("id")); });
        });
    add("POST", "/api/v1/editor/nodes/{id}/move",
        "Move a node to another key / menu; swap = exchange with the occupant", {"nodes", "move", "drag"},
        {{"parent", "body", "string", "target menu (default: its menu)"},
         {"at", "body", "array", "[row, col]", true},
         {"swap", "body", "boolean", "default true"}},
        json{{"at", {2, 3}}}, [e](const Request& r) {
            const auto at = SlotArg(r);
            if (!at || !at->Valid())
                return Reply::Error(400, "at: [row, col]");
            std::string parent = r.Str("parent");
            if (parent.empty())
                parent = OnGui([&] {
                    json* p = ops::ParentOf(const_cast<json&>(e->Document()->Doc()), r.Str("id"));
                    return p ? p->value("id", "") : std::string();
                });
            return Edit(e, QStringLiteral("Move"),
                        [&](json& d) { return ops::MoveNode(d, r.Str("id"), parent, *at, r.Flag("swap", true)); });
        });
    add("POST", "/api/v1/editor/nodes/{id}/duplicate", "Duplicate a node (ids renamed) into its menu",
        {"nodes", "copy"}, {{"at", "body", "array", "[row, col]; default the first free key"}}, nullptr,
        [e](const Request& r) {
            const auto at = SlotArg(r);
            return Edit(e, QStringLiteral("Duplicate"),
                        [&](json& d) { return ops::DuplicateNode(d, r.Str("id"), at); });
        });
    add("POST", "/api/v1/editor/nodes/{id}/export",
        "Export a subtree to its own file (any format); link=true also replaces it here by an include of that file",
        {"subtree", "export", "file", "include"},
        {{"path", "body", "string", "file (.json .yaml .csv .tsv)", true},
         {"link", "body", "boolean", "replace the node by a link to the file"}},
        json{{"path", "debug.deck.yaml"}, {"link", true}}, [e](const Request& r) {
            return OnGui([&] {
                QString err;
                QStringList warn;
                if (!e->ExportSubtree(Qs(r.Str("id")), Qs(r.Str("path")), r.Flag("link"), &err, &warn))
                    return Reply::Error(422, err.toStdString());
                json w = json::array();
                for (const auto& x : warn)
                    w.push_back(x.toStdString());
                return Reply::Ok({{"exported", r.Str("path")},
                                  {"linked", r.Flag("link")},
                                  {"links_outside", w},
                                  {"issues", IssuesJson(e->Document())}});
            });
        });
    add("POST", "/api/v1/editor/import",
        "Import a file (or a node in it) into a menu: as a submenu copy, its keys merged, or a link (include)",
        {"subtree", "import", "file", "include", "merge"},
        {{"path", "body", "string", "scenario file", true},
         {"mode", "body", "string", "menu (default) | items | link", false, {"menu", "items", "link"}},
         {"parent", "body", "string", "target menu (default: the menu on the canvas)"},
         {"at", "body", "array", "[row, col] (menu / link)"},
         {"node", "body", "string", "a node id in the file (default its root)"}},
        json{{"path", "debug.deck.yaml"}, {"mode", "link"}, {"parent", "home"}, {"at", {1, 4}}}, [e](const Request& r) {
            return OnGui([&] {
                const QString parent = r.Has("parent") ? Qs(r.Str("parent")) : e->Document()->CanvasMenu();
                QString err;
                std::string id;
                if (!e->ImportFile(Qs(r.Str("path")), parent, SlotArg(r), Qs(r.Str("mode", "menu")), Qs(r.Str("node")),
                                   &err, &id))
                    return Reply::Error(422, err.toStdString());
                json out{{"ok", true}, {"id", id}, {"issues", IssuesJson(e->Document())}};
                if (const json* n = ops::FindNode(e->Document()->Resolved(), id))
                    out["node"] = *n;
                return Reply::Ok(out);
            });
        });
    add("POST", "/api/v1/editor/nodes/{id}/inline", "Replace an include by an editable copy of what it links to",
        {"subtree", "include", "inline"}, {}, nullptr, [e](const Request& r) {
            return OnGui([&] {
                QString err;
                if (!e->InlineInclude(Qs(r.Str("id")), &err))
                    return Reply::Error(422, err.toStdString());
                return Reply::Ok({{"ok", true}, {"issues", IssuesJson(e->Document())}});
            });
        });
    add("POST", "/api/v1/editor/select", "Select a node (the canvas shows its menu)", {"selection"},
        {{"id", "body", "string", "node id; empty = none"}}, json{{"id", "pause"}}, [e](const Request& r) {
            return OnGui([&] {
                e->Document()->Select(Qs(r.Str("id")));
                return Reply::Ok(Status(e));
            });
        });
    add("POST", "/api/v1/editor/enter", "Show a menu on the canvas (\"..\" = up one level)", {"navigation", "canvas"},
        {{"id", "body", "string", "menu id or ..", true}}, json{{"id", "debug"}}, [e](const Request& r) {
            return OnGui([&] {
                const bool ok = r.Str("id") == ".." ? e->Document()->Up() : e->Document()->Enter(Qs(r.Str("id")));
                if (!ok)
                    return Reply::Error(404, "not a menu: '" + r.Str("id") + "'");
                return Reply::Ok(CanvasJson(e->Document()));
            });
        });
    add("GET", "/api/v1/editor/canvas",
        "What the canvas shows, as JSON: menu path, each key's node (id, title, type, kind, tags), back key, dials",
        {"canvas", "page", "keys"}, {}, nullptr,
        [e](const Request&) { return OnGui([e] { return Reply::Ok(CanvasJson(e->Document())); }); });
    add("POST", "/api/v1/editor/undo", "Undo the last change", {"undo"}, {}, nullptr, [e](const Request&) {
        return OnGui([e] {
            e->Document()->UndoStack()->undo();
            return Reply::Ok(Status(e));
        });
    });
    add("POST", "/api/v1/editor/redo", "Redo", {"undo"}, {}, nullptr, [e](const Request&) {
        return OnGui([e] {
            e->Document()->UndoStack()->redo();
            return Reply::Ok(Status(e));
        });
    });
    add("GET", "/api/v1/editor/issues", "Validation: errors (block running) and warnings", {"validation", "issues"}, {},
        nullptr,
        [e](const Request&) { return OnGui([e] { return Reply::Ok({{"issues", IssuesJson(e->Document())}}); }); });
    add("GET", "/api/v1/editor/presets",
        "Palette presets (category, name, node) for POST /api/v1/editor/nodes {preset}",
        {"palette", "presets", "actions"}, {}, nullptr, [](const Request&) {
            json a = json::array();
            for (const auto& p : Presets())
                a.push_back({{"category", p.category.toStdString()},
                             {"name", p.name.toStdString()},
                             {"description", p.description.toStdString()},
                             {"node", p.node}});
            return Reply::Ok({{"presets", a}});
        });
    add("GET", "/api/v1/editor/run", "Runtime status and its recent output", {"run"}, {}, nullptr, [e](const Request&) {
        return OnGui([e] {
            json j = Status(e)["run"];
            json lines = json::array();
            for (const auto& l : e->Runner()->Output())
                lines.push_back(l.toStdString());
            j["output"] = lines;
            return Reply::Ok(j);
        });
    });
    add("POST", "/api/v1/editor/run",
        "Play the scenario on the deck with unreal-streamdeck (live edits): a running daemon reloads it, else one is "
        "started",
        {"run", "play", "device"},
        {{"serial", "body", "string", "USB deck for a daemon the studio starts (default the first)"}}, json::object(),
        [e](const Request& r) {
            return OnGui([&] {
                QString err;
                if (!e->RunOn(Qs(r.Str("serial")), &err))
                    return Reply::Error(422, err.toStdString());
                return Reply::Ok(Status(e)["run"]);
            });
        });
    add("POST", "/api/v1/editor/stop", "Stop the run: a started daemon ends, a running one gets its own scenario back",
        {"run"}, {}, nullptr, [e](const Request&) {
            return OnGui([e] {
                e->StopRun();
                return Reply::Ok(Status(e)["run"]);
            });
        });
    add("GET", "/api/v1/editor/screenshot",
        "Editor screenshot (PNG): the canvas, its LCD as drawn (device pixels), or the editor window",
        {"screenshot", "image", "canvas"},
        {{"area", "query", "string", "canvas (default), lcd or window", false, {"canvas", "lcd", "window"}},
         {"scale", "query", "number", "canvas scale (default 1)"},
         {"path", "query", "string", "also save here"}},
        nullptr, [e, host](const Request& r) {
            const std::string area = r.Str("area", "canvas");
            const double scale = std::clamp(r.Real("scale", 1.0), 0.25, 4.0);
            QImage img = OnGui([&] {
                if (area == "window")
                    return host()->grab().toImage();
                if (area == "lcd")
                    return e->Canvas()->Deck()->Lcd().copy();
                return e->Canvas()->RenderImage(scale);
            });
            json meta{{"target", "editor"}, {"area", area}, {"width", img.width()}, {"height", img.height()}};
            if (r.Has("path"))
            {
                meta["saved"] = img.save(Qs(r.Str("path")), "PNG");
                meta["path"] = r.Str("path");
            }
            return Reply::Png(Png(img), meta);
        });
    // the daemon's runtime (unreal-streamdeck run --api), proxied so agents need one endpoint
    add("GET", "/api/v1/runtime/state",
        "Running scenario: menu stack, page, toggles, last actions (proxied from unreal-streamdeck)",
        {"runtime", "state"}, {}, nullptr, [e](const Request&) { return Proxy(e, "GET", "/api/v1/state", nullptr); });
    add("GET", "/api/v1/runtime/page", "Running scenario: what each key / dial means now (JSON)",
        {"runtime", "page", "keys"}, {}, nullptr,
        [e](const Request&) { return Proxy(e, "GET", "/api/v1/page", nullptr); });
    add("POST", "/api/v1/runtime/navigate", "Running scenario: navigate {to: back|home|node, node}",
        {"runtime", "navigation"},
        {{"to", "body", "string", "back | home | node", true}, {"node", "body", "string", ""}}, json{{"to", "home"}},
        [e](const Request& r) { return Proxy(e, "POST", "/api/v1/navigate", r.body); });
    add("POST", "/api/v1/runtime/press", "Running scenario: press a key by node id (or {key})", {"runtime", "press"},
        {{"node", "body", "string", "node id on the current page"}, {"key", "body", "integer", "key index"}},
        json{{"node", "debug"}}, [e](const Request& r) { return Proxy(e, "POST", "/api/v1/press", r.body); });
}
