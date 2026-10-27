#include "inspector.h"

#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSpinBox>
#include <QVBoxLayout>
#include <functional>

#include "actioncatalogue.h"
#include "editordocument.h"
#include "sdb/datadir.h"
#include "sdb/iconpacks.h"
#include "sdb/icons.h"
#include "sdb/scenarioops.h"
#include "sdb/surfacereadouts.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
QString Qs(const std::string& s)
{
    return QString::fromStdString(s);
}
std::string Str(const json& j, const char* k)
{
    return j.contains(k) && j[k].is_string() ? j[k].get<std::string>()
                                             : (j.contains(k) && !j[k].is_null() ? j[k].dump() : std::string());
}

// The main field of each action kind, edited in the "argument" box.
const char* ArgField(const std::string& kind)
{
    if (kind == "webapi")
        return "path";
    if (kind == "http")
        return "url";
    if (kind == "shell")
        return "command";
    if (kind == "open")
        return "target";
    if (kind == "keys")
        return "combo";
    if (kind == "nav")
        return "to";
    if (kind == "delay")
        return "ms";
    if (kind == "brightness")
        return "value";
    return nullptr;
}

QLineEdit* Line(const std::string& v)
{
    auto* e = new QLineEdit(Qs(v));
    return e;
}
}  // namespace

json ActionTemplate(const std::string& kind)
{
    const json pause{{"kind", "webapi"}, {"method", "POST"}, {"path", "/pause"}};
    const json resume{{"kind", "webapi"}, {"method", "POST"}, {"path", "/resume"}};
    if (kind == "action")  // a catalogue action (the inspector's picker lists them)
        return {{"kind", kind}, {"id", "run.pause_toggle"}};
    if (kind == "webapi")
        return pause;
    if (kind == "http")
        return {{"kind", kind}, {"method", "GET"}, {"url", "http://127.0.0.1/"}};
    if (kind == "multi")
        return {{"kind", kind}, {"steps", json::array({json{{"kind", "delay"}, {"ms", 100}}})}};
    if (kind == "toggle")
        return {{"kind", kind}, {"on", resume}, {"off", pause}};
    if (kind == "first")  // the first step that succeeds
        return {{"kind", kind}, {"try", json::array({resume, pause})}};
    if (kind == "dir")  // a dial: what a turn to the left / right runs
        return {{"kind", kind},
                {"left", {{"kind", "brightness"}, {"delta", -10}}},
                {"right", {{"kind", "brightness"}, {"delta", 10}}}};
    if (kind == "ttd_seek")  // a dial scrubbing time travel: 1..100 frames a detent with the turning speed
        return {{"kind", kind}, {"unit", "frames"}, {"min", 1}, {"max", 100}};
    if (kind == "shell")
        return {{"kind", kind}, {"command", "echo hello"}};
    if (kind == "open")
        return {{"kind", kind}, {"target", "https://"}};
    if (kind == "keys")
        return {{"kind", kind}, {"combo", "cmd+c"}};
    if (kind == "nav")
        return {{"kind", kind}, {"to", "back"}};
    if (kind == "delay")
        return {{"kind", kind}, {"ms", 500}};
    if (kind == "brightness")
        return {{"kind", kind}, {"delta", 10}};
    return {{"kind", kind}};  // run_toggle: no fields
}

QStringList ActionKindItems()
{
    QStringList v;
    for (const std::string_view k : ActionKinds())
        v.append(QString::fromLatin1(k.data(), static_cast<qsizetype>(k.size())));
    return v;
}

Inspector::Inspector(EditorDocument* doc, QWidget* parent) : QWidget(parent), _doc(doc)
{
    auto* col = new QVBoxLayout(this);
    col->setContentsMargins(6, 6, 6, 6);
    _title = new QLabel(this);
    QFont f = _title->font();
    f.setBold(true);
    _title->setFont(f);
    col->addWidget(_title);
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    _form = new QWidget;
    scroll->setWidget(_form);
    col->addWidget(scroll, 3);
    col->addWidget(new QLabel(QStringLiteral("JSON (everything; Apply = one undo step)")));
    _json = new QPlainTextEdit(this);
    QFont mono(QStringLiteral("Menlo"));
    mono.setStyleHint(QFont::Monospace);
    mono.setPointSize(10);
    _json->setFont(mono);
    col->addWidget(_json, 2);
    auto* apply = new QPushButton(QStringLiteral("Apply JSON"), this);
    col->addWidget(apply);
    connect(apply, &QPushButton::clicked, this, [this] {
        const json edited = json::parse(_json->toPlainText().toStdString(), nullptr, false);
        if (edited.is_discarded() || !edited.is_object())
        {
            emit Message(QStringLiteral("JSON: not an object"));
            return;
        }
        if (_shownId.isEmpty())
        {  // scenario settings
            QString err;
            _doc->Apply(
                QStringLiteral("Edit scenario"),
                [&](json& d) {
                    for (const char* k : {"name", "model", "settings"})
                    {
                        if (edited.contains(k))
                            d[k] = edited[k];
                        else if (std::string(k) == "settings")
                            d.erase(k);
                    }
                    return ops::Result{};
                },
                &err);
            return;
        }
        const json* old = ops::FindNode(_doc->Doc(), _shownId.toStdString());
        if (!old)
            return;
        json p = edited;
        p.erase("items");
        p.erase("at");
        for (const auto& [k, v] : old->items())
            if (!edited.contains(k) && k != "items" && k != "at")
                p[k] = nullptr;
        Patch(QStringLiteral("Edit JSON"), p);
    });
    connect(_doc, &EditorDocument::SelectionChanged, this, &Inspector::Rebuild);
    connect(_doc, &EditorDocument::Changed, this, [this] {
        // keep a field the user is typing in; everything else follows the document (undo, API edits)
        QWidget* fw = QApplication::focusWidget();
        if (fw && isAncestorOf(fw) && fw != _json)
            return;
        Rebuild();
    });
    Rebuild();
}

bool Inspector::Patch(const QString& text, const json& p)
{
    QString err;
    std::string newId;
    const std::string id = _shownId.toStdString();
    if (!_doc->Apply(text, [&](json& d) { return ops::UpdateNode(d, id, p); }, &err, &newId))
    {
        emit Message(err);
        Rebuild();
        return false;
    }
    if (!newId.empty() && Qs(newId) != _shownId)
        _doc->Select(Qs(newId), false);
    return true;
}

void Inspector::Rebuild()
{
    delete _form->layout();
    for (QObject* c : _form->children())
        if (c->isWidgetType())
            c->deleteLater();
    const QString sel = _doc->Selected();
    const json* raw = sel.isEmpty() ? nullptr : ops::FindNode(_doc->Doc(), sel.toStdString());
    if (raw && raw->value("type", "") == "include")
        BuildInclude(sel);
    else if (raw)
        BuildNode(sel);
    else if (!sel.isEmpty() && ops::FindNode(_doc->Resolved(), sel.toStdString()))
        BuildIncluded(sel);
    else
        BuildScenario();
}

void Inspector::BuildInclude(const QString& id)
{
    _shownId = id;
    const json n = *ops::FindNode(_doc->Doc(), id.toStdString());
    _title->setText(QStringLiteral("Include  %1").arg(id));
    auto* form = new QFormLayout(_form);
    form->addRow(new QLabel(QStringLiteral(
        "A link: the subtree lives in another file and every scenario that includes it sees its changes.")));
    auto field = [&](const char* key, const QString& label, const QString& tip) {
        auto* e = Line(n.value(key, ""));
        e->setToolTip(tip);
        form->addRow(label, e);
        connect(e, &QLineEdit::editingFinished, this, [this, e, key, old = Qs(n.value(key, ""))] {
            if (e->text() != old)
                Patch(QStringLiteral("Include"),
                      {{key, e->text().isEmpty() ? json(nullptr) : json(e->text().toStdString())}});
        });
    };
    field("file", QStringLiteral("File"), QStringLiteral("relative to this scenario's folder"));
    field("node", QStringLiteral("Node"), QStringLiteral("a node id in that file; empty = its root"));
    field("title", QStringLiteral("Title"), QStringLiteral("overrides the included root's title"));
    auto* open = new QPushButton(QStringLiteral("Open the file in the editor"));
    auto* inl = new QPushButton(QStringLiteral("Inline here (editable copy)"));
    form->addRow(open);
    form->addRow(inl);
    const QString file = QDir(_doc->BaseDir()).absoluteFilePath(Qs(n.value("file", "")));
    connect(open, &QPushButton::clicked, this, [this, file] { emit OpenRequested(file); });
    connect(inl, &QPushButton::clicked, this, [this, id] { emit InlineRequested(id); });
    _json->setPlainText(Qs(n.dump(2)));
}

void Inspector::BuildIncluded(const QString& id)
{
    _shownId.clear();
    const json n = *ops::FindNode(_doc->Resolved(), id.toStdString());
    const QString file = Qs(n.value("included_from", ""));
    _title->setText(QStringLiteral("%1  %2  (read-only)")
                        .arg(n.value("type", "") == "menu" ? QStringLiteral("Menu") : QStringLiteral("Action"), id));
    auto* form = new QFormLayout(_form);
    form->addRow(new QLabel(QStringLiteral("From %1. Edit it there, or inline the include.").arg(file)));
    auto* open = new QPushButton(QStringLiteral("Open %1").arg(QFileInfo(file).fileName()));
    form->addRow(open);
    connect(open, &QPushButton::clicked, this, [this, file] { emit OpenRequested(file); });
    json shown = n;
    shown.erase("items");
    _json->setPlainText(Qs(shown.dump(2)));
}

void Inspector::BuildScenario()
{
    _shownId.clear();
    const json& d = _doc->Doc();
    _title->setText(QStringLiteral("Scenario"));
    auto* form = new QFormLayout(_form);
    auto* name = Line(d.value("name", ""));
    form->addRow(QStringLiteral("Name"), name);
    connect(name, &QLineEdit::editingFinished, this, [this, name] {
        _doc->Apply(QStringLiteral("Rename scenario"), [&](json& x) {
            x["name"] = name->text().toStdString();
            return ops::Result{};
        });
    });
    // every model of the model table, by its token; the document's own name for it (an alias such as "mk2") selects
    // the same entry, an unknown one is shown as is
    auto* model = new QComboBox;
    model->setObjectName(QStringLiteral("model"));
    for (const Model& m : AllModels())
    {
        const QString token = Qs(ModelToken(m));
        model->addItem(QStringLiteral("%1 (%2)").arg(Qs(std::string(m.name)), token), token);
    }
    const std::string current = d.value("model", "plusxl");
    if (const Model* m = FindModelByName(current))
        model->setCurrentIndex(model->findData(Qs(ModelToken(*m))));
    else
    {
        model->addItem(QStringLiteral("%1 (unknown)").arg(Qs(current)), Qs(current));
        model->setCurrentIndex(model->count() - 1);
    }
    form->addRow(QStringLiteral("Model"), model);
    connect(model, &QComboBox::currentIndexChanged, this, [this, model] {
        _doc->Apply(QStringLiteral("Change model"), [&](json& x) {
            x["model"] = model->currentData().toString().toStdString();
            return ops::Result{};
        });
    });
    const json st = d.value("settings", json::object());
    const json nav = st.value("navigation", json::object());
    auto setNav = [this](const char* key, json v) {
        _doc->Apply(QStringLiteral("Navigation setting"), [&](json& x) {
            x["settings"]["navigation"][key] = v;
            return ops::Result{};
        });
    };
    auto* stack = new QCheckBox(QStringLiteral("stack navigation (back / breadcrumbs)"));
    stack->setChecked(nav.value("stack", true));
    form->addRow(QString(), stack);
    connect(stack, &QCheckBox::toggled, this, [setNav](bool on) { setNav("stack", on); });
    auto* crumbs = new QComboBox;
    crumbs->addItems({QStringLiteral("auto"), QStringLiteral("strip"), QStringLiteral("key"), QStringLiteral("none")});
    crumbs->setCurrentText(Qs(nav.value("breadcrumbs", "auto")));
    form->addRow(QStringLiteral("Breadcrumbs"), crumbs);
    connect(crumbs, &QComboBox::currentTextChanged, this,
            [setNav](const QString& t) { setNav("breadcrumbs", t.toStdString()); });
    auto* hold = new QCheckBox(QStringLiteral("hold back key = home"));
    hold->setChecked(nav.value("home_on_hold", true));
    form->addRow(QString(), hold);
    connect(hold, &QCheckBox::toggled, this, [setNav](bool on) { setNav("home_on_hold", on); });
    const int version = d.contains("version") && d["version"].is_number_integer() ? d["version"].get<int>()
                                                                                    : kFormatVersion;
    auto setSetting = [this](const QString& text, const char* key, json v) {
        _doc->Apply(text, [&](json& x) {
            if (v.is_null())
                x["settings"].erase(key);
            else
                x["settings"][key] = v;
            return ops::Result{};
        });
    };
    if (version < 2)
    {
        // v1: the host and the instance were the scenario's; v2 leaves them to the daemon and the host
        form->addRow(new QLabel(QStringLiteral("Format v1: Save writes v2 (the REST steps that have a catalogue "
                                               "action become action steps)")));
        auto* convert = new QPushButton(QStringLiteral("Convert to v2 now"));
        convert->setObjectName(QStringLiteral("convert-v2"));
        form->addRow(convert);
        connect(convert, &QPushButton::clicked, this, [this] {
            QStringList notes;
            _doc->ConvertToV2(&notes);
            emit Message(notes.isEmpty() ? QStringLiteral("converted to v2") : notes.join(QStringLiteral("; ")));
        });
        auto* webapi = Line(st.value("webapi", "http://127.0.0.1:8090"));
        form->addRow(QStringLiteral("unreal-ng WebAPI"), webapi);
        connect(webapi, &QLineEdit::editingFinished, this,
                [setSetting, webapi] { setSetting(QStringLiteral("WebAPI URL"), "webapi", webapi->text().toStdString()); });
        auto* emu = Line(st.value("emulator", "auto"));
        emu->setToolTip(QStringLiteral("auto = unreal-qt, or an emulator id / symbolic id"));
        form->addRow(QStringLiteral("Emulator"), emu);
        connect(emu, &QLineEdit::editingFinished, this,
                [setSetting, emu] { setSetting(QStringLiteral("Emulator id"), "emulator", emu->text().toStdString()); });
    }
    else
    {
        // v2 (scenarios.md §1, §5): which instance the deck drives, how it shows other surfaces' actions
        auto* target = new QComboBox;
        target->setObjectName(QStringLiteral("target"));
        target->addItem(QStringLiteral("follow the focused view"), QStringLiteral("follow"));
        for (int v = 1; v <= 4; ++v)
            target->addItem(QStringLiteral("view %1").arg(v), v);
        const json t = st.value("target", json("follow"));
        target->setCurrentIndex(t.is_object() ? std::max(0, target->findData(t.value("view", 1))) : 0);
        form->addRow(QStringLiteral("Target"), target);
        connect(target, &QComboBox::currentIndexChanged, this, [setSetting, target](int i) {
            setSetting(QStringLiteral("Target"), "target",
                       i == 0 ? json(nullptr) : json{{"view", target->itemData(i).toInt()}});
        });
        auto* pack = new QComboBox;
        pack->setObjectName(QStringLiteral("pack"));
        pack->addItem(QStringLiteral("(default: %1)").arg(QLatin1String(packs::kDefaultPack)), QString());
        for (const auto& p : packs::List())
            pack->addItem(Qs(p.title.empty() ? p.name : p.title + " (" + p.name + ")"), Qs(p.name));
        const QString curPack = Qs(st.value("pack", ""));
        if (!curPack.isEmpty() && pack->findData(curPack) < 0)
            pack->addItem(curPack, curPack);
        pack->setCurrentIndex(std::max(0, pack->findData(curPack)));
        pack->setToolTip(QStringLiteral("settings.pack: the icon pack of the glyph faces; a deck's own pack setting "
                                        "(the toolbar) wins"));
        form->addRow(QStringLiteral("Icon pack"), pack);
        connect(pack, &QComboBox::currentIndexChanged, this, [setSetting, pack](int i) {
            const QString v = pack->itemData(i).toString();
            setSetting(QStringLiteral("Icon pack"), "pack", v.isEmpty() ? json(nullptr) : json(v.toStdString()));
        });
        const json act = st.value("activity", json::object());
        auto setActivity = [this](const char* key, json v) {
            _doc->Apply(QStringLiteral("Activity"), [&](json& x) {
                x["settings"]["activity"][key] = v;
                return ops::Result{};
            });
        };
        auto* enabled = new QCheckBox(QStringLiteral("show what other surfaces do"));
        enabled->setObjectName(QStringLiteral("activity-enabled"));
        enabled->setChecked(act.value("enabled", true));
        form->addRow(QStringLiteral("Activity"), enabled);
        connect(enabled, &QCheckBox::toggled, this, [setActivity](bool on) { setActivity("enabled", on); });
        auto* flash = new QCheckBox(QStringLiteral("flash the keys of the same action"));
        flash->setChecked(act.value("flash_keys", true));
        form->addRow(QString(), flash);
        connect(flash, &QCheckBox::toggled, this, [setActivity](bool on) { setActivity("flash_keys", on); });
        auto* line = new QComboBox;
        line->addItems({QStringLiteral("none"), QStringLiteral("line")});
        line->setCurrentText(Qs(act.value("strip", "none")));
        line->setToolTip(QStringLiteral("line: a message on the strip, e.g. \"PAUSE · Xbox pad\""));
        form->addRow(QStringLiteral("Activity strip"), line);
        connect(line, &QComboBox::currentTextChanged, this,
                [setActivity](const QString& v) { setActivity("strip", v.toStdString()); });
    }
    json shown{{"name", d.value("name", "")}, {"model", d.value("model", "")}};
    if (d.contains("settings"))
        shown["settings"] = d["settings"];
    _json->setPlainText(Qs(shown.dump(2)));
}

void Inspector::BuildNode(const QString& id)
{
    _shownId = id;
    const json n = *ops::FindNode(_doc->Doc(), id.toStdString());
    const bool menu = n.value("type", "") == "menu";
    _title->setText(QStringLiteral("%1  %2").arg(menu ? QStringLiteral("Menu") : QStringLiteral("Action"), id));
    auto* form = new QFormLayout(_form);
    auto text = [&](const QString& label, const std::string& value, std::function<json(const QString&)> make,
                    const QString& tip = {}) {
        auto* e = Line(value);
        e->setToolTip(tip);
        form->addRow(label, e);
        connect(e, &QLineEdit::editingFinished, this, [this, e, make, label, old = Qs(value)] {
            if (e->text() == old)
                return;
            Patch(QStringLiteral("Edit ") + label, make(e->text()));
        });
        return e;
    };
    text(
        QStringLiteral("Id"), n.value("id", ""), [](const QString& v) { return json{{"id", v.toStdString()}}; },
        QStringLiteral("unique; nav targets follow a rename"));
    text(QStringLiteral("Title"), n.value("title", ""),
         [](const QString& v) { return json{{"title", v.toStdString()}}; });
    std::string tags;
    for (const auto& t : n.value("tags", json::array()))
        tags += (tags.empty() ? "" : ", ") + (t.is_string() ? t.get<std::string>() : t.dump());
    text(
        QStringLiteral("Tags"), tags,
        [](const QString& v) {
            json a = json::array();
            for (const QString& t : v.split(QRegularExpression(QStringLiteral("[,;]")), Qt::SkipEmptyParts))
                if (!t.trimmed().isEmpty())
                    a.push_back(t.trimmed().toStdString());
            return a.empty() ? json{{"tags", nullptr}} : json{{"tags", a}};
        },
        QStringLiteral("comma separated; agents filter by tag (GET /api/v1/editor/nodes?tag=...)"));
    if (n.contains("at"))
        form->addRow(QStringLiteral("Key"), new QLabel(QStringLiteral("row %1, col %2  (drag on the canvas to move)")
                                                           .arg(n["at"][0].get<int>())
                                                           .arg(n["at"][1].get<int>())));

    // face
    const json face = n.value("face", json::object());
    auto faceField = [&](const char* key, const QString& label) {
        return text(label, Str(face, key), [key](const QString& v) {
            return json{{"face", {{key, v.isEmpty() ? json(nullptr) : json(v.toStdString())}}}};
        });
    };
    faceField("label", QStringLiteral("Label"));
    auto* icon = new QComboBox;
    icon->setEditable(false);
    icon->addItem(QStringLiteral("(none)"), QString());
    for (const auto& nm : IconNames())
        icon->addItem(Qs(nm), Qs(nm));
    icon->setCurrentIndex(std::max(0, icon->findData(Qs(Str(face, "icon")))));
    form->addRow(QStringLiteral("Icon"), icon);
    connect(icon, &QComboBox::currentIndexChanged, this, [this, icon] {
        const QString v = icon->currentData().toString();
        Patch(QStringLiteral("Icon"), {{"face", {{"icon", v.isEmpty() ? json(nullptr) : json(v.toStdString())}}}});
    });
    // glyphs (face.glyph, face_on.glyph): drawn from the deck's icon pack; an id no pack has is shown as typed
    auto glyphBox = [&](const char* faceKey, const json& fc, const QString& label) {
        auto* g = new QComboBox;
        g->setObjectName(QStringLiteral("glyph-") + QLatin1String(faceKey));
        g->setEditable(true);
        g->setInsertPolicy(QComboBox::NoInsert);
        g->addItem(QString());
        for (const auto& id : packs::GlyphIds())
            g->addItem(Qs(id));
        const QString cur = Qs(Str(fc, "glyph"));
        if (!cur.isEmpty() && g->findText(cur) < 0)
            g->addItem(cur);
        g->setCurrentText(cur);
        g->setToolTip(QStringLiteral("a glyph of data/streamdeck/icons/glyphs.json, drawn from the deck's icon pack "
                                     "(the label under it); empty = none"));
        form->addRow(label, g);
        auto apply = [this, g, faceKey, old = cur] {
            const QString v = g->currentText().trimmed();
            if (v == old)
                return;
            Patch(QStringLiteral("Glyph"),
                  {{faceKey, {{"glyph", v.isEmpty() ? json(nullptr) : json(v.toStdString())}}}});
        };
        connect(g, &QComboBox::activated, this, apply);
        connect(g->lineEdit(), &QLineEdit::editingFinished, this, apply);
    };
    glyphBox("face", face, QStringLiteral("Glyph"));
    if (!menu)
        glyphBox("face_on", n.value("face_on", json::object()), QStringLiteral("Glyph when on"));
    // a value face (face.value): the key shows a readout's value as big text, the label as its caption
    {
        const json v = face.value("value", json::object());
        auto* readout = new QComboBox;
        readout->setObjectName(QStringLiteral("value-readout"));
        readout->setEditable(true);
        readout->setInsertPolicy(QComboBox::NoInsert);
        readout->addItem(QString());
        for (const auto& name : KnownReadouts())
            readout->addItem(Qs(std::string(name)));
        const QString cur = Qs(Str(v, "readout"));
        if (!cur.isEmpty() && readout->findText(cur) < 0)
            readout->addItem(cur);
        readout->setCurrentText(cur);
        readout->setToolTip(QStringLiteral("a readout path the key shows (host.stats.cpu_percent, emulator.stats, "
                                           "debug.breakpoints.total); empty = no value face"));
        form->addRow(QStringLiteral("Value"), readout);
        auto apply = [this, readout, old = cur] {
            const QString r = readout->currentText().trimmed();
            if (r == old)
                return;
            Patch(QStringLiteral("Value"),
                  {{"face", {{"value", r.isEmpty() ? json(nullptr) : json{{"readout", r.toStdString()}}}}}});
        };
        connect(readout, &QComboBox::activated, this, apply);
        connect(readout->lineEdit(), &QLineEdit::editingFinished, this, apply);
        if (!cur.isEmpty())
        {
            auto* fmt = text(
                QStringLiteral("Format"), Str(v, "format"),
                [](const QString& t) {
                    return json{{"face", {{"value", {{"format", t.isEmpty() ? json(nullptr) : json(t.toStdString())}}}}}};
                },
                QStringLiteral("a template: {:.0f}% (the value), {fps:.0f} FPS (a field), {rss_bytes:b} (bytes), "
                               "{recording?REC|IDLE}, {slots:#} (a count); empty = {}"));
            fmt->setObjectName(QStringLiteral("value-format"));
            for (const char* t : {"warn", "alert"})
            {
                const std::string now = v.contains(t) && v[t].is_number() ? v[t].dump() : std::string();
                text(
                    std::string(t) == "warn" ? QStringLiteral("Warn at") : QStringLiteral("Alert at"), now,
                    [t](const QString& s) {
                        bool ok = false;
                        const double d = s.trimmed().toDouble(&ok);
                        return json{{"face", {{"value", {{t, ok ? json(d) : json(nullptr)}}}}}};
                    },
                    QStringLiteral("the text turns amber (warn) / red (alert) at this number; warn above alert: "
                                   "low values are the bad ones; empty = none"));
            }
        }
    }
    for (const char* key : {"bg", "fg"})
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* e = Line(Str(face, key));
        auto* pick = new QPushButton(QStringLiteral("..."));
        pick->setFixedWidth(32);
        h->addWidget(e);
        h->addWidget(pick);
        form->addRow(std::string(key) == "bg" ? QStringLiteral("Background") : QStringLiteral("Foreground"), row);
        connect(e, &QLineEdit::editingFinished, this, [this, e, key] {
            Patch(QStringLiteral("Colour"),
                  {{"face", {{key, e->text().isEmpty() ? json(nullptr) : json(e->text().toStdString())}}}});
        });
        connect(pick, &QPushButton::clicked, this, [this, e, key] {
            const QColor c = QColorDialog::getColor(
                QColor(QStringLiteral("#") + (e->text().isEmpty() ? QStringLiteral("10101C") : e->text())), this);
            if (c.isValid())
                Patch(QStringLiteral("Colour"), {{"face", {{key, c.name().mid(1).toUpper().toStdString()}}}});
        });
    }
    {
        auto* row = new QWidget;
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        auto* e = Line(Str(face, "image"));
        auto* pick = new QPushButton(QStringLiteral("..."));
        pick->setFixedWidth(32);
        h->addWidget(e);
        h->addWidget(pick);
        form->addRow(QStringLiteral("Image"), row);
        connect(e, &QLineEdit::editingFinished, this, [this, e] {
            Patch(QStringLiteral("Image"),
                  {{"face", {{"image", e->text().isEmpty() ? json(nullptr) : json(e->text().toStdString())}}}});
        });
        connect(pick, &QPushButton::clicked, this, [this] {
            const QString f = QFileDialog::getOpenFileName(this, QStringLiteral("Key image"), QString(),
                                                           QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.bmp)"));
            if (!f.isEmpty())
                Patch(QStringLiteral("Image"), {{"face", {{"image", ScenarioPath(f)}}}});
        });
    }

    if (!menu)
    {
        const json d = n.value("do", json::object());
        const std::string kind = d.value("kind", "");
        auto* kindBox = new QComboBox;
        kindBox->setObjectName(QStringLiteral("kind"));
        kindBox->addItems(ActionKindItems());
        if (kindBox->findText(Qs(kind)) < 0)
            kindBox->addItem(Qs(kind));  // not a kind the engine runs (the issues say so): shown as it is
        kindBox->setCurrentText(Qs(kind));
        form->addRow(QStringLiteral("Action"), kindBox);
        connect(kindBox, &QComboBox::currentTextChanged, this, [this, kind](const QString& k) {
            if (k.toStdString() == kind)
                return;
            QString err;
            const std::string nid = _shownId.toStdString();
            if (!_doc->Apply(
                    QStringLiteral("Action kind"),
                    [&](json& d) {
                        json* node = ops::FindNode(d, nid);
                        if (!node)
                            return ops::Result::Fail("gone");
                        (*node)["do"] = ActionTemplate(k.toStdString());
                        return ops::Result{true, {}, nid};
                    },
                    &err))
                emit Message(err);
        });
        if (kind == "action")
            BuildActionStep(form, d);
        if (kind == "webapi")
            form->addRow(new QLabel(QStringLiteral("REST, breaks when the route changes: prefer an action")));
        if (kind == "webapi" || kind == "http")
        {
            auto* method = new QComboBox;
            method->addItems(
                {QStringLiteral("GET"), QStringLiteral("POST"), QStringLiteral("PUT"), QStringLiteral("DELETE")});
            method->setCurrentText(Qs(d.value("method", "POST")));
            form->addRow(QStringLiteral("Method"), method);
            connect(method, &QComboBox::currentTextChanged, this, [this](const QString& m) {
                Patch(QStringLiteral("Method"), {{"do", {{"method", m.toStdString()}}}});
            });
        }
        if (const char* f = ArgField(kind))
        {
            text(
                QString::fromLatin1(f), Str(d, f),
                [f, kind](const QString& v) {
                    json val = v.toStdString();
                    if (kind == "delay" || kind == "brightness")
                    {
                        bool ok;
                        const int i = v.toInt(&ok);
                        if (ok)
                            val = i;
                    }
                    return json{{"do", {{f, val}}}};
                },
                kind == "webapi"
                    ? QStringLiteral("relative to /api/v1/emulator/{id}, e.g. /pause, /stepover, /tape/play")
                    : QString());
        }
        if (kind == "nav" && d.value("to", "") == "node")
            text(QStringLiteral("node"), d.value("node", ""),
                 [](const QString& v) { return json{{"do", {{"node", v.toStdString()}}}}; });
        if (kind == "webapi" || kind == "http")
        {
            text(
                QStringLiteral("body"), d.contains("body") ? d["body"].dump() : std::string(),
                [](const QString& v) {
                    json b = json::parse(v.toStdString(), nullptr, false);
                    return json{{"do",
                                 {{"body", v.isEmpty()        ? json(nullptr)
                                           : b.is_discarded() ? json(v.toStdString())
                                                              : b}}}};
                },
                QStringLiteral("JSON; \"$ticks\" = dial ticks"));
        }
        text(
            QStringLiteral("State"), n.value("state", ""),
            [](const QString& v) {
                return json{{"state", v.trimmed().isEmpty() ? json(nullptr) : json(v.trimmed().toStdString())}};
            },
            QStringLiteral("a readout: the face is face_on while it is true (run.state.paused, toggle.<action id>, "
                           "audio.mute.muted, debug.mode.enabled)"));
        auto* then = new QComboBox;
        then->addItems({QStringLiteral("stay"), QStringLiteral("back"), QStringLiteral("home")});
        then->setCurrentText(Qs(n.value("then", "stay")));
        form->addRow(QStringLiteral("Then"), then);
        connect(then, &QComboBox::currentTextChanged, this, [this](const QString& t) {
            Patch(QStringLiteral("Then"),
                  {{"then", t == QLatin1String("stay") ? json(nullptr) : json(t.toStdString())}});
        });
    }
    else
    {
        text(
            QStringLiteral("Context"), n.value("context", ""),
            [](const QString& v) {
                return json{{"context", v.trimmed().isEmpty() ? json(nullptr) : json(v.trimmed().toStdString())}};
            },
            QStringLiteral("entering the menu sets this surface context (play, ttd, debug, ...); a context the "
                           "emulator sets brings the deck here"));
        const json nav = n.value("nav", json::object());
        auto* crumbs = new QComboBox;
        crumbs->addItems(
            {QStringLiteral("inherit"), QStringLiteral("strip"), QStringLiteral("key"), QStringLiteral("none")});
        crumbs->setCurrentText(Qs(nav.value("breadcrumbs", "inherit")));
        form->addRow(QStringLiteral("Breadcrumbs"), crumbs);
        connect(crumbs, &QComboBox::currentTextChanged, this, [this](const QString& t) {
            Patch(QStringLiteral("Breadcrumbs"),
                  {{"nav", {{"breadcrumbs", t == QLatin1String("inherit") ? json(nullptr) : json(t.toStdString())}}}});
        });
        std::string back =
            !nav.contains("back") ? ""
            : (nav["back"].is_null() || nav["back"] == false)
                ? "none"
                : std::to_string(nav["back"][0].get<int>()) + ", " + std::to_string(nav["back"][1].get<int>());
        text(
            QStringLiteral("Back key"), back,
            [](const QString& v) {
                const QString t = v.trimmed();
                if (t.isEmpty())
                    return json{{"nav", {{"back", nullptr}}}};  // merge patch null = remove = inherit
                if (t == QLatin1String("none"))
                    return json{{"nav", {{"back", false}}}};
                const auto parts = t.split(QLatin1Char(','));
                if (parts.size() != 2)
                    return json{{"nav", json::object()}};
                return json{{"nav", {{"back", {parts[0].trimmed().toInt(), parts[1].trimmed().toInt()}}}}};
            },
            QStringLiteral("\"row, col\"; none = no back key; empty = inherit"));
        auto* autoExit = new QSpinBox;
        autoExit->setRange(-1, 3600);
        autoExit->setSpecialValueText(QStringLiteral("inherit"));
        autoExit->setSuffix(QStringLiteral(" s"));
        autoExit->setValue(nav.value("auto_exit", -1));
        form->addRow(QStringLiteral("Auto exit"), autoExit);
        connect(autoExit, &QSpinBox::editingFinished, this, [this, autoExit] {
            Patch(QStringLiteral("Auto exit"),
                  {{"nav", {{"auto_exit", autoExit->value() < 0 ? json(nullptr) : json(autoExit->value())}}}});
        });
        auto* dials = new QComboBox;
        dials->addItems({QStringLiteral("inherit"), QStringLiteral("own")});
        dials->setCurrentText(Qs(nav.value("dials", "inherit")));
        form->addRow(QStringLiteral("Dials"), dials);
        connect(dials, &QComboBox::currentTextChanged, this, [this](const QString& t) {
            Patch(QStringLiteral("Dials"),
                  {{"nav", {{"dials", t == QLatin1String("inherit") ? json(nullptr) : json(t.toStdString())}}}});
        });
        const json strip = n.value("strip", json::object());
        auto* content = new QComboBox;
        content->addItems({QStringLiteral("title"), QStringLiteral("text"), QStringLiteral("breadcrumbs"),
                           QStringLiteral("image"), QStringLiteral("none")});
        content->setCurrentText(Qs(strip.value("content", "title")));
        form->addRow(QStringLiteral("Strip"), content);
        connect(content, &QComboBox::currentTextChanged, this, [this](const QString& t) {
            Patch(QStringLiteral("Strip"), {{"strip", {{"content", t.toStdString()}}}});
        });
        text(QStringLiteral("Strip text"), strip.value("text", ""), [](const QString& v) {
            return json{{"strip", {{"text", v.isEmpty() ? json(nullptr) : json(v.toStdString())}}}};
        });
        form->addRow(QStringLiteral("Items"), new QLabel(QString::number(n.value("items", json::array()).size())));
    }
    json shown = n;
    shown.erase("items");
    _json->setPlainText(Qs(shown.dump(2)));
}

std::string Inspector::ScenarioPath(const QString& file) const
{
    // a picked file as the scenario should name it: under the data directory $data/..., else relative to the
    // scenario's folder (paths are relative to it, not to the working directory); absolute only for an unsaved one
    const std::string path = QDir::cleanPath(file).toStdString();
    const auto root = sdb::data::Root();
    if (!root.empty())
    {
        const std::string r = root.lexically_normal().generic_string();
        if (path.size() > r.size() && path.compare(0, r.size(), r) == 0 && path[r.size()] == '/')
            return "$data/" + path.substr(r.size() + 1);
    }
    if (_doc->Path().isEmpty())
        return path;
    return _doc->RelativePath(file).toStdString();
}

void Inspector::SetStep(const QString& text, const char* key, const json& value)
{
    QString err;
    const std::string id = _shownId.toStdString();
    if (!_doc->Apply(
            text,
            [&](json& d) {
                json* node = ops::FindNode(d, id);
                if (!node || !node->contains("do"))
                    return ops::Result::Fail("gone");
                if (value.is_null())
                    (*node)["do"].erase(key);
                else
                    (*node)["do"][key] = value;  // replaced as a whole (a merge patch would merge objects)
                return ops::Result{true, {}, id};
            },
            &err))
        emit Message(err);
}

void Inspector::BuildActionStep(QFormLayout* form, const json& d)
{
    // the catalogue action: a picker (group: label [id]), its params and kind, value and hold (scenarios.md §2)
    EditorCatalogue& catalogue = EditorCatalogue::Instance();
    const std::string aid = d.value("id", "");
    auto* pick = new QComboBox;
    pick->setObjectName(QStringLiteral("action-id"));
    pick->setEditable(true);
    pick->setInsertPolicy(QComboBox::NoInsert);
    for (const auto& e : catalogue.Get().actions)
        pick->addItem(Qs(e.group + ": " + e.label + "  [" + e.id + "]"), Qs(e.id));
    int at = pick->findData(Qs(aid));
    if (at < 0)
    {
        pick->addItem(Qs(aid + "  (not in the catalogue)"), Qs(aid));
        at = pick->count() - 1;
    }
    pick->setCurrentIndex(at);
    form->addRow(QStringLiteral("Action id"), pick);
    auto setId = [this, aid](QString t) {
        t = t.trimmed();
        const qsizetype lb = t.lastIndexOf(QLatin1Char('[')), rb = t.lastIndexOf(QLatin1Char(']'));
        if (lb >= 0 && rb > lb)
            t = t.mid(lb + 1, rb - lb - 1);  // "Group: Label  [id]" -> id
        if (const qsizetype sp = t.indexOf(QLatin1Char(' ')); sp > 0)
            t = t.left(sp);
        if (!t.isEmpty() && t.toStdString() != aid)
            SetStep(QStringLiteral("Action id"), "id", t.toStdString());
    };
    connect(pick, &QComboBox::activated, this,
            [pick, setId](int i) { setId(pick->itemData(i).toString()); });
    connect(pick->lineEdit(), &QLineEdit::editingFinished, this, [pick, setId] { setId(pick->currentText()); });

    const sdb::cat::Entry* e = catalogue.Get().Find(aid);
    QString about = e ? QStringLiteral("%1 action%2%3").arg(Qs(e->kind), e->held ? QStringLiteral(", held") : QString(),
                                                           e->gui ? QStringLiteral(", unreal-qt only") : QString())
                      : QStringLiteral("not in the catalogue of %1").arg(catalogue.Source());
    if (e && !e->params.empty())
        about += QStringLiteral("\nparams: ") + Qs(e->params);
    auto* info = new QLabel(about);
    info->setObjectName(QStringLiteral("action-info"));
    info->setWordWrap(true);
    form->addRow(QString(), info);

    auto* value = Line(d.contains("value") ? (d["value"].is_string() ? d["value"].get<std::string>() : d["value"].dump())
                                           : std::string());
    value->setObjectName(QStringLiteral("action-value"));
    value->setToolTip(QStringLiteral(
        "JSON: the params of a press ({\"model\": \"PENTAGON\"}), a dial's delta ({\"d\": \"$ticks\", \"rate\": "
        "\"$rate\", ...params}), the text of a text action; empty = a plain press"));
    form->addRow(QStringLiteral("Value"), value);
    connect(value, &QLineEdit::editingFinished, this, [this, value, old = value->text()] {
        if (value->text() == old)
            return;
        const std::string t = value->text().trimmed().toStdString();
        json v = t.empty() ? json(nullptr) : json::parse(t, nullptr, false);
        if (v.is_discarded())
            v = t;  // plain text (ctx.set, input.type)
        SetStep(QStringLiteral("Action value"), "value", v);
    });
    auto* hold = new QCheckBox(QStringLiteral("hold: press on the key's press, release on its release"));
    hold->setObjectName(QStringLiteral("action-hold"));
    hold->setChecked(d.value("hold", false));
    form->addRow(QString(), hold);
    connect(hold, &QCheckBox::toggled, this,
            [this](bool on) { SetStep(QStringLiteral("Hold"), "hold", on ? json(true) : json(nullptr)); });
    auto* refresh = new QPushButton(QStringLiteral("Catalogue from the running emulator"));
    refresh->setObjectName(QStringLiteral("catalogue-refresh"));
    refresh->setToolTip(QStringLiteral("GET /api/v1/controllers/actions; now: %1").arg(catalogue.Source()));
    form->addRow(refresh);
    connect(refresh, &QPushButton::clicked, this, [this] {
        const json st = _doc->Doc().value("settings", json::object());
        QString msg;
        EditorCatalogue::Instance().Refresh(st.value("webapi", DefaultHost()), &msg);
        emit Message(msg);
        _doc->Recheck();
        Rebuild();
    });
}
