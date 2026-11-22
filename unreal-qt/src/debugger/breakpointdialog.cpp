#include "breakpointdialog.h"
#include "emulator/remotecontrolguard.h"

#include <algorithm>

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
#include <QScrollBar>
#include <QTableWidgetItem>
#include <QThread>
#include <QVBoxLayout>

#include "3rdparty/message-center/messagecenter.h"
#include "breakpointeditor.h"
#include "breakpointgroupdialog.h"
#include "breakpointstopline.h"
#include "debugger/debuggercpu.h"
#include "debugger/debugmanager.h"
#include "emulator/emulator.h"

namespace
{
// The list's columns
enum Column
{
    ColId = 0,
    ColCpu,
    ColType,
    ColAddress,
    ColAccess,
    ColStatus,
    ColHits,
    ColGroup,
    ColCondition,
    ColNotes,
    ColCount
};

// The columns of a record's hits (the per-hit record, BreakpointHitRecord)
enum HitColumn
{
    HitColHit = 0,
    HitColFrame,
    HitColT,
    HitColPc,
    HitColAddress,
    HitColValue,
    HitColKind,
    HitColCardTime,
    HitColCount
};

constexpr int kHitRowsVisible = 8;  // the rows an expanded record shows before it scrolls

QString Hex(uint32_t value, int digits)
{
    return QString("$%1").arg(value, digits, 16, QChar('0')).toUpper();
}

// What the address field of a hit is: the CPU address or port, the event's kind, the chip register
QString HitAddressText(const BreakpointHitRecord& record)
{
    switch (static_cast<BreakpointTapKind>(record.kind))
    {
        case BRK_TAP_EVENT:
        case BRK_TAP_MACHINE_EVENT:
            return QString::number(record.address);
        case BRK_TAP_CHIPREG:
            return "reg " + Hex(record.address, 2);
        default:
            return Hex(record.address, 4);
    }
}

void SetCell(QTableWidget* table, int row, int column, const QString& text)
{
    QTableWidgetItem* item = table->item(row, column);
    if (!item)
    {
        item = new QTableWidgetItem();
        item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        table->setItem(row, column, item);
    }
    if (item->text() != text)
        item->setText(text);
}
}  // namespace

BreakpointDialog::BreakpointDialog(std::shared_ptr<Emulator> emulator, QWidget* parent) : QDialog(parent), _emulator(emulator)
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
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    m_breakpointObserverId = messageCenter.AddObserver(NC_BREAKPOINT_CHANGED, [this](int code, Message* message) {
        // Use mutex to ensure thread-safe access to the dialog
        QMutexLocker locker(&m_mutex);

        assert(this);

        // Check if the dialog is still valid before accessing it
        if (_emulator)
        {
            // Use QMetaObject::invokeMethod to ensure we're on the GUI thread
            QMetaObject::invokeMethod(this, "refreshBreakpointList", Qt::QueuedConnection);
        }
    });

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
    if (m_breakpointObserverId != 0)
    {
        MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
        messageCenter.RemoveObserverById(NC_BREAKPOINT_CHANGED, m_breakpointObserverId);
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
    settings.setValue("BreakpointDialog/sortColumn", _breakpointTable->header()->sortIndicatorSection());
    settings.setValue("BreakpointDialog/sortOrder", _breakpointTable->header()->sortIndicatorOrder());
}

void BreakpointDialog::setupUI()
{
    // Create main layout and widgets with proper parenting
    _mainLayout = new QVBoxLayout();
    setLayout(_mainLayout);

    // The expanded records are read while any is open
    _hitTimer = new QTimer(this);
    _hitTimer->setInterval(kHitPollMs);
    connect(_hitTimer, &QTimer::timeout, this, [this]() { refreshHitLogs(); });

    // The list: one row per breakpoint; a breakpoint that only counts and records its hits expands to show them
    _breakpointTable = new QTreeWidget();
    _mainLayout->addWidget(_breakpointTable);
    _breakpointTable->setColumnCount(ColCount);
    _breakpointTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    _breakpointTable->setSelectionMode(QAbstractItemView::SingleSelection);
    _breakpointTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    _breakpointTable->setAlternatingRowColors(true);
    _breakpointTable->setRootIsDecorated(true);
    _breakpointTable->setUniformRowHeights(false);
    _breakpointTable->setExpandsOnDoubleClick(false);  // a double click edits

    // Set the headers for the columns
    QStringList headers;
    headers << "ID" << "CPU" << "Type" << "Address" << "Access" << "Status" << "Hits" << "Group" << "Condition" << "Notes";
    _breakpointTable->setHeaderLabels(headers);

    // Make table sortable
    _breakpointTable->setSortingEnabled(true);
    _breakpointTable->header()->setSortIndicatorShown(true);
    _breakpointTable->header()->setSectionsClickable(true);

    // Set initial sort order (by ID)
    _breakpointTable->sortByColumn(0, Qt::AscendingOrder);

    // Set column resize modes
    QHeaderView* header = _breakpointTable->header();
    header->setStretchLastSection(false);
    for (int column = 0; column < ColNotes; ++column)
        header->setSectionResizeMode(column, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(ColNotes, QHeaderView::Stretch);

    // Connect signals
    connect(_breakpointTable, &QTreeWidget::itemSelectionChanged, this,
            &BreakpointDialog::onBreakpointSelectionChanged);
    connect(_breakpointTable, &QTreeWidget::itemDoubleClicked, this, &BreakpointDialog::onBreakpointDoubleClicked);
    connect(_breakpointTable, &QTreeWidget::itemExpanded, this, &BreakpointDialog::onItemExpanded);
    connect(_breakpointTable, &QTreeWidget::itemCollapsed, this, &BreakpointDialog::onItemCollapsed);

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
    connect(_enableButton, &QPushButton::clicked, this, [this]() { setSelectedActive(true); });
    connect(_disableButton, &QPushButton::clicked, this, [this]() { setSelectedActive(false); });
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

    // A card fitted: the breakpoints of all CPUs or of one
    _cpuFilter = new QComboBox();
    _cpuFilter->setObjectName("breakpointCpuFilter");
    _cpuFilter->addItem("All CPUs", -1);
    for (const DebugCpuId cpu : listedCpus())
        _cpuFilter->addItem(QString::fromLatin1(DebugCpuName(cpu)), static_cast<int>(cpu));
    connect(_cpuFilter, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &BreakpointDialog::applyFilters);
    QLabel* cpuLabel = new QLabel("CPU:");
    const bool cards = _cpuFilter->count() > 2;
    cpuLabel->setVisible(cards);
    _cpuFilter->setVisible(cards);
    groupLayout->addWidget(cpuLabel);
    groupLayout->addWidget(_cpuFilter);

    QPushButton* manageGroupsButton = new QPushButton("Manage Groups...");
    connect(manageGroupsButton, &QPushButton::clicked, this, &BreakpointDialog::manageGroups);
    groupLayout->addWidget(manageGroupsButton);

    groupLayout->addStretch();

    _mainLayout->insertLayout(0, groupLayout);
}

void BreakpointDialog::setupTableContextMenu()
{
    _breakpointTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(_breakpointTable, &QTreeWidget::customContextMenuRequested, this, &BreakpointDialog::showContextMenu);
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
    // The selection and the open records stay across the rebuild (an edit gives a breakpoint a new id: it closes)
    const QTreeWidgetItem* selected = selectedBreakpointItem();
    const uint32_t selectedKey = selected ? keyOf(selected) : UINT32_MAX;
    _hitViews.clear();
    _breakpointTable->setSortingEnabled(false);
    _breakpointTable->clear();
    // The CPU column only with a card fitted: the main CPU alone looks as it always did
    const std::vector<DebugCpuId> cpus = listedCpus();
    _breakpointTable->setColumnHidden(ColCpu, cpus.size() < 2);
    if (!managerOf(DebugCpuId::Main))
    {
        _expandedIds.clear();
        _hitTimer->stop();
        return;
    }

    QSet<uint32_t> expandable;
    QTreeWidgetItem* reselect = nullptr;

    for (const DebugCpuId cpu : cpus)
    {
        BreakpointManager* bpManager = managerOf(cpu);
        if (!bpManager)
            continue;
        const BreakpointListByID breakpoints = bpManager->GetAllBreakpoints();
        for (const auto& pair : breakpoints)
        {
            uint16_t id = pair.first;
            const BreakpointDescriptor* bp = pair.second.get();
            // The user's breakpoints: the step traps, the BASIC typer's control points and the debugger adapters' belong
            // to their owners (as GET /breakpoints lists them)
            if (bp == nullptr || !BreakpointManager::IsUserBreakpoint(*bp))
                continue;

            QTreeWidgetItem* item = new QTreeWidgetItem();
            item->setData(ColId, Qt::DisplayRole, static_cast<int>(id));  // sorts as a number
            item->setData(ColId, Qt::UserRole, id);
            item->setText(ColCpu, QString::fromLatin1(DebugCpuName(cpu)));
            item->setData(ColCpu, Qt::UserRole, static_cast<int>(cpu));

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
                case BRK_NEXTREG:
                    typeStr = "NextREG";
                    break;
                case BRK_BEAM:
                    typeStr = "Beam";
                    break;
                case BRK_COPPER:
                    typeStr = "Copper";
                    break;
                case BRK_MAGIC:
                    typeStr = "Magic";
                    break;
                case BRK_EVENT:
                    typeStr = "Event";
                    break;
                case BRK_CHIPREG:
                    typeStr = "Chip register";
                    break;
                default:
                    typeStr = "Unknown";
                    break;
            }
            item->setText(ColType, typeStr);

            // Address column
            QString addrStr;
            switch (bp->type)
            {
                case BRK_NEXTREG:  // a register or a range, the value it must have
                    addrStr = QString("NR $%1").arg(bp->z80address, 2, 16, QChar('0')).toUpper();
                    if (bp->isRange)
                        addrStr += QString("-$%1").arg(bp->z80addressEnd, 2, 16, QChar('0')).toUpper();
                    if (bp->nrMask != 0)
                        addrStr += bp->nrMask == 0xFF
                                       ? QString(" =$%1").arg(bp->nrValue, 2, 16, QChar('0')).toUpper()
                                       : QString(" =$%1&$%2")
                                             .arg(bp->nrValue, 2, 16, QChar('0'))
                                             .arg(bp->nrMask, 2, 16, QChar('0'))
                                             .toUpper();
                    break;
                case BRK_BEAM:
                    addrStr = QString("line %1 hc %2").arg(bp->z80address).arg(bp->beamHc);
                    break;
                case BRK_COPPER:
                    addrStr = QString("copper %1").arg(bp->z80address);
                    if (bp->isRange)
                        addrStr += QString("-%1").arg(bp->z80addressEnd);
                    break;
                case BRK_MAGIC:
                    addrStr = bp->z80address == BRK_MAGIC_ED_FF ? QString("ED FF") : QString("DD 01");
                    break;
                case BRK_EVENT:  // the kind in plain words, with its value filter
                    addrStr = bp->z80address < static_cast<uint16_t>(GSCardEvent::Count)
                                  ? EventBreakpointWords(static_cast<GSCardEvent>(bp->z80address), bp->nrValue, bp->nrMask)
                                  : QString("event %1").arg(bp->z80address);
                    if (bp->z80address < static_cast<uint16_t>(GSCardEvent::Count))
                        item->setToolTip(ColAddress, QString::fromLatin1(GSCardEventName(static_cast<GSCardEvent>(bp->z80address))));
                    break;
                default:
                    addrStr = QString("$%1").arg(bp->z80address, 4, 16, QChar('0')).toUpper();
                    if (bp->isRange)
                        addrStr += QString("-$%1").arg(bp->z80addressEnd, 4, 16, QChar('0')).toUpper();
                    break;
            }
            if (bp->matchType == BRK_MATCH_BANK_ADDR)  // physical: that page in any slot (or one slot)
                addrStr += " " + QString::fromStdString(BreakpointManager::PageSpecName(*bp)) + (bp->slotOnly ? " (slot)" : "");
            if (bp->type == BRK_IO && bp->portMask != 0xFFFF)
                addrStr += QString(" &$%1").arg(bp->portMask, 4, 16, QChar('0')).toUpper();
            item->setText(ColAddress, addrStr);

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
            else if (bp->type == BRK_NEXTREG)
            {
                if (bp->ioType & BRK_IO_IN)
                    accessStr += "R";
                if (bp->ioType & BRK_IO_OUT)
                    accessStr += "W";
                if (bp->nrDoors != BRK_NR_DOOR_ALL)
                    accessStr += " " + QString::fromStdString(BreakpointManager::NextRegDoorsName(bp->nrDoors));
            }
            else if (bp->type == BRK_KEYBOARD)
            {
                if (bp->keyType & BRK_KEY_PRESS)
                    accessStr += "P";
                if (bp->keyType & BRK_KEY_RELEASE)
                    accessStr += "R";
            }
            item->setText(ColAccess, accessStr);

            // Status column (a condition that failed while running stops matching until it is set again; a count-only
            // breakpoint counts and never stops)
            const bool countOnly = bp->tapSink == BRK_COUNT_TAP;
            setStatus(item, bp->active, countOnly, bp->conditionFailed, QString::fromStdString(bp->conditionError));

            // Hits column: the hits so far (sorts as a number), the policy that picks the ones that stop
            if (bp->hitMode == BRK_HIT_ALWAYS || countOnly)
                item->setData(ColHits, Qt::DisplayRole, static_cast<qulonglong>(bp->hitCount));
            else
                item->setText(ColHits, QString("%1 (stops on %2)")
                                           .arg(bp->hitCount)
                                           .arg(QString::fromStdString(BreakpointManager::HitSpecName(*bp))));
            QString hitsTip;
            if (bp->condition)
                hitsTip = QString("%1 of %2 matches held the condition").arg(bp->hitCount).arg(bp->matchCount);
            // A breakpoint that only counts and records its hits: the row expands to show them
            if (countOnly && bp->hitLogCapacity > 0)
            {
                item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
                if (!hitsTip.isEmpty())
                    hitsTip += "\n";
                hitsTip += QString("Records %1 %2 hits: expand the row to see them")
                               .arg(bp->hitLogKeep == BreakpointHitLogKeep::Last ? "the newest" : "the first")
                               .arg(bp->hitLogCapacity);
                expandable.insert(keyOf(cpu, id));
            }
            if (!hitsTip.isEmpty())
                item->setToolTip(ColHits, hitsTip);

            // Group column
            item->setText(ColGroup, QString::fromStdString(bp->group));

            // Condition column: as written (the editor shows how it binds)
            if (bp->condition)
            {
                item->setText(ColCondition, QString::fromStdString(bp->condition->Source()));
                item->setToolTip(ColCondition, "Stops when: " + QString::fromStdString(bp->condition->Canonical()));
            }

            // Note column
            item->setText(ColNotes, QString::fromStdString(bp->note));

            for (int column = 0; column < ColCount; ++column)
                if (column != ColCondition)
                    item->setTextAlignment(column, Qt::AlignCenter);

            _breakpointTable->addTopLevelItem(item);
            if (keyOf(cpu, id) == selectedKey)
                reselect = item;
        }
    }

    _breakpointTable->setSortingEnabled(true);
    for (int column = 0; column < ColCount; ++column)
        _breakpointTable->resizeColumnToContents(column);

    // Reopen the records that were open
    _expandedIds.intersect(expandable);
    const QSet<uint32_t> reopen = _expandedIds;
    for (int i = 0; i < _breakpointTable->topLevelItemCount(); ++i)
    {
        QTreeWidgetItem* item = _breakpointTable->topLevelItem(i);
        if (reopen.contains(keyOf(item)))
            item->setExpanded(true);
    }
    if (_expandedIds.isEmpty())
        _hitTimer->stop();
    if (reselect)
        _breakpointTable->setCurrentItem(reselect);
}

void BreakpointDialog::setStatus(QTreeWidgetItem* item, bool active, bool countOnly, bool conditionFailed,
                                 const QString& conditionError)
{
    const QString statusStr = conditionFailed ? "Condition failed"
                              : !active       ? "Inactive"
                              : countOnly     ? "Counting"
                                              : "Active";
    item->setText(ColStatus, statusStr);
    item->setForeground(ColStatus, conditionFailed ? QColor(192, 0, 0)
                                   : active        ? QColor(0, 128, 0)
                                                   : QColor(128, 128, 128));
    item->setToolTip(ColStatus, conditionFailed ? conditionError : QString());
}

QTreeWidgetItem* BreakpointDialog::selectedBreakpointItem() const
{
    const QList<QTreeWidgetItem*> items = _breakpointTable->selectedItems();
    if (items.isEmpty())
        return nullptr;
    QTreeWidgetItem* item = items.first();
    return item->parent() ? item->parent() : item;
}

uint16_t BreakpointDialog::breakpointIdOf(const QTreeWidgetItem* item)
{
    return item ? static_cast<uint16_t>(item->data(ColId, Qt::UserRole).toUInt()) : BRK_INVALID;
}

DebugCpuId BreakpointDialog::cpuOf(const QTreeWidgetItem* item)
{
    return item ? static_cast<DebugCpuId>(item->data(ColCpu, Qt::UserRole).toInt()) : DebugCpuId::Main;
}

std::vector<DebugCpuId> BreakpointDialog::listedCpus() const
{
    return debuggercpu::Fitted(_emulator ? _emulator->GetContext() : nullptr);  // lifetime-ok: the dialog holds the shared_ptr
}

BreakpointManager* BreakpointDialog::managerOf(DebugCpuId cpu) const
{
    DebugManager* debug = _emulator ? _emulator->GetDebugManager() : nullptr;
    return debug ? debug->GetBreakpointsManager(cpu) : nullptr;
}

void BreakpointDialog::onItemExpanded(QTreeWidgetItem* item)
{
    if (!item || item->parent())
        return;
    const uint32_t id = keyOf(item);
    if (_hitViews.contains(id))
        return;

    // One row under the breakpoint, across all columns: the record's summary over its hits
    QTreeWidgetItem* row = new QTreeWidgetItem(item);
    row->setFlags(Qt::ItemIsEnabled);  // not selectable: the breakpoint's row is
    row->setFirstColumnSpanned(true);

    QWidget* widget = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(widget);
    layout->setContentsMargins(4, 2, 4, 6);
    layout->setSpacing(2);
    HitView view;
    view.row = row;
    view.summary = new QLabel();
    view.summary->setObjectName("breakpointHitSummary");
    layout->addWidget(view.summary);
    view.table = new QTableWidget(0, HitColCount);
    view.table->setObjectName("breakpointHits");
    view.table->setHorizontalHeaderLabels({"Hit", "Frame", "T", "PC", "Address", "Value", "Kind", "Card time"});
    view.table->horizontalHeaderItem(HitColKind)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    view.table->verticalHeader()->setVisible(false);
    view.table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view.table->setSelectionBehavior(QAbstractItemView::SelectRows);
    view.table->setAlternatingRowColors(true);
    view.table->setWordWrap(false);
    view.table->horizontalHeader()->setDefaultAlignment(Qt::AlignRight | Qt::AlignVCenter);
    // Compact rows, as dense as the list's own
    const int lineHeight = view.table->fontMetrics().height() + 4;
    view.table->verticalHeader()->setMinimumSectionSize(lineHeight);
    view.table->verticalHeader()->setDefaultSectionSize(lineHeight);
    view.table->setColumnHidden(HitColCardTime, true);
    view.table->horizontalHeader()->setToolTip("Hit: the breakpoint's count after the hit. Frame and T: when it came (a card CPU: the ZX "
                           "frame and its time since the frame began). PC: the instruction. Value: the byte the hit "
                           "carried");
    layout->addWidget(view.table);
    _breakpointTable->setItemWidget(row, 0, widget);

    _hitViews.insert(id, view);
    _expandedIds.insert(id);
    refreshHitView(id, _hitViews[id], true);
    if (!_hitTimer->isActive())
        _hitTimer->start();
}

void BreakpointDialog::onItemCollapsed(QTreeWidgetItem* item)
{
    if (!item || item->parent())
        return;
    const uint32_t id = keyOf(item);
    _hitViews.remove(id);
    _expandedIds.remove(id);
    // The row goes (its widget with it): a closed record is not read
    while (item->childCount() > 0)
        delete item->takeChild(0);
    if (_expandedIds.isEmpty())
        _hitTimer->stop();
}

void BreakpointDialog::refreshHitLogs(bool force)
{
    for (auto it = _hitViews.begin(); it != _hitViews.end(); ++it)
        refreshHitView(it.key(), it.value(), force);
}

void BreakpointDialog::refreshHitView(uint32_t key, HitView& view, bool force)
{
    const uint16_t id = static_cast<uint16_t>(key & 0xFFFF);
    BreakpointManager* bpManager = managerOf(static_cast<DebugCpuId>(key >> 16));
    if (!bpManager || !view.table)
        return;
    // The counts first (no records copied): a record only changes when they move
    const BreakpointManager::HitLogInfo counts = bpManager->GetHitLog(id, 0, 0);
    if (!counts.found)
        return;
    if (!force && counts.hitCount == view.lastHitCount && counts.view.total == view.lastTotal)
        return;
    view.lastHitCount = counts.hitCount;
    view.lastTotal = counts.view.total;

    // The breakpoint's row follows its count
    if (QTreeWidgetItem* parent = view.row ? view.row->parent() : nullptr)
        if (parent->data(ColHits, Qt::DisplayRole).toULongLong() != counts.hitCount)
            parent->setData(ColHits, Qt::DisplayRole, static_cast<qulonglong>(counts.hitCount));

    if (!counts.hasLog)
    {
        view.summary->setText("This breakpoint keeps no record of its hits.");
        view.table->setRowCount(0);
        view.table->hide();
        view.row->setSizeHint(0, QSize(0, view.summary->sizeHint().height() + 2 + 6));
        return;
    }

    // The first ones of a record of the first N, the newest of a record of the newest
    const BreakpointHitLog::View& log = counts.view;
    const bool keepLast = log.keep == BreakpointHitLogKeep::Last;
    const uint64_t shown = std::min<uint64_t>(log.stored, kShownHits);
    const uint64_t from = keepLast ? log.oldest + log.stored - shown : log.oldest;
    const BreakpointManager::HitLogInfo page = bpManager->GetHitLog(id, from, static_cast<uint32_t>(shown));
    const std::vector<BreakpointHitRecord>& records = page.view.records;

    QString summary = QString("Records %1 %2 hits").arg(keepLast ? "the newest" : "the first").arg(page.view.capacity);
    if (page.view.total == 0)
        summary += ": no hits yet.";
    else
    {
        summary += QString(": %1 recorded, %2 held").arg(page.view.total).arg(page.view.stored);
        if (page.view.dropped > 0)
            summary += QString(", %1 %2").arg(page.view.dropped).arg(keepLast ? "overwritten" : "dropped (only counted)");
        summary += ".";
        if (records.size() < page.view.stored)
            summary += QString(" Showing the %1 %2.").arg(keepLast ? "newest" : "first").arg(records.size());
    }
    view.summary->setText(summary);

    QScrollBar* scroll = view.table->verticalScrollBar();
    const bool followNewest = keepLast && scroll->value() == scroll->maximum();
    bool cardTime = false;
    view.table->setUpdatesEnabled(false);
    view.table->setRowCount(static_cast<int>(records.size()));
    for (int row = 0; row < static_cast<int>(records.size()); ++row)
    {
        const BreakpointHitRecord& record = records[static_cast<size_t>(row)];
        SetCell(view.table, row, HitColHit, QString::number(record.hit));
        SetCell(view.table, row, HitColFrame, QString::number(record.frame));
        SetCell(view.table, row, HitColT, QString::number(record.t));
        SetCell(view.table, row, HitColPc, Hex(record.pc, 4));
        SetCell(view.table, row, HitColAddress, HitAddressText(record));
        SetCell(view.table, row, HitColValue, record.value >= 0 ? Hex(static_cast<uint32_t>(record.value), 2) : QString());
        SetCell(view.table, row, HitColKind, QString(BreakpointManager::HitKindName(record.kind)).replace('_', ' '));
        view.table->item(row, HitColKind)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        SetCell(view.table, row, HitColCardTime, record.cpuTime >= 0 ? QString::number(record.cpuTime) : QString());
        cardTime = cardTime || record.cpuTime >= 0;
    }
    view.table->setColumnHidden(HitColCardTime, !cardTime);
    if (!records.empty() && !view.sized)
    {
        view.table->resizeColumnsToContents();  // once: the columns stay where the human put them
        view.sized = true;
    }
    view.table->setUpdatesEnabled(true);

    // The row's height follows the hits held, up to kHitRowsVisible before it scrolls
    const int rows = std::clamp(static_cast<int>(records.size()), 1, kHitRowsVisible);
    const int tableHeight = view.table->horizontalHeader()->sizeHint().height() +
                            rows * view.table->verticalHeader()->defaultSectionSize() + 2 * view.table->frameWidth();
    if (view.table->minimumHeight() != tableHeight)
    {
        view.table->setFixedHeight(tableHeight);
        const int height = view.summary->sizeHint().height() + tableHeight + 2 + 2 + 6;
        view.row->setSizeHint(0, QSize(0, height));
    }
    // After the row took its height (a row just opened is laid out later): the newest stay in view
    if (followNewest)
        QMetaObject::invokeMethod(view.table, &QTableWidget::scrollToBottom, Qt::QueuedConnection);
}

void BreakpointDialog::populateGroupComboBox()
{
    QString currentGroup = _groupFilter->currentText();

    _groupFilter->clear();
    _groupFilter->addItem("All Groups");

    // Every listed CPU's groups
    for (const DebugCpuId cpu : listedCpus())
    {
        BreakpointManager* bpManager = managerOf(cpu);
        if (!bpManager)
            continue;
        for (const auto& group : bpManager->GetBreakpointGroups())
        {
            const QString name = QString::fromStdString(group);
            if (_groupFilter->findText(name) < 0)
                _groupFilter->addItem(name);
        }
    }

    // Restore previous selection if it still exists
    int index = _groupFilter->findText(currentGroup);
    if (index != -1)
        _groupFilter->setCurrentIndex(index);
}

void BreakpointDialog::updateButtonStates()
{
    bool hasSelection = selectedBreakpointItem() != nullptr;

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
    for (int row = 0; row < _breakpointTable->topLevelItemCount(); ++row)
    {
        totalRows++;
        if (!_breakpointTable->topLevelItem(row)->isHidden())
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
    const QTreeWidgetItem* selected = selectedBreakpointItem();
    if (!selected)
    {
        QMessageBox::information(this, "Edit Breakpoint", "Please select a breakpoint to edit.");
        return;
    }
    const uint16_t id = breakpointIdOf(selected);

    // Open the breakpoint editor with the selected breakpoint (of its own CPU)
    BreakpointEditor editor(_emulator, BreakpointEditor::Edit, id, this, cpuOf(selected));

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
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    const QTreeWidgetItem* selected = selectedBreakpointItem();
    if (!selected)
        return;
    const uint16_t id = breakpointIdOf(selected);
    const DebugCpuId cpu = cpuOf(selected);
    const QString whose = IsCardCpu(cpu) ? QString(" of the %1 CPU").arg(DebugCpuName(cpu)) : QString();

    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this, "Confirm Delete",
                                  "Are you sure you want to delete breakpoint #" + QString::number(id) + whose + "?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes)
    {
        BreakpointManager* bpManager = managerOf(cpu);
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

void BreakpointDialog::setSelectedActive(bool active)
{
    if (RemoteControlGuard::Refuse(this))  // an automation session holds the machine
        return;
    QTreeWidgetItem* selected = selectedBreakpointItem();
    BreakpointManager* bpManager = managerOf(cpuOf(selected));
    if (!selected || !bpManager)
        return;
    const uint16_t id = breakpointIdOf(selected);
    const BreakpointSnapshot bp = bpManager->GetBreakpointById(id);
    if (!bp)
        return;
    if (active)
        bpManager->ActivateBreakpoint(id);
    else
        bpManager->DeactivateBreakpoint(id);
    setStatus(selected, active, bp->tapSink == BRK_COUNT_TAP, bp->conditionFailed,
              QString::fromStdString(bp->conditionError));

    // Broadcast notification that breakpoints have changed
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    messageCenter.Post(NC_BREAKPOINT_CHANGED, nullptr, true);
}

void BreakpointDialog::toggleBreakpoint()
{
    const QTreeWidgetItem* selected = selectedBreakpointItem();
    BreakpointManager* bpManager = managerOf(cpuOf(selected));
    if (!selected || !bpManager)
        return;
    const BreakpointSnapshot bp = bpManager->GetBreakpointById(breakpointIdOf(selected));
    if (bp)
        setSelectedActive(!bp->active);
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

void BreakpointDialog::onBreakpointDoubleClicked(QTreeWidgetItem* item, int column)
{
    Q_UNUSED(column);
    if (item && !item->parent())
        editBreakpoint();
}

void BreakpointDialog::showContextMenu(const QPoint& pos)
{
    QTreeWidgetItem* item = _breakpointTable->itemAt(pos);
    if (!item)
        return;
    if (item->parent())
        item = item->parent();
    _breakpointTable->setCurrentItem(item);

    QMenu contextMenu(this);

    QAction* editAction = contextMenu.addAction("Edit Breakpoint");
    connect(editAction, &QAction::triggered, this, &BreakpointDialog::editBreakpoint);

    const bool isActive = item->text(ColStatus) == "Active" || item->text(ColStatus) == "Counting";
    QAction* toggleAction = contextMenu.addAction(isActive ? "Disable" : "Enable");
    connect(toggleAction, &QAction::triggered, this, &BreakpointDialog::toggleBreakpoint);

    // A breakpoint that records its hits: the same as its row's arrow
    if (item->childIndicatorPolicy() == QTreeWidgetItem::ShowIndicator)
    {
        QAction* hitsAction = contextMenu.addAction(item->isExpanded() ? "Hide Hits" : "Show Hits");
        connect(hitsAction, &QAction::triggered, this, [item]() { item->setExpanded(!item->isExpanded()); });
    }

    contextMenu.addSeparator();

    QAction* deleteAction = contextMenu.addAction("Delete");
    connect(deleteAction, &QAction::triggered, this, &BreakpointDialog::deleteBreakpoint);

    contextMenu.exec(_breakpointTable->viewport()->mapToGlobal(pos));
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
    const int cpuFilter = _cpuFilter ? _cpuFilter->currentData().toInt() : -1;

    // Enable/disable clear button based on whether there's search text or group filter
    _clearSearchButton->setEnabled(!searchText.isEmpty() || groupFilter != "All Groups" || cpuFilter >= 0);

    // Hide rows that don't match the search text and group filter
    for (int row = 0; row < _breakpointTable->topLevelItemCount(); ++row)
    {
        QTreeWidgetItem* item = _breakpointTable->topLevelItem(row);
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
            const int columnsToSearch[] = {ColId, ColCpu, ColType, ColAddress, ColAccess, ColGroup, ColCondition, ColNotes};

            for (int colIndex : columnsToSearch)
            {
                const QString text = item->text(colIndex);

                // Special handling for address column
                if (isAddressSearch && colIndex == ColAddress)  // Address column
                {
                    // For address searches, we want to match the address format exactly
                    if (text.contains(addressSearchText, Qt::CaseInsensitive))
                    {
                        matchesSearch = true;
                        break;
                    }
                }
                else if (text.contains(searchText, Qt::CaseInsensitive))
                {
                    matchesSearch = true;
                    break;
                }
            }
        }

        // Apply group filter if not "All Groups"
        bool matchesGroup = true;
        if (groupFilter != "All Groups")
            matchesGroup = item->text(ColGroup) == groupFilter;

        const bool matchesCpu = cpuFilter < 0 || static_cast<int>(cpuOf(item)) == cpuFilter;

        // Hide row if it doesn't match the filters
        item->setHidden(!(matchesSearch && matchesGroup && matchesCpu));
    }

    // Update status bar with filtered count
    updateStatusBar();
}

void BreakpointDialog::clearFilters()
{
    _searchField->clear();
    _groupFilter->setCurrentText("All Groups");
    if (_cpuFilter)
        _cpuFilter->setCurrentIndex(0);

    // Show all rows
    for (int row = 0; row < _breakpointTable->topLevelItemCount(); ++row)
        _breakpointTable->topLevelItem(row)->setHidden(false);

    updateStatusBar();
}

void BreakpointDialog::applyChanges()
{
    // Nothing to do here for now, as changes are applied immediately
    accept();
}
