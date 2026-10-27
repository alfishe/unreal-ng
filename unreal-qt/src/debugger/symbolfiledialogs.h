#pragma once

#include <map>
#include <string>

#include <QDialog>

class LabelManager;
class QComboBox;
class QLineEdit;
class QListWidget;
struct SymbolReply;

/// The options of a symbol import (SymbolControl "import"): format (auto or a codec), the target set, the space for
/// records without a page, a base, the merge policy
class SymbolImportOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    SymbolImportOptionsDialog(const QString& path, QWidget* parent = nullptr);
    /// The options for SymbolControl, without "path"
    std::map<std::string, std::string> Options() const;

private:
    QComboBox* _format = nullptr;
    QLineEdit* _set = nullptr;
    QLineEdit* _space = nullptr;
    QLineEdit* _base = nullptr;
    QComboBox* _policy = nullptr;
};

/// The options of a symbol export (SymbolControl "export"): format, the sets (none = the labels as they show), what
/// becomes of page symbols in a format without pages
class SymbolExportOptionsDialog : public QDialog
{
    Q_OBJECT

public:
    SymbolExportOptionsDialog(LabelManager* labelManager, const QString& path, QWidget* parent = nullptr);
    std::map<std::string, std::string> Options() const;

private:
    QComboBox* _format = nullptr;
    QListWidget* _sets = nullptr;
    QComboBox* _pages = nullptr;
};

/// An import / export / live import reply as one message box: counts, conflicts, diagnostics (the first few)
void ShowSymbolReport(QWidget* parent, const QString& title, const SymbolReply& reply);
