#include "CategoryPanel.h"
#include "WebAPIClient.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QSizePolicy>
#include "emulator/monitoring/manifest.h"

// Cross-check: module arrays must match LOGGER_MODULE_COUNT - 1 (skip MODULE_NONE)
// Verified at compile time; if MODULE_COUNT changes in platform.h, this fires.
static_assert(monitoring::LOGGER_MODULE_COUNT == 13,
              "LOGGER_MODULE_COUNT changed — update kModuleCount and all arrays in CategoryPanel");

// Module API names matching PlatformModulesEnum order (1-12)
const char* CategoryPanel::kModuleApiNames[] = {
    "core", "z80", "memory", "io", "disk",
    "video", "sound", "dma", "loader", "debug", "disassembler", "recording"
};

// Display names for the tree
static const char* kModuleDisplayNames[] = {
    "Core", "Z80", "Memory", "IO", "Disk",
    "Video", "Sound", "DMA", "Loader", "Debug", "Disassembler", "Recording"
};

// Submodule names per module
static const char* kCoreSubmodules[]   = {"Init", "Config", "Loop", "State"};
static const char* kZ80Submodules[]    = {"Decode", "Execute", "Interrupt", "IO"};
static const char* kMemSubmodules[]    = {"Read", "Write", "Page", "Bank"};
static const char* kIOSubmodules[]     = {"Port", "Device", "Timing"};
static const char* kDiskSubmodules[]   = {"FDC", "Image", "TRDOS", "SCL"};
static const char* kVideoSubmodules[]  = {"ULA", "Border", "Attr", "Screen"};
static const char* kSoundSubmodules[]  = {"AY", "Beeper", "TurboSound", "Mixer"};
static const char* kDMASubmodules[]    = {"Transfer", "Timing"};
static const char* kLoaderSubmodules[] = {"SNA", "Z80", "TAP", "TZX", "TRD", "SCL"};
static const char* kDebugSubmodules[]  = {"Break", "Trace", "Watch", "Step"};
static const char* kDisasmSubmodules[] = {"Decode", "Label", "Xref"};
static const char* kRecSubmodules[]    = {"Manager", "Encoder"};

struct ModuleDef { const char** submodules; int count; };
static const ModuleDef kSubmodules[] = {
    {kCoreSubmodules,   4}, {kZ80Submodules,    4}, {kMemSubmodules,    4},
    {kIOSubmodules,     3}, {kDiskSubmodules,   4}, {kVideoSubmodules,  4},
    {kSoundSubmodules,  4}, {kDMASubmodules,    2}, {kLoaderSubmodules, 6},
    {kDebugSubmodules,  4}, {kDisasmSubmodules, 3}, {kRecSubmodules,    2},
};

static const char* kLevelNames[] = {"inherit", "trace", "debug", "info", "warning", "error", "none"};
static const char* kLevelDisplay[] = {"Inherit", "Trace", "Debug", "Info", "Warn", "Error", "None"};
static constexpr int kLevelCount = 7;

// =============================================================================
// Constructor
// =============================================================================

CategoryPanel::CategoryPanel(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // Header with expand/collapse
    auto* headerRow = new QHBoxLayout();
    auto* header = new QLabel("<b>Module Logger</b>");
    headerRow->addWidget(header);
    headerRow->addStretch();
    auto* expandBtn = new QPushButton("+ Expand");
    expandBtn->setFixedWidth(80);
    auto* collapseBtn = new QPushButton(QString::fromUtf8("\xe2\x88\x92") + " Collapse");
    collapseBtn->setFixedWidth(80);
    headerRow->addWidget(expandBtn);
    headerRow->addWidget(collapseBtn);
    layout->addLayout(headerRow);

    // Tab widget with Settings and View tabs
    _tabWidget = new QTabWidget();
    layout->addWidget(_tabWidget, 1);

    // ============================
    // Settings Tab
    // ============================
    auto* settingsTab = new QWidget();
    auto* settingsLayout = new QVBoxLayout(settingsTab);
    settingsLayout->setContentsMargins(0, 0, 0, 0);
    settingsLayout->setSpacing(4);

    _settingsSelectAll = new QCheckBox("Select / Deselect All");
    _settingsSelectAll->setTristate(true);
    _settingsSelectAll->setCheckState(Qt::Checked);
    settingsLayout->addWidget(_settingsSelectAll);

    _moduleTree = new QTreeWidget();
    _moduleTree->setHeaderLabels({"Module", "Level", "Status"});
    _moduleTree->header()->setStretchLastSection(false);
    _moduleTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _moduleTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    _moduleTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    _moduleTree->setColumnWidth(1, 90);
    _moduleTree->setColumnWidth(2, 110);
    _moduleTree->setIndentation(16);
    _moduleTree->setRootIsDecorated(true);
    settingsLayout->addWidget(_moduleTree, 1);

    connect(_settingsSelectAll, &QCheckBox::toggled, this, &CategoryPanel::onSettingsSelectAllToggled);

    buildSettingsTree();
    connect(_moduleTree, &QTreeWidget::itemChanged, this, &CategoryPanel::onSettingsItemChanged);
    connect(_moduleTree, &QTreeWidget::itemDoubleClicked, this, &CategoryPanel::onItemDoubleClicked);

    connect(expandBtn, &QPushButton::clicked, this, [this]() {
        if (_tabWidget->currentIndex() == 0) _moduleTree->expandAll();
        else _viewTree->expandAll();
    });
    connect(collapseBtn, &QPushButton::clicked, this, [this]() {
        if (_tabWidget->currentIndex() == 0) _moduleTree->collapseAll();
        else _viewTree->collapseAll();
    });

    // Global level
    auto* levelRow = new QHBoxLayout();
    levelRow->addWidget(new QLabel("Global Level:"));
    _globalLevelCombo = new QComboBox();
    _globalLevelCombo->addItems({"Trace", "Debug", "Info", "Warn", "Error"});
    _globalLevelCombo->setCurrentIndex(0);
    connect(_globalLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!_suppressSignals) { _hasPendingChanges = true; updateButtons(); }
    });
    levelRow->addWidget(_globalLevelCombo, 1);
    settingsLayout->addLayout(levelRow);

    // Status
    _statusLabel = new QLabel("Waiting for emulator...");
    _statusLabel->setStyleSheet("color: #888; font-size: 11px;");
    _statusLabel->setWordWrap(true);
    _statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    settingsLayout->addWidget(_statusLabel);

    // Buttons
    auto* btnRow = new QHBoxLayout();
    _applyBtn = new QPushButton("Apply");
    _cancelBtn = new QPushButton("Cancel");
    auto* refreshBtn = new QPushButton("Refresh");
    _applyBtn->setEnabled(false);
    _cancelBtn->setEnabled(false);
    connect(_applyBtn, &QPushButton::clicked, this, &CategoryPanel::onApply);
    connect(_cancelBtn, &QPushButton::clicked, this, &CategoryPanel::onCancel);
    connect(refreshBtn, &QPushButton::clicked, this, &CategoryPanel::onRefresh);
    btnRow->addWidget(_applyBtn);
    btnRow->addWidget(_cancelBtn);
    btnRow->addWidget(refreshBtn);
    settingsLayout->addLayout(btnRow);

    _tabWidget->addTab(settingsTab, "Settings");

    // ============================
    // View Tab (display filter)
    // ============================
    auto* viewTab = new QWidget();
    auto* viewLayout = new QVBoxLayout(viewTab);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(4);

    auto* viewTip = new QLabel("<i>Toggle modules to show/hide their log messages</i>");
    viewTip->setStyleSheet("color: #888; font-size: 10px;");
    _selectAllCheckbox = new QCheckBox("Select / Deselect All");
    _selectAllCheckbox->setTristate(true);
    _selectAllCheckbox->setCheckState(Qt::Checked);
    viewLayout->addWidget(_selectAllCheckbox);

    connect(_selectAllCheckbox, &QCheckBox::toggled, this, &CategoryPanel::onSelectAllToggled);

    _viewTree = new QTreeWidget();
    _viewTree->setHeaderLabels({"Module", "Activity", "Count"});
    _viewTree->header()->setStretchLastSection(false);
    _viewTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    _viewTree->header()->setSectionResizeMode(1, QHeaderView::Fixed);
    _viewTree->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    _viewTree->setColumnWidth(1, 50);
    _viewTree->setColumnWidth(2, 60);
    _viewTree->setIndentation(16);
    _viewTree->setRootIsDecorated(true);
    viewLayout->addWidget(_viewTree, 1);

    connect(_viewTree, &QTreeWidget::itemChanged, this, &CategoryPanel::onViewItemChanged);

    _tabWidget->addTab(viewTab, "View");

    // Populate View tree immediately (all modules, all checked)
    buildViewTree();

    // Activity timer (1s tick to update staleness)
    _activityTimer = new QTimer(this);
    _activityTimer->setInterval(1000);
    connect(_activityTimer, &QTimer::timeout, this, &CategoryPanel::onActivityTimerTick);
    _activityTimer->start();

    // Initialize elapsed timers
    for (int i = 0; i < kModuleCount; i++)
        _lastActivity[i].invalidate();
}

// =============================================================================
// WebAPI client binding
// =============================================================================

void CategoryPanel::setApiClient(WebAPIClient* client, const QString& emulatorId)
{
    _apiClient = client;
    _emulatorId = emulatorId;

    if (_apiClient)
    {
        connect(_apiClient, &WebAPIClient::loggingStateReceived,
                this, &CategoryPanel::updateFromLoggingState);
        connect(_apiClient, &WebAPIClient::loggingUpdateDone,
                this, [this](const QString& msg) {
                    _statusLabel->setText(msg);
                    QTimer::singleShot(300, this, &CategoryPanel::onRefresh);
                });
        connect(_apiClient, &WebAPIClient::errorOccurred,
                this, [this](const QString& err) {
                    _statusLabel->setText("Error: " + err);
                });
    }
}

// =============================================================================
// Build Settings tree (emulator-side control)
// =============================================================================

void CategoryPanel::buildSettingsTree()
{
    _suppressSignals = true;

    for (int m = 0; m < kModuleCount; m++)
    {
        auto* moduleItem = new QTreeWidgetItem(_moduleTree);
        moduleItem->setText(0, kModuleDisplayNames[m]);
        moduleItem->setCheckState(0, Qt::Unchecked);
        moduleItem->setText(2, QString::fromUtf8("\xe2\x80\x94"));
        moduleItem->setData(0, Qt::UserRole, m);
        moduleItem->setData(0, Qt::UserRole + 1, -1);

        auto* levelCombo = new QComboBox();
        for (int i = 0; i < kLevelCount; i++)
            levelCombo->addItem(kLevelDisplay[i]);
        levelCombo->setCurrentIndex(0);
        _moduleTree->setItemWidget(moduleItem, 1, levelCombo);
        _moduleLevelCombos[m] = levelCombo;
        connect(levelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
            if (!_suppressSignals) { _hasPendingChanges = true; updateButtons(); }
        });

        const auto& def = kSubmodules[m];
        for (int s = 0; s < def.count; s++)
        {
            auto* subItem = new QTreeWidgetItem(moduleItem);
            subItem->setText(0, def.submodules[s]);
            subItem->setCheckState(0, Qt::Unchecked);
            subItem->setText(2, QString::fromUtf8("\xe2\x80\x94"));
            subItem->setData(0, Qt::UserRole, m);
            subItem->setData(0, Qt::UserRole + 1, s);
        }
    }

    _moduleTree->expandAll();
    _suppressSignals = false;
}

// =============================================================================
// Build / refresh View tree (display filter)
// =============================================================================

void CategoryPanel::buildViewTree()
{
    _suppressSignals = true;
    _viewTree->clear();

    for (int m = 0; m < kModuleCount; m++)
    {
        // Determine initial checked state: checked if server says enabled, or if no state yet
        bool moduleEnabled = !_hasServerState || _serverModules[m].enabled;

        auto* moduleItem = new QTreeWidgetItem(_viewTree);
        moduleItem->setText(0, kModuleDisplayNames[m]);
        moduleItem->setCheckState(0, moduleEnabled ? Qt::Checked : Qt::Unchecked);
        moduleItem->setText(1, QString::fromUtf8("\xe2\x97\x8b"));  // ○ idle
        moduleItem->setForeground(1, QColor("#ccc"));
        moduleItem->setText(2, QString::number(_activityCounts[m]));
        moduleItem->setData(0, Qt::UserRole, m);
        moduleItem->setData(0, Qt::UserRole + 1, -1);

        // Always show all submodules
        const auto& def = kSubmodules[m];
        for (int s = 0; s < def.count; s++)
        {
            bool subEnabled = moduleEnabled &&
                (!_hasServerState || (_serverModules[m].submoduleMask & (1u << s)));

            auto* subItem = new QTreeWidgetItem(moduleItem);
            subItem->setText(0, def.submodules[s]);
            subItem->setCheckState(0, subEnabled ? Qt::Checked : Qt::Unchecked);
            subItem->setText(2, "0");
            subItem->setData(0, Qt::UserRole, m);
            subItem->setData(0, Qt::UserRole + 1, s);
        }
    }

    _viewTree->expandAll();
    _suppressSignals = false;
}

void CategoryPanel::refreshViewTree()
{
    buildViewTree();
}

// =============================================================================
// Parse JSON logging state from WebAPI
// =============================================================================

void CategoryPanel::updateFromLoggingState(const QJsonObject& state)
{
    _suppressSignals = true;

    // Global level
    _serverGlobalLevel = state.value("global_level").toString("trace");
    static const char* globalLevels[] = {"trace", "debug", "info", "warning", "error"};
    for (int i = 0; i < 5; i++)
    {
        if (_serverGlobalLevel == globalLevels[i])
        {
            _globalLevelCombo->setCurrentIndex(i);
            break;
        }
    }

    // Parse modules array
    QJsonArray modules = state.value("modules").toArray();
    for (int i = 0; i < modules.size() && i < kModuleCount; i++)
    {
        QJsonObject mod = modules[i].toObject();
        _serverModules[i].enabled = mod.value("enabled").toBool();
        _serverModules[i].submoduleMask = static_cast<uint16_t>(mod.value("submodule_mask").toInt(0xFFFF));
        _serverModules[i].level = mod.value("level").toString("inherit");

        // Update module level combo
        for (int l = 0; l < kLevelCount; l++)
        {
            if (_serverModules[i].level == kLevelNames[l])
            {
                _moduleLevelCombos[i]->setCurrentIndex(l);
                break;
            }
        }

        // Update Settings tree checkboxes
        auto* moduleItem = _moduleTree->topLevelItem(i);
        if (!moduleItem) continue;

        bool allChecked = true, noneChecked = true;
        for (int s = 0; s < moduleItem->childCount(); s++)
        {
            auto* subItem = moduleItem->child(s);
            bool on = _serverModules[i].enabled && (_serverModules[i].submoduleMask & (1u << s));
            subItem->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            markPending(subItem, false);
            if (on) noneChecked = false; else allChecked = false;
        }

        if (!_serverModules[i].enabled || noneChecked)
            moduleItem->setCheckState(0, Qt::Unchecked);
        else if (allChecked)
            moduleItem->setCheckState(0, Qt::Checked);
        else
            moduleItem->setCheckState(0, Qt::PartiallyChecked);
        markPending(moduleItem, false);
    }

    _hasServerState = true;
    updateStatusColumn();
    _hasPendingChanges = false;
    updateButtons();
    _statusLabel->setText("State synchronized");
    _suppressSignals = false;

    // Rebuild View tab to reflect newly enabled modules
    refreshViewTree();
}

// =============================================================================
// Status column (Settings tab)
// =============================================================================

void CategoryPanel::updateStatusColumn()
{
    if (!_hasServerState) return;

    for (int i = 0; i < _moduleTree->topLevelItemCount() && i < kModuleCount; i++)
    {
        auto* moduleItem = _moduleTree->topLevelItem(i);
        const auto& ms = _serverModules[i];
        const auto& def = kSubmodules[i];

        QString effectiveLevel = (ms.level == "inherit") ? _serverGlobalLevel : ms.level;

        if (!ms.enabled)
        {
            moduleItem->setText(2, "OFF");
            moduleItem->setForeground(2, QColor("#999"));
        }
        else
        {
            uint16_t allMask = (1u << def.count) - 1;
            uint16_t effective = ms.submoduleMask & allMask;

            if (effective == 0)
            {
                moduleItem->setText(2, "OFF");
                moduleItem->setForeground(2, QColor("#999"));
            }
            else if (effective == allMask)
            {
                moduleItem->setText(2, QString("ON [%1]").arg(effectiveLevel));
                moduleItem->setForeground(2, QColor("#2E7D32"));
            }
            else
            {
                moduleItem->setText(2, QString("Partial [%1]").arg(effectiveLevel));
                moduleItem->setForeground(2, QColor("#F57F17"));
            }
        }

        for (int s = 0; s < moduleItem->childCount(); s++)
        {
            auto* subItem = moduleItem->child(s);
            bool on = ms.enabled && (ms.submoduleMask & (1u << s));
            subItem->setText(2, on ? "ON" : "OFF");
            subItem->setForeground(2, on ? QColor("#2E7D32") : QColor("#999"));
        }
    }
}

// =============================================================================
// Settings tab: item changed
// =============================================================================

void CategoryPanel::onSettingsItemChanged(QTreeWidgetItem* item, int column)
{
    if (_suppressSignals || column != 0) return;
    _suppressSignals = true;

    int subIdx = item->data(0, Qt::UserRole + 1).toInt();
    if (subIdx == -1)
    {
        Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked)
        {
            for (int i = 0; i < item->childCount(); i++)
            {
                item->child(i)->setCheckState(0, state);
                markPending(item->child(i), true);
            }
        }
    }
    else
    {
        auto* parent = item->parent();
        if (parent)
        {
            bool allChecked = true, noneChecked = true;
            for (int i = 0; i < parent->childCount(); i++)
            {
                if (parent->child(i)->checkState(0) == Qt::Checked) noneChecked = false;
                else allChecked = false;
            }
            if (allChecked) parent->setCheckState(0, Qt::Checked);
            else if (noneChecked) parent->setCheckState(0, Qt::Unchecked);
            else parent->setCheckState(0, Qt::PartiallyChecked);
        }
    }

    markPending(item, true);
    _hasPendingChanges = true;
    updateButtons();
    _suppressSignals = false;
    updateSettingsSelectAll();
}

// =============================================================================
// Double-click → filter (legacy)
// =============================================================================

void CategoryPanel::onItemDoubleClicked(QTreeWidgetItem* item, int /*column*/)
{
    int moduleIdx = item->data(0, Qt::UserRole).toInt();
    int subIdx = item->data(0, Qt::UserRole + 1).toInt();
    emit filterRequested(moduleIdx + 1, subIdx);
}

// =============================================================================
// View tab: checkbox changed → immediate visibility toggle
// =============================================================================

void CategoryPanel::onViewItemChanged(QTreeWidgetItem* item, int column)
{
    if (_suppressSignals || column != 0) return;
    _suppressSignals = true;

    int moduleIdx = item->data(0, Qt::UserRole).toInt();
    int subIdx = item->data(0, Qt::UserRole + 1).toInt();

    if (subIdx == -1)
    {
        // Module-level toggle: propagate to children
        Qt::CheckState state = item->checkState(0);
        if (state != Qt::PartiallyChecked)
        {
            for (int i = 0; i < item->childCount(); i++)
                item->child(i)->setCheckState(0, state);
        }

        // Emit visibility change: moduleId is 1-based (PlatformModulesEnum)
        bool visible = (state == Qt::Checked || state == Qt::PartiallyChecked);
        emit visibilityChanged(moduleIdx + 1, visible);

        // Also emit submodule mask (all on or all off when module toggled)
        uint16_t mask = 0;
        for (int i = 0; i < item->childCount(); i++)
        {
            if (item->child(i)->checkState(0) == Qt::Checked)
                mask |= (1u << i);
        }
        emit submoduleVisibilityChanged(moduleIdx + 1, mask);
    }
    else
    {
        // Submodule toggle: update parent state
        auto* parent = item->parent();
        if (parent)
        {
            bool allChecked = true, noneChecked = true;
            uint16_t mask = 0;
            for (int i = 0; i < parent->childCount(); i++)
            {
                if (parent->child(i)->checkState(0) == Qt::Checked)
                {
                    noneChecked = false;
                    mask |= (1u << i);
                }
                else
                {
                    allChecked = false;
                }
            }
            if (allChecked) parent->setCheckState(0, Qt::Checked);
            else if (noneChecked) parent->setCheckState(0, Qt::Unchecked);
            else parent->setCheckState(0, Qt::PartiallyChecked);

            bool visible = !noneChecked;
            emit visibilityChanged(moduleIdx + 1, visible);
            emit submoduleVisibilityChanged(moduleIdx + 1, mask);
        }
    }

    _suppressSignals = false;
    updateViewSelectAll();
}

// =============================================================================
// Select / Deselect All
// =============================================================================

void CategoryPanel::onSettingsSelectAllToggled(bool checked)
{
    _suppressSignals = true;
    Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
    for (int i = 0; i < _moduleTree->topLevelItemCount(); i++)
    {
        auto* item = _moduleTree->topLevelItem(i);
        item->setCheckState(0, state);
        markPending(item, true);
        for (int c = 0; c < item->childCount(); c++)
        {
            item->child(c)->setCheckState(0, state);
            markPending(item->child(c), true);
        }
    }
    _suppressSignals = false;
    _hasPendingChanges = true;
    updateButtons();
}

void CategoryPanel::onSelectAllToggled(bool checked)
{
    _suppressSignals = true;
    Qt::CheckState state = checked ? Qt::Checked : Qt::Unchecked;
    for (int i = 0; i < _viewTree->topLevelItemCount(); i++)
    {
        auto* item = _viewTree->topLevelItem(i);
        item->setCheckState(0, state);
        for (int c = 0; c < item->childCount(); c++)
            item->child(c)->setCheckState(0, state);
    }
    _suppressSignals = false;

    // Emit visibility + submodule signals for all modules
    for (int i = 0; i < _viewTree->topLevelItemCount(); i++)
    {
        auto* item = _viewTree->topLevelItem(i);
        int moduleIdx = item->data(0, Qt::UserRole).toInt();
        emit visibilityChanged(moduleIdx + 1, checked);

        uint16_t mask = 0;
        if (checked)
        {
            for (int c = 0; c < item->childCount(); c++)
                mask |= (1u << c);
        }
        emit submoduleVisibilityChanged(moduleIdx + 1, mask);
    }
}

// =============================================================================
// Activity indicators
// =============================================================================

void CategoryPanel::onModuleActivity(int moduleId, int submoduleId)
{
    if (moduleId < 1 || moduleId > kModuleCount) return;
    int idx = moduleId - 1;

    _activityCounts[idx]++;
    _lastActivity[idx].restart();

    // Track submodule activity
    if (submoduleId >= 0 && submoduleId < kMaxSubmodules)
    {
        _subActivityCounts[idx][submoduleId]++;
        _subLastActivity[idx][submoduleId].restart();
    }

    // Update counts and dots in View tree
    for (int i = 0; i < _viewTree->topLevelItemCount(); i++)
    {
        auto* item = _viewTree->topLevelItem(i);
        if (item->data(0, Qt::UserRole).toInt() != idx) continue;

        item->setText(2, QString::number(_activityCounts[idx]));
        setActivityDot(item, _lastActivity[idx]);

        // Update matching submodule child
        if (submoduleId >= 0)
        {
            for (int c = 0; c < item->childCount(); c++)
            {
                auto* child = item->child(c);
                if (child->data(0, Qt::UserRole + 1).toInt() == submoduleId)
                {
                    child->setText(2, QString::number(_subActivityCounts[idx][submoduleId]));
                    setActivityDot(child, _subLastActivity[idx][submoduleId]);
                    break;
                }
            }
        }
        break;
    }
}

void CategoryPanel::onActivityTimerTick()
{
    for (int i = 0; i < _viewTree->topLevelItemCount(); i++)
    {
        auto* item = _viewTree->topLevelItem(i);
        int idx = item->data(0, Qt::UserRole).toInt();
        if (idx < 0 || idx >= kModuleCount) continue;

        setActivityDot(item, _lastActivity[idx]);

        for (int c = 0; c < item->childCount(); c++)
        {
            auto* child = item->child(c);
            int subIdx = child->data(0, Qt::UserRole + 1).toInt();
            if (subIdx >= 0 && subIdx < kMaxSubmodules)
                setActivityDot(child, _subLastActivity[idx][subIdx]);
        }
    }
}

void CategoryPanel::setActivityDot(QTreeWidgetItem* item, const QElapsedTimer& timer)
{
    if (!timer.isValid())
    {
        item->setText(1, QString::fromUtf8("\xe2\x97\x8b"));  // ○ never
        item->setForeground(1, QColor("#ccc"));
    }
    else
    {
        qint64 elapsed = timer.elapsed();
        if (elapsed < 2000)
        {
            item->setText(1, QString::fromUtf8("\xe2\x97\x8f"));  // ● active
            item->setForeground(1, QColor("#2E7D32"));
        }
        else if (elapsed < 10000)
        {
            item->setText(1, QString::fromUtf8("\xe2\x97\x8f"));  // ● recent
            item->setForeground(1, QColor("#F57F17"));
        }
        else
        {
            item->setText(1, QString::fromUtf8("\xe2\x97\x8b"));  // ○ idle
            item->setForeground(1, QColor("#999"));
        }
    }
}

// =============================================================================
// Apply — send changes via WebAPI
// =============================================================================

void CategoryPanel::onApply()
{
    if (!_apiClient) return;
    int pendingOps = 0;

    for (int i = 0; i < _moduleTree->topLevelItemCount() && i < kModuleCount; i++)
    {
        auto* moduleItem = _moduleTree->topLevelItem(i);

        uint16_t desiredMask = 0;
        for (int s = 0; s < moduleItem->childCount(); s++)
        {
            if (moduleItem->child(s)->checkState(0) == Qt::Checked)
                desiredMask |= (1u << s);
        }
        bool desiredEnabled = (desiredMask != 0);

        int levelIdx = _moduleLevelCombos[i]->currentIndex();
        QString desiredLevel = kLevelNames[levelIdx];

        // Without server state, always send; with server state, only send diffs
        bool needsSend = !_hasServerState;
        if (_hasServerState)
        {
            bool maskChanged = desiredMask != _serverModules[i].submoduleMask;
            bool enableChanged = desiredEnabled != _serverModules[i].enabled;
            bool levelChanged = desiredLevel != _serverModules[i].level;
            needsSend = maskChanged || enableChanged || levelChanged;
        }

        if (needsSend)
        {
            QJsonObject params;
            params["enabled"] = desiredEnabled;
            params["submodule_mask"] = desiredMask;
            params["level"] = desiredLevel;

            _apiClient->setLoggingModule(_emulatorId, kModuleApiNames[i], params);
            pendingOps++;
        }
    }

    static const char* globalLevels[] = {"trace", "debug", "info", "warning", "error"};
    int globalIdx = _globalLevelCombo->currentIndex();
    QString desiredGlobal = (globalIdx >= 0 && globalIdx < 5) ? globalLevels[globalIdx] : "trace";
    if (!_hasServerState || desiredGlobal != _serverGlobalLevel)
    {
        _apiClient->setLoggingLevel(_emulatorId, desiredGlobal);
        pendingOps++;
    }

    if (pendingOps > 0)
        _statusLabel->setText(QString("Applying %1 change(s)...").arg(pendingOps));
    else
        _statusLabel->setText("No changes to apply");
}

// =============================================================================
// Cancel
// =============================================================================

void CategoryPanel::onCancel()
{
    if (!_hasServerState) return;

    _suppressSignals = true;
    for (int i = 0; i < _moduleTree->topLevelItemCount() && i < kModuleCount; i++)
    {
        auto* moduleItem = _moduleTree->topLevelItem(i);
        const auto& ms = _serverModules[i];

        bool allChecked = true, noneChecked = true;
        for (int s = 0; s < moduleItem->childCount(); s++)
        {
            bool on = ms.enabled && (ms.submoduleMask & (1u << s));
            moduleItem->child(s)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            markPending(moduleItem->child(s), false);
            if (on) noneChecked = false; else allChecked = false;
        }

        if (!ms.enabled || noneChecked)
            moduleItem->setCheckState(0, Qt::Unchecked);
        else if (allChecked)
            moduleItem->setCheckState(0, Qt::Checked);
        else
            moduleItem->setCheckState(0, Qt::PartiallyChecked);
        markPending(moduleItem, false);

        for (int l = 0; l < kLevelCount; l++)
        {
            if (ms.level == kLevelNames[l])
            {
                _moduleLevelCombos[i]->setCurrentIndex(l);
                break;
            }
        }
    }

    static const char* globalLevels[] = {"trace", "debug", "info", "warning", "error"};
    for (int i = 0; i < 5; i++)
    {
        if (_serverGlobalLevel == globalLevels[i])
        {
            _globalLevelCombo->setCurrentIndex(i);
            break;
        }
    }

    _hasPendingChanges = false;
    updateButtons();
    _statusLabel->setText("Reverted to server state");
    _suppressSignals = false;
}

// =============================================================================
// Refresh
// =============================================================================

void CategoryPanel::onLoggerStateReceived(uint32_t moduleMask, QVector<uint16_t> submoduleMasks,
                                           QVector<uint8_t> moduleLevels, uint8_t globalLevel)
{
    // Don't overwrite if user has unsaved changes
    if (_hasPendingChanges)
        return;

    _suppressSignals = true;

    // Global level combo: items are {Trace=0, Debug=1, Info=2, Warn=3, Error=4}
    // LoggerLevel enum: LogTrace=1, LogDebug=2, LogInfo=3, LogWarning=4, LogError=5
    // Mapping: comboIndex = globalLevel - 1
    static const char* globalLevels[] = {"trace", "debug", "info", "warning", "error"};
    if (globalLevel >= 1 && globalLevel <= 5)
    {
        _globalLevelCombo->setCurrentIndex(globalLevel - 1);
        _serverGlobalLevel = globalLevels[globalLevel - 1];
    }

    // Track aggregate for select-all checkbox
    int totalModulesOn = 0, totalModulesOff = 0;

    // Update per-module state
    // SHM arrays are PlatformModulesEnum-indexed: [0]=unknown, [1]=Core, [2]=Z80, ...
    // UI tree items: [0]=Core, [1]=Z80, ... → use SHM index (i + 1)
    for (int i = 0; i < kModuleCount; i++)
    {
        int shmIdx = i + 1;  // PlatformModulesEnum offset
        bool enabled = (moduleMask & (1u << shmIdx)) != 0;
        uint16_t subMask = (shmIdx < submoduleMasks.size()) ? submoduleMasks[shmIdx] : 0xFFFF;
        uint8_t level = (shmIdx < moduleLevels.size()) ? moduleLevels[shmIdx] : 0;

        _serverModules[i].enabled = enabled;
        _serverModules[i].submoduleMask = subMask;
        _serverModules[i].level = (level < kLevelCount) ? kLevelNames[level] : "inherit";

        // Update per-module level combo (kLevelDisplay maps directly to LoggerLevel enum values)
        if (level < kLevelCount)
            _moduleLevelCombos[i]->setCurrentIndex(level);

        // Update Settings tree checkboxes
        auto* moduleItem = _moduleTree->topLevelItem(i);
        if (!moduleItem) continue;

        bool allChecked = true, noneChecked = true;
        for (int s = 0; s < moduleItem->childCount(); s++)
        {
            bool on = enabled && (subMask & (1u << s));
            moduleItem->child(s)->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
            markPending(moduleItem->child(s), false);
            if (on) noneChecked = false; else allChecked = false;
        }

        if (!enabled || noneChecked)
        {
            moduleItem->setCheckState(0, Qt::Unchecked);
            totalModulesOff++;
        }
        else if (allChecked)
        {
            moduleItem->setCheckState(0, Qt::Checked);
            totalModulesOn++;
        }
        else
        {
            moduleItem->setCheckState(0, Qt::PartiallyChecked);
            totalModulesOn++;  // partial counts as "some on"
        }
        markPending(moduleItem, false);
    }

    _hasServerState = true;
    updateStatusColumn();
    _hasPendingChanges = false;
    updateButtons();
    _statusLabel->setText("SHM synced");
    _suppressSignals = false;

    // Update Settings select-all checkbox to reflect current state
    updateSettingsSelectAll();
}

void CategoryPanel::onRefresh()
{
    if (_apiClient)
        _apiClient->fetchLogging(_emulatorId);
}

// =============================================================================
// Select-all checkbox state sync
// =============================================================================

void CategoryPanel::updateSettingsSelectAll()
{
    int total = _moduleTree->topLevelItemCount();
    int checkedCount = 0;
    for (int i = 0; i < total; i++)
    {
        auto state = _moduleTree->topLevelItem(i)->checkState(0);
        if (state == Qt::Checked)
            checkedCount++;
        else if (state == Qt::PartiallyChecked)
            checkedCount++;  // any enabled counts
    }

    // Block signals to avoid re-triggering the toggle slot
    QSignalBlocker blocker(_settingsSelectAll);
    if (checkedCount == 0)
        _settingsSelectAll->setCheckState(Qt::Unchecked);
    else if (checkedCount == total)
    {
        // Check if ALL are fully checked (not partial)
        bool allFull = true;
        for (int i = 0; i < total; i++)
            if (_moduleTree->topLevelItem(i)->checkState(0) != Qt::Checked)
            { allFull = false; break; }
        _settingsSelectAll->setCheckState(allFull ? Qt::Checked : Qt::PartiallyChecked);
    }
    else
        _settingsSelectAll->setCheckState(Qt::PartiallyChecked);
}

void CategoryPanel::updateViewSelectAll()
{
    int total = _viewTree->topLevelItemCount();
    int checkedCount = 0;
    for (int i = 0; i < total; i++)
    {
        auto state = _viewTree->topLevelItem(i)->checkState(0);
        if (state == Qt::Checked || state == Qt::PartiallyChecked)
            checkedCount++;
    }

    QSignalBlocker blocker(_selectAllCheckbox);
    if (checkedCount == 0)
        _selectAllCheckbox->setCheckState(Qt::Unchecked);
    else if (checkedCount == total)
    {
        bool allFull = true;
        for (int i = 0; i < total; i++)
            if (_viewTree->topLevelItem(i)->checkState(0) != Qt::Checked)
            { allFull = false; break; }
        _selectAllCheckbox->setCheckState(allFull ? Qt::Checked : Qt::PartiallyChecked);
    }
    else
        _selectAllCheckbox->setCheckState(Qt::PartiallyChecked);
}

// =============================================================================
// Visual helpers
// =============================================================================

void CategoryPanel::markPending(QTreeWidgetItem* item, bool pending)
{
    QFont font = item->font(0);
    font.setBold(pending);
    item->setFont(0, font);
    item->setForeground(0, pending ? QColor("#D4A017") : QColor());
}

void CategoryPanel::updateButtons()
{
    _applyBtn->setEnabled(_hasPendingChanges);
    _cancelBtn->setEnabled(_hasPendingChanges);
}

void CategoryPanel::updateCategories(const QString& /*responseData*/) {}
