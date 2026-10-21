#include "editordocument.h"

#include <QDir>
#include <QFileInfo>
#include <QUndoCommand>
#include <algorithm>

#include "actioncatalogue.h"
#include "sdb/catalogue.h"
#include "sdb/scenarioio.h"

using namespace sdb;
using namespace sdb::scn;

namespace
{
class SnapshotCommand : public QUndoCommand
{
public:
    SnapshotCommand(EditorDocument* d, nlohmann::json before, nlohmann::json after, const QString& text)
        : QUndoCommand(text), _d(d), _before(std::move(before)), _after(std::move(after))
    {
    }
    void undo() override
    {
        _d->SetDoc(_before);
    }
    void redo() override
    {
        _d->SetDoc(_after);
    }

private:
    EditorDocument* _d;
    nlohmann::json _before, _after;
};
}  // namespace

EditorDocument::EditorDocument(QObject* parent) : QObject(parent)
{
    NewDocument(*FindModelByName("plusxl"), QStringLiteral("untitled"));
    connect(&_undo, &QUndoStack::cleanChanged, this, &EditorDocument::PathChanged);
}

const Model& EditorDocument::GetModel() const
{
    const Model* m = ops::ModelOf(_doc);
    return m ? *m : *FindModelByName("plusxl");
}

bool EditorDocument::Valid() const
{
    return std::none_of(_issues.begin(), _issues.end(), [](const Issue& i) { return i.error; });
}

void EditorDocument::NewDocument(const Model& m, const QString& name)
{
    _undo.clear();
    _path.clear();
    SetDoc(ops::NewScenario(m, name.toStdString()));
    _selected.clear();
    _canvasMenu = QStringLiteral("home");
    _undo.setClean();
    emit PathChanged();
    emit SelectionChanged();
    emit CanvasChanged();
}

bool EditorDocument::Open(const QString& path, QString* error)
{
    std::string err;
    auto d = io::LoadFile(path.toStdString(), &err);
    if (!d)
    {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    if (!d->contains("root"))
    {
        if (error)
            *error = QStringLiteral("not a scenario (no root)");
        return false;
    }
    _undo.clear();
    _path = QFileInfo(path).absoluteFilePath();  // includes resolve relative to it
    SetDoc(*d);
    _selected.clear();
    _canvasMenu = QString::fromStdString((*d)["root"].value("id", "home"));
    _undo.setClean();
    emit PathChanged();
    emit SelectionChanged();
    emit CanvasChanged();
    return true;
}

int EditorDocument::Version() const
{
    return _doc.contains("version") && _doc["version"].is_number_integer() ? _doc["version"].get<int>()
                                                                           : kFormatVersion;
}

bool EditorDocument::ConvertToV2(QStringList* notes)
{
    if (Version() >= 2)
        return false;
    std::vector<std::string> n;
    const json v2 = scn::ConvertToV2(_doc, &n);
    _notes.clear();
    for (const auto& line : n)
        _notes.append(QString::fromStdString(line));
    if (notes)
        *notes = _notes;
    Replace(v2, QStringLiteral("Convert to v2"));
    return true;
}

void EditorDocument::Recheck()
{
    Revalidate();
    emit Changed();
}

bool EditorDocument::Save(const QString& path, QString* error)
{
    const QString target = path.isEmpty() ? _path : path;
    if (target.isEmpty())
    {
        if (error)
            *error = QStringLiteral("no file name");
        return false;
    }
    _notes.clear();
    ConvertToV2();  // the editor writes the current format
    std::string err;
    if (!io::SaveFile(target.toStdString(), _doc, &err))
    {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    _path = target;
    _undo.setClean();
    emit PathChanged();
    return true;
}

bool EditorDocument::ExportTo(const QString& path, QString* error) const
{
    std::string err;
    if (!io::SaveFile(path.toStdString(), _doc, &err))
    {
        if (error)
            *error = QString::fromStdString(err);
        return false;
    }
    return true;
}

bool EditorDocument::Apply(const QString& text, const std::function<ops::Result(json&)>& op, QString* error,
                           std::string* id)
{
    json after = _doc;
    const auto r = op(after);
    if (!r.ok)
    {
        if (error)
            *error = QString::fromStdString(r.error);
        return false;
    }
    if (id)
        *id = r.id;
    if (after == _doc)
        return true;
    _undo.push(new SnapshotCommand(this, _doc, std::move(after), text));  // push runs redo() -> SetDoc
    return true;
}

bool EditorDocument::Replace(const json& doc, const QString& text)
{
    if (!doc.is_object() || !doc.contains("root"))
        return false;
    _undo.push(new SnapshotCommand(this, _doc, doc, text));
    return true;
}

void EditorDocument::SetDoc(const json& d)
{
    _doc = d;
    Revalidate();
    FixCanvas();
    emit Changed();
}

void EditorDocument::Revalidate()
{
    _issues.clear();
    std::string err;
    auto r = io::ResolveIncludes(_doc, BaseDir().toStdString(), &err);
    _resolved = r ? *r : _doc;
    if (!r)
        _issues.push_back({"", "includes: " + err, true});
    _loose = ParseLoose(_resolved);
    // the host's catalogue: unknown ids, held actions without hold (warnings; nothing to say without a catalogue)
    if (auto s = Parse(_resolved, _issues); s && !EditorCatalogue::Instance().Get().actions.empty())
        for (auto& i : cat::Check(*s, EditorCatalogue::Instance().Get()))
            _issues.push_back(std::move(i));
}

QString EditorDocument::BaseDir() const
{
    return _path.isEmpty() ? QDir::currentPath() : QFileInfo(_path).absolutePath();
}

QString EditorDocument::RelativePath(const QString& file) const
{
    const QString rel = QDir(BaseDir()).relativeFilePath(file);
    return rel.startsWith(QLatin1String("../../..")) ? QFileInfo(file).absoluteFilePath() : rel;
}

QString EditorDocument::IncludedFrom(const QString& id) const
{
    if (ops::FindNode(_doc, id.toStdString()))
        return {};
    const json* n = ops::FindNode(_resolved, id.toStdString());
    return n ? QString::fromStdString(n->value("included_from", "")) : QString();
}

void EditorDocument::FixCanvas()
{
    // the canvas menu or the selection may be gone (undo, delete, rename)
    const json* m = ops::FindNode(_resolved, _canvasMenu.toStdString());
    if (!m || m->value("type", "") != "menu")
    {
        _canvasMenu = QString::fromStdString(_doc["root"].value("id", "home"));
        emit CanvasChanged();
    }
    if (!_selected.isEmpty() && !ops::FindNode(_resolved, _selected.toStdString()))
    {
        _selected.clear();
        emit SelectionChanged();
    }
}

void EditorDocument::Select(const QString& id, bool showItsMenu)
{
    if (!id.isEmpty() && !ops::FindNode(_resolved, id.toStdString()))
        return;
    _selected = id;
    if (!id.isEmpty() && showItsMenu)
    {
        // show the menu that holds it (a menu itself is shown from its parent)
        if (json* p = ops::ParentOf(_resolved, id.toStdString()))
        {
            const QString pid = QString::fromStdString(p->value("id", ""));
            if (pid != _canvasMenu)
            {
                _canvasMenu = pid;
                emit CanvasChanged();
            }
        }
    }
    emit SelectionChanged();
}

bool EditorDocument::Enter(const QString& menuId)
{
    const json* m = ops::FindNode(_resolved, menuId.toStdString());
    if (!m || m->value("type", "") != "menu")
        return false;
    _canvasMenu = menuId;
    emit CanvasChanged();
    return true;
}

bool EditorDocument::Up()
{
    json* p = ops::ParentOf(_resolved, _canvasMenu.toStdString());
    if (!p)
        return false;
    const QString from = _canvasMenu;
    _canvasMenu = QString::fromStdString(p->value("id", ""));
    _selected = from;  // the menu we came from stays selected
    emit CanvasChanged();
    emit SelectionChanged();
    return true;
}
