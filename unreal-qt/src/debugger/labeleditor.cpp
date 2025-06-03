#include "labeleditor.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QSettings>
#include <QStandardPaths>
#include <QVBoxLayout>

#include "debugger/labels/labelmanager.h"
#include "labeldialog.h"

// File format filters
static const QString LABEL_FILTERS =
    "All Supported Files (*.map *.sym *.vice *.sna *.sna.z80 *.z80 *.sna.zxst *.zxst);;"
    "Linker Map Files (*.map);;"
    "Symbol Files (*.sym);;"
    "VICE Symbol Files (*.vice);;"
    "All Files (*.*)";

// Settings keys
static const char* SETTINGS_GROUP = "LabelEditor";
static const char* SETTINGS_RECENT_FILES = "RecentFiles";
static const char* SETTINGS_GEOMETRY = "Geometry";
static const char* SETTINGS_STATE = "State";

LabelEditor::LabelEditor(LabelManager* labelManager, QWidget* parent)
    : QDialog(parent), _labelManager(labelManager), ui(nullptr), _currentLabel()
{
    // Setup UI
    setupUI();

    // Load settings
    loadSettings();

    // Update UI state
    updateButtonStates();
    updateRecentFilesMenu();

    // Initial table population
    populateLabelTable();
}

LabelEditor::~LabelEditor()
{
    // Save settings
    saveSettings();
}

void LabelEditor::loadSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    // Restore window geometry and state
    QByteArray geometry = settings.value(SETTINGS_GEOMETRY).toByteArray();
    if (!geometry.isEmpty())
    {
        restoreGeometry(geometry);
    }

    // Load recent files
    QStringList recentFilesList = settings.value(SETTINGS_RECENT_FILES).toStringList();
    _recentFiles.clear();
    for (const QString& file : recentFilesList)
    {
        _recentFiles.push_back(file);
    }

    settings.endGroup();
}

void LabelEditor::saveSettings()
{
    QSettings settings;
    settings.beginGroup(SETTINGS_GROUP);

    // Save window geometry
    settings.setValue(SETTINGS_GEOMETRY, saveGeometry());
    // Note: QDialog doesn't have saveState method by default
    // We're only saving geometry for now

    // Save recent files
    QStringList recentFilesList;
    for (const auto& file : _recentFiles)
    {
        recentFilesList << file;
    }
    settings.setValue(SETTINGS_RECENT_FILES, recentFilesList);

    settings.endGroup();
}

void LabelEditor::setupUI()
{
    // Create main layout
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(2, 2, 2, 2);
    mainLayout->setSpacing(2);

    // Create menu bar
    _menuBar = new QMenuBar(this);
    mainLayout->setMenuBar(_menuBar);

    // File menu
    QMenu* fileMenu = _menuBar->addMenu(tr("&File"));

    _loadAction = fileMenu->addAction(tr("&Load..."), this, &LabelEditor::loadLabels);
    _loadAction->setShortcut(QKeySequence::Open);

    _saveAction = fileMenu->addAction(tr("&Save"), this, &LabelEditor::saveLabels);
    _saveAction->setShortcut(QKeySequence::Save);
    _saveAction->setEnabled(false);

    _saveAsAction = fileMenu->addAction(tr("Save &As..."), this, &LabelEditor::saveAsLabels);
    _saveAsAction->setShortcut(QKeySequence::SaveAs);

    fileMenu->addSeparator();

    // Recent files submenu
    _recentFilesMenu = fileMenu->addMenu(tr("Recent Files"));
    updateRecentFilesMenu();

    fileMenu->addSeparator();

    _exitAction = fileMenu->addAction(tr("E&xit"), this, &QDialog::close);
    _exitAction->setShortcut(tr("Ctrl+Q"));

    // Create toolbar
    _toolBar = new QToolBar(tr("Toolbar"), this);
    _toolBar->setIconSize(QSize(16, 16));
    mainLayout->addWidget(_toolBar);

    // Add actions to toolbar
    _toolBar->addAction(_loadAction);
    _toolBar->addAction(_saveAction);
    _toolBar->addAction(_saveAsAction);
    _toolBar->addSeparator();

    // Create status bar
    _statusBar = new QStatusBar(this);

    // Create table widget
    _labelTable = new QTableWidget(this);
    _labelTable->setColumnCount(7);
    _labelTable->setHorizontalHeaderLabels(
        {tr("Name"), tr("Address"), tr("Bank"), tr("Bank Offset"), tr("Type"), tr("Module"), tr("Comment")});
    _labelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _labelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _labelTable->setSortingEnabled(true);
    _labelTable->setAlternatingRowColors(true);
    _labelTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // Set column widths
    _labelTable->setColumnWidth(0, 150);                           // Name
    _labelTable->setColumnWidth(1, 70);                            // Address
    _labelTable->setColumnWidth(2, 50);                            // Bank
    _labelTable->setColumnWidth(3, 70);                            // Bank Offset
    _labelTable->setColumnWidth(4, 80);                            // Type
    _labelTable->setColumnWidth(5, 100);                           // Module
    _labelTable->horizontalHeader()->setStretchLastSection(true);  // Comment

    // Create buttons
    _addButton = new QPushButton(tr("&Add"), this);
    _editButton = new QPushButton(tr("&Edit"), this);
    _deleteButton = new QPushButton(tr("&Delete"), this);
    _closeButton = new QPushButton(tr("&Close"), this);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(_addButton);
    buttonLayout->addWidget(_editButton);
    buttonLayout->addWidget(_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(_closeButton);

    // Assemble main layout
    mainLayout->setMenuBar(_menuBar);
    mainLayout->addWidget(_toolBar);
    mainLayout->addWidget(_labelTable);
    mainLayout->addLayout(buttonLayout);
    mainLayout->addWidget(_statusBar);

    // Connect signals and slots
    connect(_addButton, &QPushButton::clicked, this, &LabelEditor::addLabel);
    connect(_editButton, &QPushButton::clicked, this, &LabelEditor::editLabel);
    connect(_deleteButton, &QPushButton::clicked, this, &LabelEditor::deleteLabel);
    connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);

    connect(_labelTable, &QTableWidget::itemSelectionChanged, this, &LabelEditor::onLabelSelectionChanged);
    connect(_labelTable, &QTableWidget::itemDoubleClicked, this, &LabelEditor::onLabelDoubleClicked);
    connect(_labelTable, &QTableWidget::customContextMenuRequested, this, &LabelEditor::showContextMenu);

    // Connect recent files menu
    connect(_recentFilesMenu, &QMenu::aboutToShow, this, &LabelEditor::updateRecentFilesMenu);
    connect(_recentFilesMenu, &QMenu::triggered, this, [this](QAction* action) { openRecentFile(action); });

    // Setup context menu and shortcuts
    setupTableContextMenu();
    setupShortcuts();

    // Update button states
    updateButtonStates();

    // Add exit action
    _exitAction = fileMenu->addAction(tr("E&xit"), this, &QDialog::reject);
    _exitAction->setShortcut(QKeySequence::Quit);

    // Update window properties
    setWindowTitle(tr("Label Manager"));
    resize(900, 600);
}

LabelEditor::LabelEditor(Label& label, LabelManager* labelManager, QWidget* parent)
    : QDialog(parent), _labelManager(labelManager), ui(nullptr), _currentLabel(label)
{
    // Setup UI
    setupUI();

    // Load settings
    loadSettings();

    // Update UI state
    updateButtonStates();
    updateRecentFilesMenu();

    // Setup context menu and shortcuts
    setupTableContextMenu();
    setupShortcuts();

    // Initial table population
    populateLabelTable();

    // Set window title
    setWindowTitle(tr("Edit Label"));
}

void LabelEditor::loadLabels()
{
    QString filePath = QFileDialog::getOpenFileName(
        this, tr("Load Labels"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation), LABEL_FILTERS);

    if (!filePath.isEmpty())
    {
        loadFromFile(filePath);
    }
}

void LabelEditor::saveLabels()
{
    if (_currentFilePath.isEmpty())
    {
        saveAsLabels();
    }
    else
    {
        saveToFile(_currentFilePath);
    }
}

void LabelEditor::saveAsLabels()
{
    QString filePath = getSaveFileNameWithFormat();
    if (!filePath.isEmpty())
    {
        if (saveToFile(filePath))
        {
            _currentFilePath = filePath;
            _saveAction->setEnabled(true);
            addToRecentFiles(filePath);
            _statusBar->showMessage(tr("Labels saved to %1").arg(filePath), 3000);
        }
    }
}

void LabelEditor::loadFromFile(const QString& filePath)
{
    if (!_labelManager)
    {
        QString errorMsg = tr("Error: Label manager is not initialized.");
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
        return;
    }

    try
    {
        // Clear existing labels first
        _labelManager->ClearAllLabels();

        // Load new labels using LabelManager
        bool success = _labelManager->LoadLabels(filePath.toStdString());
        if (success)
        {
            _currentFilePath = filePath;
            _saveAction->setEnabled(true);
            addToRecentFiles(filePath);
            populateLabelTable();

            QString successMsg = tr("Successfully loaded labels from %1").arg(QFileInfo(filePath).fileName());
            _statusBar->showMessage(successMsg, 3000);
        }
        else
        {
            QString errorMsg = tr("Failed to load labels from %1").arg(filePath);
            _statusBar->showMessage(errorMsg, 3000);
            QMessageBox::warning(this, tr("Load Error"), errorMsg);
        }
    }
    catch (const std::exception& e)
    {
        QString errorMsg = tr("Error loading labels: %1").arg(e.what());
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
    }
}

bool LabelEditor::saveToFile(const QString& filePath)
{
    if (!_labelManager)
    {
        QString errorMsg = tr("Error: Label manager is not initialized.");
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
        return false;
    }

    try
    {
        // Use LabelManager's format detection and saving
        bool success = _labelManager->SaveLabels(filePath.toStdString());

        if (success)
        {
            _currentFilePath = filePath;
            _saveAction->setEnabled(true);
            addToRecentFiles(filePath);

            QString successMsg = tr("Successfully saved labels to %1").arg(QFileInfo(filePath).fileName());
            _statusBar->showMessage(successMsg, 3000);
            return true;
        }
        else
        {
            QString errorMsg = tr("Failed to save labels to %1").arg(filePath);
            _statusBar->showMessage(errorMsg, 3000);
            QMessageBox::warning(this, tr("Save Error"), errorMsg);
            return false;
        }
    }
    catch (const std::exception& e)
    {
        QString errorMsg = tr("Error saving labels: %1").arg(e.what());
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
        return false;
    }
}

QString LabelEditor::getSaveFileNameWithFormat(QString* selectedFormat)
{
    QString selectedFilter;
    QString filePath = QFileDialog::getSaveFileName(
        this, tr("Save Labels"),
        _currentFilePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
                                   : _currentFilePath,
        LABEL_FILTERS, &selectedFilter);

    // Let LabelManager handle the format detection based on file extension
    return filePath;
}

void LabelEditor::addToRecentFiles(const QString& filePath)
{
    // Remove if already in the list
    _recentFiles.erase(std::remove(_recentFiles.begin(), _recentFiles.end(), filePath), _recentFiles.end());

    // Add to the beginning
    _recentFiles.insert(_recentFiles.begin(), filePath);

    // Limit the number of recent files
    if (_recentFiles.size() > MAX_RECENT_FILES)
    {
        _recentFiles.resize(MAX_RECENT_FILES);
    }

    // Update the menu
    updateRecentFilesMenu();
}

void LabelEditor::updateRecentFilesMenu()
{
    _recentFilesMenu->clear();

    for (size_t i = 0; i < _recentFiles.size(); ++i)
    {
        QString text = QString("&%1 %2").arg(i + 1).arg(QFileInfo(_recentFiles[i]).fileName());
        QAction* action = _recentFilesMenu->addAction(text);
        action->setData(_recentFiles[i]);
    }

    _recentFilesMenu->setEnabled(!_recentFiles.empty());
}

void LabelEditor::openRecentFile(QAction* action)
{
    QString filePath = action->data().toString();
    if (!filePath.isEmpty())
    {
        loadFromFile(filePath);
    }
}

void LabelEditor::populateLabelTable()
{
    if (!_labelManager || !_labelTable)
    {
        return;
    }

    // Disable sorting while updating to prevent visual glitches
    _labelTable->setSortingEnabled(false);

    // Clear existing items
    _labelTable->setRowCount(0);

    try
    {
        // Get all labels from the manager
        auto labels = _labelManager->GetAllLabels();

        // Populate the table
        for (const auto& label : labels)
        {
            if (!label)
                continue;

            int row = _labelTable->rowCount();
            _labelTable->insertRow(row);

            // Add label data to columns
            _labelTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(label->name)));
            _labelTable->setItem(
                row, 1, new QTableWidgetItem(QString("0x%1").arg(label->address, 4, 16, QChar('0')).toUpper()));

            _labelTable->setItem(row, 2,
                                 new QTableWidgetItem(label->bank == 0xFF ? "N/A" : QString::number(label->bank)));

            _labelTable->setItem(
                row, 3,
                new QTableWidgetItem(label->bankAddress == 0xFFFF
                                         ? "N/A"
                                         : QString("0x%1").arg(label->bankAddress, 4, 16, QChar('0')).toUpper()));

            _labelTable->setItem(row, 4, new QTableWidgetItem(QString::fromStdString(label->type)));
            _labelTable->setItem(row, 5, new QTableWidgetItem(QString::fromStdString(label->module)));
            _labelTable->setItem(row, 6, new QTableWidgetItem(QString::fromStdString(label->comment)));
            
            // Store the label pointer in the first column's item for later reference
            if (QTableWidgetItem* nameItem = _labelTable->item(row, 0))
            {
                nameItem->setData(Qt::UserRole, QVariant::fromValue((void*)label.get()));
            }
        }
        
        // Re-enable sorting if needed
        _labelTable->setSortingEnabled(true);
    }
    catch (const std::exception& e)
    {
        QString errorMsg = tr("Error getting labels: %1").arg(e.what());
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
    }
}

void LabelEditor::deleteLabel()
{
    if (!_labelManager)
    {
        _statusBar->showMessage(tr("Error: Label manager is not initialized"), 3000);
        return;
    }

    QList<QTableWidgetItem*> selectedItems = _labelTable->selectedItems();
    if (selectedItems.isEmpty())
    {
        _statusBar->showMessage(tr("No label selected"), 2000);
        return;
    }

    int row = selectedItems.first()->row();
    Label* labelToDelete = getLabelFromRow(row);

    if (labelToDelete)
    {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(
            this, tr("Delete Label"),
            tr("Are you sure you want to delete label '%1'?").arg(QString::fromStdString(labelToDelete->name)),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::Yes)
        {
            try
            {
                std::string labelName = labelToDelete->name;  // Save name before deletion
                _labelManager->RemoveLabel(labelName);
                refreshLabelList();
                _statusBar->showMessage(tr("Deleted label: %1").arg(QString::fromStdString(labelName)), 3000);
            }
            catch (const std::exception& e)
            {
                QString errorMsg = tr("Error deleting label: %1").arg(e.what());
                _statusBar->showMessage(errorMsg, 3000);
                QMessageBox::critical(this, tr("Error"), errorMsg);
            }
        }
    }
}

void LabelEditor::onLabelSelectionChanged()
{
    updateButtonStates();
}

void LabelEditor::onLabelDoubleClicked(QTableWidgetItem* item)
{
    if (item)
    {
        editLabel();
    }
}

void LabelEditor::addLabel()
{
    if (!_labelManager)
    {
        _statusBar->showMessage(tr("Error: Label manager is not initialized"), 3000);
        return;
    }

    try
    {
        // Create dialog in Add mode
        LabelDialog dialog(_labelManager, this);
        
        if (dialog.exec() == QDialog::Accepted)
        {
            // Get the new label from the dialog
            Label newLabel = dialog.getLabel();
            
            // Check if a label with this name already exists
            if (_labelManager->GetLabelByName(newLabel.name))
            {
                QString errorMsg = tr("A label with the name '%1' already exists")
                                    .arg(QString::fromStdString(newLabel.name));
                _statusBar->showMessage(errorMsg, 3000);
                QMessageBox::warning(this, tr("Add Label Failed"), errorMsg);
                return;
            }

            // Add the new label
            _labelManager->AddLabel(newLabel.name, newLabel.address, newLabel.bank,
                                  newLabel.bankAddress, newLabel.type, newLabel.module,
                                  newLabel.comment, newLabel.active);
            
            refreshLabelList();
            _statusBar->showMessage(tr("Added new label: %1").arg(QString::fromStdString(newLabel.name)), 3000);
        }
    }
    catch (const std::exception& e)
    {
        QString errorMsg = tr("Error adding label: %1").arg(e.what());
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
    }
}

void LabelEditor::editLabel()
{
    if (!_labelManager)
    {
        _statusBar->showMessage(tr("Error: Label manager is not initialized"), 3000);
        return;
    }

    QList<QTableWidgetItem*> selectedItems = _labelTable->selectedItems();
    if (selectedItems.isEmpty())
    {
        _statusBar->showMessage(tr("No label selected"), 2000);
        return;
    }

    int row = selectedItems.first()->row();
    if (Label* originalLabel = getLabelFromRow(row))
    {
        try
        {
            // Create dialog in Edit mode with the original label
            Label editedLabel = *originalLabel;  // Create a copy for editing
            LabelDialog dialog(editedLabel, _labelManager, this);
            
            if (dialog.exec() == QDialog::Accepted)
            {
                // Get the edited label from the dialog
                editedLabel = dialog.getLabel();
                
                // Check if name was changed and if the new name conflicts with an existing label
                if (originalLabel->name != editedLabel.name && 
                    _labelManager->GetLabelByName(editedLabel.name))
                {
                    QString errorMsg = tr("A label with the name '%1' already exists")
                                        .arg(QString::fromStdString(editedLabel.name));
                    _statusBar->showMessage(errorMsg, 3000);
                    QMessageBox::warning(this, tr("Edit Label Failed"), errorMsg);
                    return;
                }

                // Remove old label and add the edited one
                _labelManager->RemoveLabel(originalLabel->name);
                _labelManager->AddLabel(editedLabel.name, editedLabel.address, editedLabel.bank,
                                      editedLabel.bankAddress, editedLabel.type, editedLabel.module,
                                      editedLabel.comment, editedLabel.active);
                
                refreshLabelList();
                _statusBar->showMessage(tr("Updated label: %1").arg(QString::fromStdString(editedLabel.name)), 3000);
            }
        }
        catch (const std::exception& e)
        {
            QString errorMsg = tr("Error editing label: %1").arg(e.what());
            _statusBar->showMessage(errorMsg, 3000);
            QMessageBox::critical(this, tr("Error"), errorMsg);
        }
    }
}

void LabelEditor::refreshLabelList()
{
    if (_labelTable)
    {
        _labelTable->clearContents();
        _labelTable->setRowCount(0);
        populateLabelTable();
    }
}

void LabelEditor::openRecentFile()
{
    QAction* action = qobject_cast<QAction*>(sender());
    if (action)
    {
        QString filePath = action->data().toString();
        if (!filePath.isEmpty())
        {
            loadFromFile(filePath);
        }
    }
}

void LabelEditor::showContextMenu(const QPoint& pos)
{
    QMenu menu(this);

    QAction* addAction = menu.addAction(tr("Add Label..."));
    QAction* editAction = menu.addAction(tr("Edit Label..."));
    QAction* deleteAction = menu.addAction(tr("Delete Label"));

    // Enable/disable actions based on selection
    bool hasSelection = !_labelTable->selectedItems().isEmpty();
    editAction->setEnabled(hasSelection);
    deleteAction->setEnabled(hasSelection);

    // Show context menu at cursor position
    QPoint globalPos = _labelTable->viewport()->mapToGlobal(pos);
    QAction* selectedAction = menu.exec(globalPos);

    if (!selectedAction)
    {
        return;
    }

    if (selectedAction == addAction)
    {
        addLabel();
    }
    else if (selectedAction == editAction)
    {
        editLabel();
    }
    else if (selectedAction == deleteAction)
    {
        deleteLabel();
    }
}

void LabelEditor::updateButtonStates()
{
    bool hasSelection = !_labelTable->selectedItems().isEmpty();
    _editButton->setEnabled(hasSelection);
    _deleteButton->setEnabled(hasSelection);
    _saveAction->setEnabled(!_currentFilePath.isEmpty());
}

void LabelEditor::setupTableContextMenu()
{
    _labelTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_labelTable, &QTableWidget::customContextMenuRequested, this, &LabelEditor::showContextMenu);
}

void LabelEditor::setupShortcuts()
{
    // Shortcut for deleting selected label
    QShortcut* deleteShortcut = new QShortcut(QKeySequence::Delete, this);
    connect(deleteShortcut, &QShortcut::activated, this, &LabelEditor::deleteLabel);
}

Label* LabelEditor::getLabelFromRow(int row) const
{
    if (row < 0 || row >= _labelTable->rowCount())
        return nullptr;
        
    QTableWidgetItem* item = _labelTable->item(row, 0);  // Get the name column item
    if (!item)
        return nullptr;
        
    // Get the label pointer from the item's data
    QVariant data = item->data(Qt::UserRole);
    if (!data.isValid() || data.isNull())
        return nullptr;
        
    // Convert the void* back to Label*
    return static_cast<Label*>(data.value<void*>());
}
