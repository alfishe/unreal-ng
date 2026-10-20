// The scenario being edited: the JSON document, its file, undo history, validation, selection and the menu the
// canvas shows. Every change goes through Apply() as one undo step (snapshots of the whole document: scenarios are
// small, and a snapshot undo can never drift from the document).
#pragma once
#include <QObject>
#include <QString>
#include <QUndoStack>
#include <functional>
#include <optional>
#include <vector>

#include "sdb/scenario.h"
#include "sdb/scenarioops.h"

class EditorDocument : public QObject
{
    Q_OBJECT
public:
    using json = nlohmann::json;
    explicit EditorDocument(QObject* parent = nullptr);

    [[nodiscard]] const json& Doc() const
    {
        return _doc;
    }
    // The document with every include replaced by its subtree (what the canvas shows and the runtime plays).
    [[nodiscard]] const json& Resolved() const
    {
        return _resolved;
    }
    // The file an included node comes from ("" = the node is in this document and editable).
    [[nodiscard]] QString IncludedFrom(const QString& id) const;
    [[nodiscard]] QString BaseDir() const;  // the folder relative include paths start from
    [[nodiscard]] QString RelativePath(const QString& file) const;
    [[nodiscard]] const QString& Path() const
    {
        return _path;
    }
    [[nodiscard]] bool Dirty() const
    {
        return !_undo.isClean();
    }
    [[nodiscard]] QUndoStack* UndoStack()
    {
        return &_undo;
    }
    [[nodiscard]] const sdb::Model& GetModel() const;
    [[nodiscard]] const std::vector<sdb::scn::Issue>& Issues() const
    {
        return _issues;
    }
    [[nodiscard]] bool Valid() const;
    // The tree of the document even when it has issues (null only when it is structurally broken).
    [[nodiscard]] const std::optional<sdb::scn::Scenario>& Loose() const
    {
        return _loose;
    }

    void NewDocument(const sdb::Model& m, const QString& name);
    bool Open(const QString& path, QString* error);
    bool Save(const QString& path, QString* error);            // empty = the current path
    bool ExportTo(const QString& path, QString* error) const;  // a copy in another format, the document keeps its path

    // One undoable change. `op` edits the JSON; false (with `error`) = nothing changes.
    bool Apply(const QString& text, const std::function<sdb::scn::ops::Result(json&)>& op, QString* error = nullptr,
               std::string* id = nullptr);
    bool Replace(const json& doc, const QString& text = QStringLiteral("Replace document"));

    // Selection and the menu on the canvas
    [[nodiscard]] const QString& Selected() const
    {
        return _selected;
    }
    [[nodiscard]] const QString& CanvasMenu() const
    {
        return _canvasMenu;
    }
    void Select(const QString& id, bool showItsMenu = true);  // showItsMenu: the canvas shows the menu holding it
    bool Enter(const QString& menuId);                        // show this menu on the canvas
    bool Up();                                                // the canvas shows the parent menu

    // Called by the undo commands
    void SetDoc(const json& d);

signals:
    void Changed();  // document content
    void SelectionChanged();
    void CanvasChanged();  // which menu the canvas shows
    void PathChanged();

private:
    void Revalidate();
    void FixCanvas();

    json _doc, _resolved;
    QString _path, _selected, _canvasMenu = QStringLiteral("home");
    QUndoStack _undo;
    std::vector<sdb::scn::Issue> _issues;
    std::optional<sdb::scn::Scenario> _loose;
};
