// The scenario editor without a window (offscreen widgets): copy + paste of an included node, the inspector's action
// kind and model boxes against the engine's tables, the run file without the editor's include marks, the
// automation examples. One thread; files in a temporary folder.
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "check.h"
#include "daemoncontrol.h"
#include "editorapi.h"
#include "editordocument.h"
#include "editorwidget.h"
#include "inspector.h"
#include "presets.h"
#include "runcontroller.h"
#include "sdb/automation.h"
#include "sdb/models.h"
#include "sdb/scenario.h"
#include "sdb/scenarioio.h"
#include "sdb/scenarioops.h"

using nlohmann::json;
using namespace sdb;
using namespace sdb::scn;

namespace
{
// true when `j` or anything below it carries an "included_from" mark
bool HasMark(const json& j)
{
    if (j.is_object())
    {
        if (j.contains("included_from"))
            return true;
        for (const auto& [k, v] : j.items())
            if (HasMark(v))
                return true;
    }
    else if (j.is_array())
        for (const auto& v : j)
            if (HasMark(v))
                return true;
    return false;
}

void Write(const QString& file, const json& j)
{
    std::string e;
    CHECK(io::SaveFile(file.toStdString(), j, &e));
}

// The inspector's box of this name (the rebuilt one: the old form is deleted later)
QComboBox* Box(QWidget* root, const char* name)
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    const auto boxes = root->findChildren<QComboBox*>(QString::fromLatin1(name));
    return boxes.size() == 1 ? boxes[0] : nullptr;
}

json Doc(const json& items, const char* model = "plusxl")
{
    return {{"format", "streamdeck-scenario"},
            {"version", kFormatVersion},
            {"name", "t"},
            {"model", model},
            {"root", {{"id", "home"}, {"type", "menu"}, {"items", items}}}};
}

void TestPasteIncluded(EditorWidget& w, const QTemporaryDir& dir)
{
    std::printf("editor: copy + paste of an included node\n");
    QDir().mkpath(dir.filePath(QStringLiteral("lib")));
    Write(dir.filePath(QStringLiteral("lib/debug.deck.json")),
          Doc(json::array({json{
              {"id", "step"}, {"type", "action"}, {"at", {0, 1}}, {"do", {{"kind", "webapi"}, {"path", "/step"}}}}})));
    const QString host = dir.filePath(QStringLiteral("host.deck.json"));
    Write(
        host,
        Doc(json::array({json{{"id", "dbg"}, {"type", "include"}, {"file", "lib/debug.deck.json"}, {"at", {1, 1}}}})));
    QString err;
    CHECK(w.OpenFile(host, &err));
    EditorDocument* d = w.Document();
    CHECK(HasMark(d->Resolved()));  // the editor's own view marks the included nodes
    CHECK(!d->IncludedFrom(QStringLiteral("dbg.step")).isEmpty());

    // the included menu, and an action inside it
    for (const char* id : {"dbg", "dbg.step"})
    {
        d->Select(QString::fromLatin1(id), false);
        w.CopySelected();
        CHECK(w.Paste(&err));
        const std::string pasted = d->Selected().toStdString();
        const json* n = ops::FindNode(d->Doc(), pasted);
        CHECK(n && pasted != id);
        CHECK(n && !HasMark(*n));  // the copy is the document's own
        CHECK(d->IncludedFrom(d->Selected()).isEmpty());
    }
    CHECK(!HasMark(d->Doc()));
    const QString saved = dir.filePath(QStringLiteral("saved.deck.json"));
    CHECK(w.SaveFile(saved, &err));
    QFile f(saved);
    CHECK(f.open(QIODevice::ReadOnly) && !f.readAll().contains("included_from"));

    // the run file: includes resolved, without the marks (absolute paths of the editor's machine)
    const json run = w.Runner()->RunDocument();
    CHECK(ops::FindNode(run, "dbg.step") && !HasMark(run));
    CHECK(HasMark(d->Resolved()));  // the editor keeps its marks
}

void TestKindBox(EditorWidget& w)
{
    std::printf("editor: the action kind box\n");
    QStringList want;
    for (const std::string_view k : ActionKinds())
        want.append(QString::fromLatin1(k.data(), static_cast<qsizetype>(k.size())));
    CHECK(ActionKindItems() == want);
    for (const char* k : {"first", "dir", "ttd_seek", "run_toggle"})
        CHECK(want.contains(QString::fromLatin1(k)));
    // every kind's template is a valid action
    for (const std::string_view k : ActionKinds())
    {
        std::vector<Issue> issues;
        const json doc = Doc(json::array(
            {json{{"id", "a"}, {"type", "action"}, {"at", {0, 0}}, {"do", ActionTemplate(std::string(k))}}}));
        const bool ok = Parse(doc, issues).has_value();
        CHECK(ok);
        if (!ok)
            for (const auto& i : issues)
                std::printf("    %.*s: %s\n", static_cast<int>(k.size()), k.data(), i.message.c_str());
    }

    // the inspector shows the kind of the selected action, also the newer ones
    EditorDocument* d = w.Document();
    d->Replace(
        Doc(json::array({json{{"id", "rt"}, {"type", "action"}, {"at", {0, 0}}, {"do", {{"kind", "run_toggle"}}}},
                         json{{"id", "odd"}, {"type", "action"}, {"at", {0, 1}}, {"do", {{"kind", "bogus"}}}}})));
    d->Select(QStringLiteral("rt"), false);
    QComboBox* box = Box(&w, "kind");
    CHECK(box && box->currentText() == QStringLiteral("run_toggle") && box->count() == want.size());
    if (box)
    {
        box->setCurrentText(QStringLiteral("first"));  // picking a kind starts from its template
        const json* n = ops::FindNode(d->Doc(), "rt");
        CHECK(n && (*n)["do"] == ActionTemplate("first"));
    }
    d->Select(QStringLiteral("odd"), false);  // a kind the engine does not run is shown as it is, not as "webapi"
    box = Box(&w, "kind");
    CHECK(box && box->currentText() == QStringLiteral("bogus"));
}

void TestModelBox(EditorWidget& w)
{
    std::printf("editor: the model box\n");
    EditorDocument* d = w.Document();
    // every model of the table, each by its token; the document's alias selects the model's entry
    const std::pair<const char*, std::uint16_t> cases[] = {{"plusxl", 0x00C6}, {"xl2", 0x008F},     {"mini2", 0x0090},
                                                           {"mk2", 0x0080},    {"discord", 0x00B3}, {"+", 0x0084}};
    for (const auto& [name, pid] : cases)
    {
        d->Replace(Doc(json::array(), name));
        d->Select(QString(), false);
        QComboBox* box = Box(&w, "model");
        CHECK(box && box->count() == static_cast<int>(AllModels().size()));
        if (!box)
            continue;
        for (int i = 0; i < box->count(); ++i)
        {
            const Model* m = FindModelByName(box->itemData(i).toString().toStdString());
            CHECK(m && m == &AllModels()[static_cast<size_t>(i)]);
        }
        const Model* shown = FindModelByName(box->currentData().toString().toStdString());
        CHECK(shown && shown->pid == pid);
    }
    d->Replace(Doc(json::array(), "nosuch"));
    d->Select(QString(), false);
    QComboBox* box = Box(&w, "model");
    CHECK(box && box->currentData().toString() == QStringLiteral("nosuch"));
}

void TestRouteExamples(EditorWidget& w)
{
    std::printf("editor: automation examples\n");
    automation::Api api("editor test", "0");
    AddEditorRoutes(api, &w, [&w]() -> QWidget* { return &w; });
    w.Document()->Replace(Doc(json::array()));
    int presets = 0;
    for (const auto& r : api.Routes())
        if (r.example.is_object() && r.example.contains("preset"))
        {
            ++presets;
            CHECK(FindPreset(QString::fromStdString(r.example["preset"].get<std::string>())) != nullptr);
            automation::Request q{r.method, r.path, {}, r.example};
            const auto reply = api.Dispatch(q);
            CHECK(reply.status == 200);
            if (reply.status != 200)
                std::printf("    %s %s: %s\n", r.method.c_str(), r.path.c_str(), reply.body.dump().c_str());
        }
    CHECK(presets >= 1);
}
}  // namespace

void TestEditor()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    DaemonControl daemon;
    EditorWidget w(&daemon);
    TestPasteIncluded(w, dir);
    TestKindBox(w);
    TestModelBox(w);
    TestRouteExamples(w);
}
