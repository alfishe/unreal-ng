#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QHeaderView>
#include <QMenu>
#include <QShortcut>
#include <QAction>
#include <QFileDialog>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QApplication>
#include <QToolBar>
#include <QStatusBar>
#include <QMenuBar>
#include <QToolButton>
#include <string>
#include <cstdint>
#include <vector>
#include "debugger/labels/labelmanager.h"

// Forward declarations
class LabelManager;
class QLineEdit;
class QComboBox;
class QTableWidgetItem;

namespace Ui
{
    class LabelEditor;
}

class LabelEditor : public QDialog
{
    Q_OBJECT

public:
    explicit LabelEditor(LabelManager* labelManager, QWidget* parent = nullptr);
    explicit LabelEditor(Label& label, LabelManager* labelManager, QWidget* parent = nullptr);
    ~LabelEditor();

    const Label& getLabel() const { return _currentLabel; }

private slots:
    // Label actions
    void addLabel();
    void editLabel();
    void deleteLabel();
    void loadLabels();
    void saveLabels();
    void saveAsLabels();
    void updateRecentFilesMenu();
    void loadFromFile(const QString& filePath);
    bool saveToFile(const QString& filePath);
    QString getSaveFileNameWithFormat(QString* selectedFormat = nullptr);
    void openRecentFile();

    // Table events
    void onLabelSelectionChanged();
    void onLabelDoubleClicked(QTableWidgetItem* item);
    void showContextMenu(const QPoint& pos);

    // Dialog updates
    void refreshLabelList();
    // void applyFilters(); // Future: if search/filter is added
    // void clearFilters(); // Future: if search/filter is added

private:
    // Settings management
    void loadSettings();
    void saveSettings();
    
    // UI setup
    void setupUI();
    void setupTableContextMenu();
    void setupShortcuts();
    void populateLabelTable();
    void updateButtonStates();
    
    // Recent files management
    void addToRecentFiles(const QString& filePath);
    void openRecentFile(QAction* action);
    
    // Helper to get a label from a table row
    std::shared_ptr<Label> getLabelFromRow(int row) const;

    LabelManager* _labelManager = nullptr;

    Ui::LabelEditor *ui; // For use with .ui file if we create one

    // UI Elements (if not using a .ui file, declare them here)
    QTableWidget* _labelTable = nullptr;
    QPushButton* _addButton = nullptr;
    QPushButton* _editButton = nullptr;
    QPushButton* _deleteButton = nullptr;
    QPushButton* _closeButton = nullptr;

    // Future: Filter UI elements
    // QLineEdit* _searchField = nullptr;
    // QComboBox* _typeFilter = nullptr;
    // QComboBox* _bankFilter = nullptr;

    // File menu actions
    QAction* _loadAction = nullptr;
    QAction* _saveAction = nullptr;
    QAction* _saveAsAction = nullptr;
    QMenu* _recentFilesMenu = nullptr;
    QAction* _exitAction = nullptr;
    
    // UI Elements
    QMenuBar* _menuBar = nullptr;
    QToolBar* _toolBar = nullptr;
    QStatusBar* _statusBar = nullptr;
    
    // Recent files
    std::vector<QString> _recentFiles;
    static const int MAX_RECENT_FILES = 10;
    
    // Current file path
    QString _currentFilePath;
    
    // Current label being edited
    Label _currentLabel;
};
