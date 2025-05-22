#include "breakpointdialog.h"

#include <QApplication>
#include <QComboBox>
#include <QDebug>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMetaMethod>
#include <QMetaObject>
#include <QMutexLocker>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include "3rdparty/message-center/messagecenter.h"
#include "breakpointeditor.h"
#include "breakpointgroupdialog.h"
#include "emulator/emulator.h"

BreakpointDialog::BreakpointDialog(Emulator* emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
{
    setWindowTitle("Breakpoint Manager");
    resize(800, 500);

    setupUI();
    setupFilterUI();
    setupGroupFilter();
    setupTableContextMenu();
    setupShortcuts();

    populateBreakpointTable();
    updateButtonStates();
    updateStatusBar();

    // Subscribe to breakpoint change notifications with thread safety
    m_breakpointCallback = [this](int code, Message* message) {
        // Use mutex to ensure thread-safe access to the dialog
        QMutexLocker locker(&m_mutex);

        assert(this);

        // Check if the dialog is still valid before accessing it
        if (_emulator)
        {
            // Use QMetaObject::invokeMethod to ensure we're on the GUI thread
            QMetaObject::invokeMethod(this, "refreshBreakpointList", Qt::QueuedConnection);
        }
    };

    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.AddObserver(NC_BREAKPOINT_CHANGED, m_breakpointCallback);

    // Restore dialog geometry
    QSettings settings;
    restoreGeometry(settings.value("BreakpointDialog/geometry").toByteArray());

    // Restore column widths
    for (int i = 0; i < _breakpointTable->columnCount(); ++i)
    {
        int width = settings.value(QString("BreakpointDialog/column%1Width").arg(i), -1).toInt();
        if (width > 0)
            _breakpointTable->setColumnWidth(i, width);
    }

    // Restore sort column and order
    int sortColumn = settings.value("BreakpointDialog/sortColumn", 0).toInt();
    Qt::SortOrder sortOrder =
        static_cast<Qt::SortOrder>(settings.value("BreakpointDialog/sortOrder", Qt::AscendingOrder).toInt());
    _breakpointTable->sortByColumn(sortColumn, sortOrder);
}

BreakpointDialog::~BreakpointDialog()
{
    // Unsubscribe from breakpoint change notifications
    QMutexLocker locker(&m_mutex);
    if (_emulator)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.RemoveObserver(NC_BREAKPOINT_CHANGED, m_breakpointCallback);
    }

    // Save dialog geometry
    QSettings settings;
    settings.setValue("BreakpointDialog/geometry", saveGeometry());

    // Save column widths
    for (int i = 0; i < _breakpointTable->columnCount(); ++i)
    {
        settings.setValue(QString("BreakpointDialog/column%1Width").arg(i), _breakpointTable->columnWidth(i));
    }

    // Save sort column and order
    settings.setValue("BreakpointDialog/sortColumn", _breakpointTable->horizontalHeader()->sortIndicatorSection());
    settings.setValue("BreakpointDialog/sortOrder", _breakpointTable->horizontalHeader()->sortIndicatorOrder());
}

void BreakpointDialog::setupUI()
{
    // Create main layout and widgets with proper parenting
    _mainLayout = new QVBoxLayout();
    setLayout(_mainLayout);

    // Create the table widget with proper parenting
    _breakpointTable = new QTableWidget();
    _mainLayout->addWidget(_breakpointTable);
    _breakpointTable->setColumnCount(7);
    _breakpointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _breakpointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _breakpointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _breakpointTable->setAlternatingRowColors(true);

    // Set the headers for the columns
    QStringList headers;
    headers << "ID" << "Type" << "Address" << "Access" << "Status" << "Group" << "Notes";
    _breakpointTable->setHorizontalHeaderLabels(headers);

    // Make table sortable
    _breakpointTable->setSortingEnabled(true);
    _breakpointTable->horizontalHeader()->setSortIndicatorShown(true);
    _breakpointTable->horizontalHeader()->setSectionsClickable(true);

    // Set initial sort order (by ID)
    _breakpointTable->sortByColumn(0, Qt::AscendingOrder);

    // Set column resize modes
    QHeaderView* header = _breakpointTable->horizontalHeader();
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);  // ID
    header->setSectionResizeMode(1, QHeaderView::ResizeToContents);  // Type
    header->setSectionResizeMode(2, QHeaderView::ResizeToContents);  // Address
    header->setSectionResizeMode(3, QHeaderView::ResizeToContents);  // Access
    header->setSectionResizeMode(4, QHeaderView::ResizeToContents);  // Status
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents);  // Group
    header->setSectionResizeMode(6, QHeaderView::Stretch);           // Notes

    // Connect signals
    connect(_breakpointTable, &QTableWidget::itemSelectionChanged, this,
            &BreakpointDialog::onBreakpointSelectionChanged);
    connect(_breakpointTable, &QTableWidget::itemDoubleClicked, this, &BreakpointDialog::onBreakpointDoubleClicked);

    // Add table to layout
    _mainLayout->addWidget(_breakpointTable);

    // Create button layout
    QHBoxLayout* buttonLayout = new QHBoxLayout();

    // Action buttons
    _addButton = new QPushButton("Add");
    _editButton = new QPushButton("Edit");
    _deleteButton = new QPushButton("Delete");
    _enableButton = new QPushButton("Enable");
    _disableButton = new QPushButton("Disable");
    _groupButton = new QPushButton("Group...");

    // Connect button signals
    connect(_addButton, &QPushButton::clicked, this, &BreakpointDialog::addBreakpoint);
    connect(_editButton, &QPushButton::clicked, this, &BreakpointDialog::editBreakpoint);
    connect(_deleteButton, &QPushButton::clicked, this, &BreakpointDialog::deleteBreakpoint);
    connect(_enableButton, &QPushButton::clicked, [this]() {
        toggleBreakpoint();
        _breakpointTable->selectedItems()
            .at(0)
            ->tableWidget()
            ->item(_breakpointTable->selectedItems().at(0)->row(), 4)
            ->setText("Active");
    });
    connect(_disableButton, &QPushButton::clicked, [this]() {
        toggleBreakpoint();
        _breakpointTable->selectedItems()
            .at(0)
            ->tableWidget()
            ->item(_breakpointTable->selectedItems().at(0)->row(), 4)
            ->setText("Inactive");
    });
    connect(_groupButton, &QPushButton::clicked, this, &BreakpointDialog::manageGroups);

    // Add buttons to layout
    buttonLayout->addWidget(_addButton);
    buttonLayout->addWidget(_editButton);
    buttonLayout->addWidget(_deleteButton);
    buttonLayout->addWidget(_enableButton);
    buttonLayout->addWidget(_disableButton);
    buttonLayout->addWidget(_groupButton);
    buttonLayout->addStretch();

    // Add button layout to main layout
    _mainLayout->addLayout(buttonLayout);

    // Status bar
    QHBoxLayout* statusLayout = new QHBoxLayout();
    _statusLabel = new QLabel();
    statusLayout->addWidget(_statusLabel);
    statusLayout->addStretch();

    // Dialog buttons
    _closeButton = new QPushButton("Close");
    _applyButton = new QPushButton("Apply");

    connect(_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(_applyButton, &QPushButton::clicked, this, &BreakpointDialog::applyChanges);

    statusLayout->addWidget(_applyButton);
    statusLayout->addWidget(_closeButton);

    _mainLayout->addLayout(statusLayout);

    // Layout is managed by QDialog
    // No need to explicitly set layout as it's already set in setupUI()

    // Ensure the dialog has a reasonable minimum size
    setMinimumSize(800, 500);
}

void BreakpointDialog::setupFilterUI()
{
    QHBoxLayout* filterLayout = new QHBoxLayout();

    // Search label
    QLabel* searchLabel = new QLabel("Search:");
    filterLayout->addWidget(searchLabel);

    // Search field
    _searchField = new QLineEdit();
    _searchField->setPlaceholderText("Filter by address, group, notes...");
    connect(_searchField, &QLineEdit::textChanged, this, &BreakpointDialog::applyFilters);
    filterLayout->addWidget(_searchField);

    // Clear button
    _clearSearchButton = new QPushButton("×");
    _clearSearchButton->setToolTip("Clear search");
    _clearSearchButton->setFixedSize(24, 24);
    _clearSearchButton->setEnabled(false);
    connect(_clearSearchButton, &QPushButton::clicked, this, &BreakpointDialog::clearFilters);
    filterLayout->addWidget(_clearSearchButton);

    // Add filter layout to main layout before the table widget
    // This will place it at the top of the dialog
    _mainLayout->insertLayout(0, filterLayout);
}

void BreakpointDialog::setupGroupFilter()
{
    QHBoxLayout* groupLayout = new QHBoxLayout();

    QLabel* groupLabel = new QLabel("Group:");
    groupLayout->addWidget(groupLabel);

    _groupFilter = new QComboBox();
    _groupFilter->addItem("All Groups");
    populateGroupComboBox();
    connect(_groupFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BreakpointDialog::applyFilters);
    groupLayout->addWidget(_groupFilter);

    QPushButton* manageGroupsButton = new QPushButton("Manage Groups...");
    connect(manageGroupsButton, &QPushButton::clicked, this, &BreakpointDialog::manageGroups);
    groupLayout->addWidget(manageGroupsButton);

    groupLayout->addStretch();

    _mainLayout->insertLayout(0, groupLayout);
}

void BreakpointDialog::setupTableContextMenu()
{
    _breakpointTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_breakpointTable, &QTableWidget::customContextMenuRequested, this, &BreakpointDialog::showContextMenu);
}

void BreakpointDialog::setupShortcuts()
{
    // Add new breakpoint
    QShortcut* addShortcut = new QShortcut(QKeySequence("Ctrl+N"), this);
    connect(addShortcut, &QShortcut::activated, this, &BreakpointDialog::addBreakpoint);

    // Edit selected breakpoint
    QShortcut* editShortcut = new QShortcut(QKeySequence("Ctrl+E"), this);
    connect(editShortcut, &QShortcut::activated, this, &BreakpointDialog::editBreakpoint);

    // Delete selected breakpoint
    QShortcut* deleteShortcut = new QShortcut(QKeySequence("Delete"), this);
    connect(deleteShortcut, &QShortcut::activated, this, &BreakpointDialog::deleteBreakpoint);

    // Toggle selected breakpoint
    QShortcut* toggleShortcut = new QShortcut(QKeySequence("Ctrl+T"), this);
    connect(toggleShortcut, &QShortcut::activated, this, &BreakpointDialog::toggleBreakpoint);

    // Focus search field
    QShortcut* searchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(searchShortcut, &QShortcut::activated, [this]() {
        _searchField->setFocus();
        _searchField->selectAll();
    });

    // Clear filters
    QShortcut* clearShortcut = new QShortcut(QKeySequence("Escape"), this);
    connect(clearShortcut, &QShortcut::activated, this, &BreakpointDialog::clearFilters);
}

void BreakpointDialog::populateBreakpointTable()
{
    _breakpointTable->setSortingEnabled(false);
    _breakpointTable->setRowCount(0);

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;

    const BreakpointMapByID& breakpoints = bpManager->GetAllBreakpoints();

    int row = 0;
    for (const auto& pair : breakpoints)
    {
        uint16_t id = pair.first;
        const BreakpointDescriptor* bp = pair.second;

        _breakpointTable->insertRow(row);

        // ID column
        QTableWidgetItem* idItem = new QTableWidgetItem(QString::number(id));
        idItem->setData(Qt::UserRole, id);
        idItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 0, idItem);

        // Type column
        QString typeStr;
        switch (bp->type)
        {
            case BRK_MEMORY:
                typeStr = "Memory";
                break;
            case BRK_IO:
                typeStr = "Port";
                break;
            case BRK_KEYBOARD:
                typeStr = "Keyboard";
                break;
            default:
                typeStr = "Unknown";
                break;
        }
        QTableWidgetItem* typeItem = new QTableWidgetItem(typeStr);
        typeItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 1, typeItem);

        // Address column
        QString addrStr = QString("$%1").arg(bp->z80address, 4, 16, QChar('0')).toUpper();
        QTableWidgetItem* addrItem = new QTableWidgetItem(addrStr);
        addrItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 2, addrItem);

        // Access column
        QString accessStr;
        if (bp->type == BRK_MEMORY)
        {
            if (bp->memoryType & BRK_MEM_READ)
                accessStr += "R";
            if (bp->memoryType & BRK_MEM_WRITE)
                accessStr += "W";
            if (bp->memoryType & BRK_MEM_EXECUTE)
                accessStr += "X";
        }
        else if (bp->type == BRK_IO)
        {
            if (bp->ioType & BRK_IO_IN)
                accessStr += "I";
            if (bp->ioType & BRK_IO_OUT)
                accessStr += "O";
        }
        QTableWidgetItem* accessItem = new QTableWidgetItem(accessStr);
        accessItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 3, accessItem);

        // Status column
        QString statusStr = bp->active ? "Active" : "Inactive";
        QTableWidgetItem* statusItem = new QTableWidgetItem(statusStr);
        statusItem->setForeground(bp->active ? QColor(0, 128, 0) : QColor(128, 128, 128));
        statusItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 4, statusItem);

        // Group column
        QTableWidgetItem* groupItem = new QTableWidgetItem(QString::fromStdString(bp->group));
        groupItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 5, groupItem);

        // Note column
        QTableWidgetItem* noteItem = new QTableWidgetItem(QString::fromStdString(bp->note));
        noteItem->setTextAlignment(Qt::AlignCenter);
        _breakpointTable->setItem(row, 6, noteItem);

        row++;
    }

    _breakpointTable->setSortingEnabled(true);
    _breakpointTable->resizeColumnsToContents();
}

void BreakpointDialog::populateGroupComboBox()
{
    QString currentGroup = _groupFilter->currentText();

    _groupFilter->clear();
    _groupFilter->addItem("All Groups");

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (bpManager)
    {
        std::vector<std::string> groups = bpManager->GetBreakpointGroups();
        for (const auto& group : groups)
        {
            _groupFilter->addItem(QString::fromStdString(group));
        }
    }

    // Restore previous selection if it still exists
    int index = _groupFilter->findText(currentGroup);
    if (index != -1)
        _groupFilter->setCurrentIndex(index);
}

void BreakpointDialog::updateButtonStates()
{
    bool hasSelection = !_breakpointTable->selectedItems().isEmpty();

    _editButton->setEnabled(hasSelection);
    _deleteButton->setEnabled(hasSelection);
    _enableButton->setEnabled(hasSelection);
    _disableButton->setEnabled(hasSelection);
}

void BreakpointDialog::updateStatusBar()
{
    int totalRows = 0;
    int visibleRows = 0;

    // Count total and visible rows
    for (int row = 0; row < _breakpointTable->rowCount(); ++row)
    {
        totalRows++;
        if (!_breakpointTable->isRowHidden(row))
            visibleRows++;
    }

    // Update status label
    if (totalRows == visibleRows)
    {
        _statusLabel->setText(QString("Total breakpoints: %1").arg(totalRows));
    }
    else
    {
        _statusLabel->setText(QString("Showing %1 of %2 breakpoints").arg(visibleRows).arg(totalRows));
    }
}

void BreakpointDialog::addBreakpoint()
{
    BreakpointEditor editor(_emulator, BreakpointEditor::Add, this);

    if (editor.exec() == QDialog::Accepted)
    {
        // Refresh the breakpoint list to show the new breakpoint
        refreshBreakpointList();

        // Broadcast notification that breakpoints have changed
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
    }
}

void BreakpointDialog::editBreakpoint()
{
    // Get the selected breakpoint
    QList<QTableWidgetItem*> selectedItems = _breakpointTable->selectedItems();
    if (selectedItems.isEmpty())
    {
        QMessageBox::information(this, "Edit Breakpoint", "Please select a breakpoint to edit.");
        return;
    }

    // Get the breakpoint ID from the selected row
    int row = selectedItems.first()->row();
    QTableWidgetItem* idItem = _breakpointTable->item(row, 0);
    uint16_t id = idItem->data(Qt::UserRole).toUInt();

    // Open the breakpoint editor with the selected breakpoint
    BreakpointEditor editor(_emulator, BreakpointEditor::Edit, id, this);

    if (editor.exec() == QDialog::Accepted)
    {
        // Refresh the breakpoint list to show the updated breakpoint
        refreshBreakpointList();

        // Broadcast notification that breakpoints have changed
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
    }
}

void BreakpointDialog::deleteBreakpoint()
{
    QList<QTableWidgetItem*> selectedItems = _breakpointTable->selectedItems();
    if (selectedItems.isEmpty())
        return;

    int row = selectedItems.first()->row();
    QTableWidgetItem* idItem = _breakpointTable->item(row, 0);
    uint16_t id = idItem->data(Qt::UserRole).toUInt();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirm Delete",
                                  "Are you sure you want to delete breakpoint #" + QString::number(id) + "?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        BreakpointManager* bpManager = _emulator->GetBreakpointManager();
        if (bpManager)
        {
            bpManager->RemoveBreakpointByID(id);
            refreshBreakpointList();

            // Broadcast notification that breakpoints have changed
            MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
            messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
        }
    }
}

void BreakpointDialog::toggleBreakpoint()
{
    QList<QTableWidgetItem*> selectedItems = _breakpointTable->selectedItems();
    if (selectedItems.isEmpty())
        return;

    int row = selectedItems.first()->row();
    QTableWidgetItem* idItem = _breakpointTable->item(row, 0);
    uint16_t id = idItem->data(Qt::UserRole).toUInt();

    BreakpointManager* bpManager = _emulator->GetBreakpointManager();
    if (!bpManager)
        return;

    QTableWidgetItem* statusItem = _breakpointTable->item(row, 4);
    if (statusItem->text() == "Active")
    {
        bpManager->DeactivateBreakpoint(id);
        statusItem->setText("Inactive");
        statusItem->setForeground(QColor(128, 128, 128));
    }
    else
    {
        bpManager->ActivateBreakpoint(id);
        statusItem->setText("Active");
        statusItem->setForeground(QColor(0, 128, 0));
    }

    // Broadcast notification that breakpoints have changed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
}

void BreakpointDialog::createGroup()
{
    // We'll implement this when we create the BreakpointGroupDialog class
    QMessageBox::information(this, "Create Group", "This feature will be implemented soon.");
}

void BreakpointDialog::manageGroups()
{
    // We'll implement this when we create the BreakpointGroupDialog class
    QMessageBox::information(this, "Manage Groups", "This feature will be implemented soon.");
}

void BreakpointDialog::filterByGroup(const QString& group)
{
    applyFilters();
}

void BreakpointDialog::onBreakpointSelectionChanged()
{
    updateButtonStates();
}

void BreakpointDialog::onBreakpointDoubleClicked(QTableWidgetItem* item)
{
    editBreakpoint();
}

void BreakpointDialog::showContextMenu(const QPoint& pos)
{
    QTableWidgetItem* item = _breakpointTable->itemAt(pos);
    if (!item)
        return;

    int row = item->row();
    QTableWidgetItem* idItem = _breakpointTable->item(row, 0);
    uint16_t id = idItem->data(Qt::UserRole).toUInt();

    QMenu contextMenu(this);

    QAction* editAction = contextMenu.addAction("Edit Breakpoint");
    connect(editAction, &QAction::triggered, this, &BreakpointDialog::editBreakpoint);

    QTableWidgetItem* statusItem = _breakpointTable->item(row, 4);
    bool isActive = (statusItem->text() == "Active");

    QAction* toggleAction = contextMenu.addAction(isActive ? "Disable" : "Enable");
    connect(toggleAction, &QAction::triggered, this, &BreakpointDialog::toggleBreakpoint);

    contextMenu.addSeparator();

    QAction* deleteAction = contextMenu.addAction("Delete");
    connect(deleteAction, &QAction::triggered, this, &BreakpointDialog::deleteBreakpoint);

    contextMenu.exec(_breakpointTable->mapToGlobal(pos));
}

void BreakpointDialog::refreshBreakpointList()
{
    populateBreakpointTable();
    populateGroupComboBox();
    applyFilters();
    updateButtonStates();
    updateStatusBar();
}

void BreakpointDialog::applyFilters()
{
    QString searchText = _searchField->text().trimmed();
    QString groupFilter = _groupFilter->currentText();

    // Enable/disable clear button based on whether there's search text or group filter
    _clearSearchButton->setEnabled(!searchText.isEmpty() || groupFilter != "All Groups");

    // Hide rows that don't match the search text and group filter
    for (int row = 0; row < _breakpointTable->rowCount(); ++row)
    {
        bool matchesSearch = true;

        // Apply text search if there's search text
        if (!searchText.isEmpty())
        {
            // Search across multiple important columns (ID, Address, Access, Group, Notes)
            // We're using OR logic - match in any column is sufficient
            matchesSearch = false;

            // Check if search text might be an address (starts with $ or 0x)
            bool isAddressSearch = searchText.startsWith("$") || searchText.startsWith("0x");
            QString addressSearchText = searchText;
            if (isAddressSearch && searchText.startsWith("0x"))
            {
                // Convert 0xXXXX format to $XXXX format for matching
                addressSearchText = "$" + searchText.mid(2);
            }

            // Define columns to search in
            const int columnsToSearch[] = {0, 2, 3, 5, 6};  // ID, Address, Access, Group, Notes

            for (int colIndex : columnsToSearch)
            {
                QTableWidgetItem* item = _breakpointTable->item(row, colIndex);
                if (!item)
                    continue;

                // Special handling for address column
                if (isAddressSearch && colIndex == 2)  // Address column
                {
                    // For address searches, we want to match the address format exactly
                    if (item->text().contains(addressSearchText, Qt::CaseInsensitive))
                    {
                        matchesSearch = true;
                        break;
                    }
                }
                else if (item->text().contains(searchText, Qt::CaseInsensitive))
                {
                    matchesSearch = true;
                    break;
                }
            }
        }

        // Apply group filter if not "All Groups"
        bool matchesGroup = true;
        if (groupFilter != "All Groups")
        {
            QTableWidgetItem* groupItem = _breakpointTable->item(row, 5);
            matchesGroup = groupItem && groupItem->text() == groupFilter;
        }

        // Hide row if it doesn't match both filters
        _breakpointTable->setRowHidden(row, !(matchesSearch && matchesGroup));
    }

    // Update status bar with filtered count
    updateStatusBar();
}

void BreakpointDialog::clearFilters()
{
    _searchField->clear();
    _groupFilter->setCurrentText("All Groups");

    // Show all rows
    for (int row = 0; row < _breakpointTable->rowCount(); ++row)
    {
        _breakpointTable->setRowHidden(row, false);
    }

    updateStatusBar();
}

void BreakpointDialog::applyChanges()
{
    // Nothing to do here for now, as changes are applied immediately
    accept();
}
