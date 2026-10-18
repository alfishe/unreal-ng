#pragma once

/// @file profilesdialog.h
/// @brief Controllers > Profiles… (docs/inprogress/2026-10-17-controller-manager/ui-ux.md §3.3): every profile
/// (built-in, shipped games, the user's) with its kind, scope and whether it applies now; the selected one as YAML,
/// editable. Save writes the text as a user profile (a shipped id becomes a user profile of the same id, which
/// overrides it), New starts from a template, Import… / Export… read and write a file, Delete removes a user profile
/// (a shipped one of the same id comes back), Use as game profile is the manual choice. Everything goes through the
/// host verbs (HostInputControl), the same path as WebAPI, MCP, CLI and Python.

#include <QDialog>

class HostControllers;
class QComboBox;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTreeWidget;

class ProfilesDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProfilesDialog(HostControllers* host, QWidget* parent = nullptr);

private:
    void reload(const QString& select = {});
    void showProfile(const QString& id);
    void save();
    void remove();
    void importFile();
    void exportFile();
    void newProfile();
    QString selectedId() const;
    void report(bool ok, const QString& text);

    HostControllers* _host = nullptr;
    QComboBox* _set = nullptr;
    QTreeWidget* _list = nullptr;
    QPlainTextEdit* _text = nullptr;
    QLabel* _status = nullptr;
    QPushButton* _delete = nullptr;
    QPushButton* _apply = nullptr;
    QString _shown;  ///< the id whose text is in the editor
};
