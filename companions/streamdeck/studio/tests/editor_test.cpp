// The scenario editor without a window (offscreen widgets): copy + paste of an included node, the inspector's action
// kind and model boxes against the engine's tables, the run file without the editor's include marks, the
// automation examples. One thread; files in a temporary folder.
#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QUndoStack>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "actioncatalogue.h"
#include "check.h"
#include "daemoncontrol.h"
#include "editorapi.h"
#include "editordocument.h"
#include "editorwidget.h"
#include "inspector.h"
#include "presets.h"
#include "runcontroller.h"
#include "sdb/automation.h"
#include "sdb/catalogue.h"
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
// The action kind (scenarios.md §2): the catalogue picker, value, hold; the palette's catalogue presets
void TestActionStep(EditorWidget& w)
{
    std::printf("editor: the action kind\n");
    cat::Catalogue c;
    c.actions.push_back({"run.pause_toggle", "Pause / continue", "Run", "press", false, false, ""});
    c.actions.push_back({"run.turbo_hold", "Turbo while held", "Run", "press", true, false, ""});
    c.actions.push_back({"mach.start", "Start a machine", "Capture and machine", "press", false, false, "model"});
    EditorCatalogue::Instance().Set(c, QStringLiteral("the test"));
    EditorDocument* d = w.Document();
    d->Replace(Doc(json::array({json{{"id", "a"}, {"type", "action"}, {"at", {0, 0}}, {"do", ActionTemplate("action")}},
                                json{{"id", "m"}, {"type", "menu"}, {"at", {0, 1}}, {"items", json::array()}}})));
    CHECK(d->Valid());
    d->Select(QStringLiteral("a"), false);
    QComboBox* pick = Box(&w, "action-id");
    CHECK(pick && pick->count() == 3 && pick->currentData().toString() == QStringLiteral("run.pause_toggle"));
    if (pick)
    {
        const int at = pick->findData(QStringLiteral("mach.start"));
        pick->setCurrentIndex(at);
        emit pick->activated(at);
        const json* n = ops::FindNode(d->Doc(), "a");
        CHECK(n && (*n)["do"]["id"] == "mach.start");
    }
    // value: JSON replaced as a whole (not merged), plain text kept as text, empty removes it
    auto value = [&]() -> QLineEdit* {
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        const auto v = w.findChildren<QLineEdit*>(QStringLiteral("action-value"));
        return v.size() == 1 ? v[0] : nullptr;
    };
    auto setValue = [&](const QString& t) {
        if (QLineEdit* e = value())
        {
            e->setText(t);
            emit e->editingFinished();
        }
    };
    setValue(QStringLiteral(R"({"model":"PENTAGON","ram_kb":128})"));
    setValue(QStringLiteral(R"({"model":"48K"})"));
    const json* n = ops::FindNode(d->Doc(), "a");
    CHECK(n && (*n)["do"]["value"] == json({{"model", "48K"}}));
    setValue(QStringLiteral("ttd"));
    n = ops::FindNode(d->Doc(), "a");
    CHECK(n && (*n)["do"]["value"] == "ttd");
    setValue(QString());
    n = ops::FindNode(d->Doc(), "a");
    CHECK(n && !(*n)["do"].contains("value"));
    // an id the catalogue lacks: shown as it is, a warning
    d->Replace(Doc(json::array({json{{"id", "a"}, {"type", "action"}, {"at", {0, 0}},
                                     {"do", {{"kind", "action"}, {"id", "run.nothing"}}}}})));
    d->Select(QStringLiteral("a"), false);
    pick = Box(&w, "action-id");
    CHECK(pick && pick->currentData().toString() == QStringLiteral("run.nothing"));
    CHECK(d->Valid() && std::any_of(d->Issues().begin(), d->Issues().end(), [](const Issue& i) {
              return !i.error && i.message.find("not in the host's catalogue") != std::string::npos;
          }));
    // the palette: one preset per catalogue action, a held one with hold
    int fromCatalogue = 0;
    for (const auto& p : Presets())
        if (p.category.startsWith(QStringLiteral("unreal-ng: ")))
        {
            ++fromCatalogue;
            CHECK(p.node["do"]["kind"] == "action");
        }
    // Presets() is built once, from the shipped catalogue of the data directory the binary finds
    const auto shipped = cat::DefaultFile().empty() ? std::nullopt : cat::Load(cat::DefaultFile());
    CHECK(fromCatalogue == (shipped ? static_cast<int>(shipped->actions.size()) : 0));
    CHECK(CatalogueStep(c.actions[1]).value("hold", false) && !CatalogueStep(c.actions[0]).contains("hold"));
}

// Save writes format v2: a v1 document is converted first (one undo step), the notes say what changed
void TestSaveV2(EditorWidget& w, const QTemporaryDir& dir)
{
    std::printf("editor: save as v2\n");
    const QString file = dir.filePath(QStringLiteral("old.deck.json"));
    Write(file, json::parse(R"({"version":1,"name":"old","model":"plusxl","settings":{"webapi":"http://h:1"},
      "root":{"id":"home","type":"menu","items":[
        {"id":"p","type":"action","at":[0,0],"do":{"kind":"webapi","path":"/pause"}},
        {"id":"r","type":"action","at":[0,1],"do":{"kind":"run_toggle"}}]}})"));
    QString err;
    CHECK(w.OpenFile(file, &err));
    EditorDocument* d = w.Document();
    CHECK(d->Version() == 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    CHECK(!w.findChildren<QWidget*>(QStringLiteral("convert-v2")).isEmpty());  // the scenario form offers it
    CHECK(d->Save({}, &err));
    CHECK(d->Version() == 2 && !d->LastNotes().isEmpty());
    std::string e;
    const auto saved = io::LoadFile(file.toStdString(), &e);
    CHECK(saved && (*saved)["version"] == 2 && !(*saved)["settings"].contains("webapi"));
    CHECK(saved && (*saved)["root"]["items"][0]["do"] == json({{"kind", "action"}, {"id", "run.pause"}}));
    CHECK(saved && (*saved)["root"]["items"][1]["do"] == json({{"kind", "action"}, {"id", "run.pause_toggle"}}));
    d->UndoStack()->undo();  // the conversion is one step
    CHECK(d->Version() == 1);
    CHECK(!d->ConvertToV2() || d->Version() == 2);
    CHECK(!d->ConvertToV2());  // v2 already
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
    TestActionStep(w);
    TestSaveV2(w, dir);
}
