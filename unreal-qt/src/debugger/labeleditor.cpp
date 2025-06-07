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
    "All Supported Files (*.map *.sym *.vice);;"
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
        {tr("Label"), tr("Address"), tr("Bank"), tr("Bank Offset"), tr("RAM/ROM"), tr("Type"), tr("Comment")});
    _labelTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _labelTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _labelTable->setSortingEnabled(true);
    _labelTable->setAlternatingRowColors(true);
    _labelTable->setContextMenuPolicy(Qt::CustomContextMenu);
    _labelTable->setEditTriggers(QAbstractItemView::NoEditTriggers);

    // Set column widths
    _labelTable->setColumnWidth(0, 150);                           // Name
    _labelTable->setColumnWidth(1, 70);                            // Address
    _labelTable->setColumnWidth(2, 50);                            // Bank
    _labelTable->setColumnWidth(3, 100);                           // Bank Offset
    _labelTable->setColumnWidth(4, 80);                            // Bank type (RAM / ROM)
    _labelTable->setColumnWidth(5, 64);                            // Label type (code, data, const)
    _labelTable->horizontalHeader()->setStretchLastSection(true);  // Comment

    // Create buttons
    _addButton = new QPushButton(tr("&Add"), this);
    _editButton = new QPushButton(tr("&Edit"), this);
    _deleteButton = new QPushButton(tr("&Delete"), this);
    _closeButton = new QPushButton(tr("&Close"), this);

    // Create label for total count
    _totalLabelsLabel = new QLabel(this);
    updateTotalLabelsCount(0);

    // Button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addWidget(_addButton);
    buttonLayout->addWidget(_editButton);
    buttonLayout->addWidget(_deleteButton);
    buttonLayout->addStretch();
    buttonLayout->addWidget(_totalLabelsLabel);
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

void LabelEditor::updateTotalLabelsCount(int count)
{
    _totalLabelsLabel->setText(QString("Total: %1").arg(count));
}

void LabelEditor::populateLabelTable()
{
    if (!_labelManager || !_labelTable)
    {
        return;
    }

    try
    {
        // Disable sorting while updating to prevent visual glitches
        _labelTable->setSortingEnabled(false);
        _labelTable->setRowCount(0);
        updateTotalLabelsCount(0);

        // Get all labels from the manager
        auto labels = _labelManager->GetAllLabels();
        _labelTable->setRowCount(static_cast<int>(labels.size()));

        int row = 0;
        for (const auto& label : labels)
        {
            if (!label)
                continue;

            // Name
            _labelTable->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(label->name)));

            // Address
            _labelTable->setItem(
                row, 1, new QTableWidgetItem(QString("0x%1").arg(label->address, 4, 16, QChar('0')).toUpper()));

            // Bank
            _labelTable->setItem(row, 2,
                                 new QTableWidgetItem(label->bank == UINT16_MAX ? "*" : QString::number(label->bank)));

            // Bank Offset
            _labelTable->setItem(
                row, 3,
                new QTableWidgetItem(label->bankOffset == UINT16_MAX
                                         ? "*"
                                         : QString("0x%1").arg(label->bankOffset, 4, 16, QChar('0')).toUpper()));

            // Bank Type (ROM/RAM)
            QTableWidgetItem* bankTypeItem = new QTableWidgetItem(label->isROM() ? "ROM" : "RAM");
            bankTypeItem->setTextAlignment(Qt::AlignCenter);
            _labelTable->setItem(row, 4, bankTypeItem);

            // Type
            QTableWidgetItem* typeItem = new QTableWidgetItem(QString::fromStdString(label->type));
            typeItem->setTextAlignment(Qt::AlignCenter);
            _labelTable->setItem(row, 5, typeItem);

            // Module
            QTableWidgetItem* moduleItem = new QTableWidgetItem(QString::fromStdString(label->module));
            moduleItem->setTextAlignment(Qt::AlignCenter);
            _labelTable->setItem(row, 6, moduleItem);

            // Comment
            _labelTable->setItem(row, 7, new QTableWidgetItem(QString::fromStdString(label->comment)));

            // Active state
            QTableWidgetItem* activeItem = new QTableWidgetItem();
            activeItem->setCheckState(label->active ? Qt::Checked : Qt::Unchecked);
            _labelTable->setItem(row, 8, activeItem);

            // Store the label name in the first column's item for later reference
            if (QTableWidgetItem* nameItem = _labelTable->item(row, 0))
            {
                nameItem->setData(Qt::UserRole, QString::fromStdString(label->name));
            }

            row++;
        }

        // Re-enable sorting
        _labelTable->setSortingEnabled(true);
        updateTotalLabelsCount(_labelTable->rowCount());
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
    std::shared_ptr<Label> labelToDelete = getLabelFromRow(row);

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
                QString errorMsg =
                    tr("A label with the name '%1' already exists").arg(QString::fromStdString(newLabel.name));
                _statusBar->showMessage(errorMsg, 3000);
                QMessageBox::warning(this, tr("Add Label Failed"), errorMsg);
                return;
            }

            // Create a new label with default values
            newLabel.name = "NEW_LABEL";
            newLabel.address = 0x0000;
            newLabel.bank = UINT16_MAX;        // Any bank by default
            newLabel.bankOffset = UINT16_MAX;  // Any bank offset by default
            newLabel.type = "code";
            newLabel.module = "";
            newLabel.comment = "";
            newLabel.active = true;

            // Add the new label
            _labelManager->AddLabel(newLabel.name, newLabel.address, newLabel.bank, newLabel.bankOffset, newLabel.type,
                                    newLabel.module, newLabel.comment, newLabel.active);

            refreshLabelList();
            updateTotalLabelsCount(_labelTable->rowCount());
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
    std::shared_ptr<Label> originalLabel = getLabelFromRow(row);
    if (!originalLabel)
    {
        _statusBar->showMessage(tr("Error: Could not find the selected label"), 3000);
        return;
    }

    try
    {
        // Create a copy of the label to edit
        Label editedLabel = *originalLabel;

        // Ensure bank and bankOffset are properly initialized if they were not set before
        if (editedLabel.bank == 0xFFFF)
            editedLabel.bank = UINT16_MAX;
        if (editedLabel.bankOffset == 0xFFFF)
            editedLabel.bankOffset = UINT16_MAX;

        // Create dialog in Edit mode with the label copy
        LabelDialog dialog(editedLabel, _labelManager, this);

        if (dialog.exec() == QDialog::Accepted)
        {
            // Get the edited label from the dialog
            Label updatedLabel = dialog.getLabel();

            // Check if name was changed and if the new name conflicts with an existing label
            if (originalLabel->name != updatedLabel.name && _labelManager->GetLabelByName(updatedLabel.name))
            {
                QString errorMsg =
                    tr("A label with the name '%1' already exists").arg(QString::fromStdString(updatedLabel.name));
                _statusBar->showMessage(errorMsg, 3000);
                QMessageBox::warning(this, tr("Edit Label Failed"), errorMsg);
                return;
            }

            // Update the label in the label manager
            if (_labelManager->UpdateLabel(updatedLabel))
            {
                // Refresh the label list to show changes
                refreshLabelList();
                _statusBar->showMessage(tr("Label updated successfully"), 3000);
            }
            else
            {
                _statusBar->showMessage(tr("Failed to update label"), 3000);
                QMessageBox::warning(this, tr("Edit Label Failed"),
                                     tr("Failed to update the label in the label manager."));
            }
        }
    }
    catch (const std::exception& e)
    {
        QString errorMsg = tr("Error editing label: %1").arg(e.what());
        _statusBar->showMessage(errorMsg, 3000);
        QMessageBox::critical(this, tr("Error"), errorMsg);
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

std::shared_ptr<Label> LabelEditor::getLabelFromRow(int row) const
{
    if (row < 0 || row >= _labelTable->rowCount() || !_labelManager)
        return nullptr;

    // First try to get the label from the item's user data (most reliable)
    QTableWidgetItem* item = _labelTable->item(row, 0);  // Get the name column item
    if (item)
    {
        QVariant data = item->data(Qt::UserRole);
        if (data.isValid() && !data.isNull())
        {
            // The user data should be stored as a pointer to the label's name
            std::string labelName = data.toString().toStdString();
            if (!labelName.empty())
            {
                return _labelManager->GetLabelByName(labelName);
            }
        }
    }

    // If user data is not available or invalid, try to find by name and address
    QString name = _labelTable->item(row, 0)->text();
    if (name.isEmpty())
        return nullptr;

    // Find the label in the label manager
    std::shared_ptr<Label> label = _labelManager->GetLabelByName(name.toStdString());
    if (label)
        return label;

    // If not found by name, try by address
    if (!_labelTable->item(row, 1))
        return nullptr;

    // Try to find by address if not found by name
    QString addressStr = _labelTable->item(row, 1)->text();
    bool ok;
    uint16_t address = addressStr.toUShort(&ok, 16);
    if (!ok)
        return nullptr;

    // Get bank if available
    bool hasBank = false;
    uint8_t bank = 0;
    if (_labelTable->item(row, 2) && _labelTable->item(row, 2)->text() != "*" &&
        _labelTable->item(row, 2)->text() != "N/A")
    {
        bank = static_cast<uint8_t>(_labelTable->item(row, 2)->text().toUShort(&ok));
        hasBank = ok;
    }

    // Try to find by Z80 address first (most common case)
    label = _labelManager->GetLabelByZ80Address(address);
    if (!label)
        return nullptr;

    // If we have a bank and the found label doesn't match, try to find a better match
    if (hasBank && label->bank != bank)
    {
        // No direct bank-specific lookup available, so we need to scan all labels
        auto allLabels = _labelManager->GetAllLabels();
        for (const auto& l : allLabels)
        {
            if (l->address == address && l->bank == bank)
            {
                return l;
            }
        }
    }

    return label;
}
