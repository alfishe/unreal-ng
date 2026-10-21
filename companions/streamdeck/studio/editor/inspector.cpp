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

#include "editordocument.h"
#include "sdb/icons.h"
#include "sdb/scenarioops.h"

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
    auto* webapi = Line(st.value("webapi", "http://127.0.0.1:8090"));
    form->addRow(QStringLiteral("unreal-ng WebAPI"), webapi);
    connect(webapi, &QLineEdit::editingFinished, this, [this, webapi] {
        _doc->Apply(QStringLiteral("WebAPI URL"), [&](json& x) {
            x["settings"]["webapi"] = webapi->text().toStdString();
            return ops::Result{};
        });
    });
    auto* emu = Line(st.value("emulator", "auto"));
    emu->setToolTip(QStringLiteral("auto = unreal-qt, or an emulator id / symbolic id"));
    form->addRow(QStringLiteral("Emulator"), emu);
    connect(emu, &QLineEdit::editingFinished, this, [this, emu] {
        _doc->Apply(QStringLiteral("Emulator id"), [&](json& x) {
            x["settings"]["emulator"] = emu->text().toStdString();
            return ops::Result{};
        });
    });
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
                Patch(QStringLiteral("Image"), {{"face", {{"image", f.toStdString()}}}});
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
