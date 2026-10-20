// The scenario editor: toolbar, tree, palette, canvas with breadcrumbs, inspector, issues / runtime output.
// Hosted by the studio in a dock: docked beside the live deck (one window) or floating (its own window).
#pragma once
#include <QStringList>
#include <QWidget>
#include <optional>
#include <string>

#include "sdb/scenario.h"

class DesignCanvas;
class EditorDocument;
class Inspector;
class QHBoxLayout;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QTabWidget;
class QTreeWidget;
class QAction;
class RunController;

class EditorWidget : public QWidget
{
    Q_OBJECT
public:
    explicit EditorWidget(QWidget* parent = nullptr);

    [[nodiscard]] EditorDocument* Document() const
    {
        return _doc;
    }
    [[nodiscard]] DesignCanvas* Canvas() const
    {
        return _canvas;
    }
    [[nodiscard]] RunController* Runner() const
    {
        return _run;
    }

    // UI commands (also used by the automation routes)
    bool OpenFile(const QString& path, QString* error = nullptr);
    bool SaveFile(const QString& path, QString* error = nullptr);  // empty = current file (asks when untitled, UI only)
    bool RunOn(const QString& serial = {}, QString* error = nullptr);  // the daemon plays it (RunController)
    void StopRun();
    void CopySelected();
    bool Paste(QString* error = nullptr);
    bool DeleteSelected(QString* error = nullptr);
    // subtrees between files
    bool ExportSubtree(const QString& id, const QString& path, bool link, QString* error = nullptr,
                       QStringList* warnings = nullptr);
    bool ImportFile(const QString& path, const QString& parent, std::optional<sdb::scn::Slot> at, const QString& mode,
                    const QString& node, QString* error = nullptr, std::string* newId = nullptr);
    bool InlineInclude(const QString& id, QString* error = nullptr);
    void ShowMessage(const QString& text);

    // Menu / toolbar commands with their dialogs
    void CmdNew();
    void CmdOpen();
    void CmdSave();
    void CmdSaveAs();
    void CmdExportCopy();
    void CmdImport(const QString& mode);  // menu | items | link, into the canvas menu
    void CmdExportSelected(bool link);
    void CmdDuplicate();
    void CmdInlineSelected();
    void CmdCut();
    void ShowIssues();
    [[nodiscard]] QAction* UndoAction() const
    {
        return _undo;
    }
    [[nodiscard]] QAction* RedoAction() const
    {
        return _redo;
    }

signals:
    void RunStateChanged();

private:
    void RebuildTree();
    void RebuildCrumbs();
    void RebuildIssues();
    void UpdateTitle();

    EditorDocument* _doc;
    DesignCanvas* _canvas;
    Inspector* _inspector;
    RunController* _run;
    QTreeWidget* _tree;
    QTreeWidget* _palette;
    QWidget* _crumbs;
    QHBoxLayout* _crumbsLayout;
    QListWidget* _issues;
    QPlainTextEdit* _output;
    QTabWidget* _bottom;
    QAction *_runAct, *_stopAct, *_undo, *_redo;
    QLabel* _status;
    QString _clipboard;
    bool _syncing = false;
};
