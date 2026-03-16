#pragma once

#include <QWidget>
#include <QTreeWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QJsonObject>
#include <QTabWidget>
#include <QMap>
#include <QElapsedTimer>

class QLabel;
class QTimer;
class WebAPIClient;

/// Module control panel with two tabs:
/// - Settings: emulator-side module enable/disable/level control via WebAPI
/// - View: display filter checkboxes + activity indicators
class CategoryPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CategoryPanel(QWidget* parent = nullptr);

    /// Set the shared WebAPI client and emulator ID
    void setApiClient(WebAPIClient* client, const QString& emulatorId);

    /// Parse JSON logging state from WebAPI GET /logging response
    void updateFromLoggingState(const QJsonObject& state);

public slots:
    /// Activity indicator: called when a log message is received for a module
    void onModuleActivity(int moduleId, int submoduleId);
    void onLoggerStateReceived(uint32_t moduleMask, QVector<uint16_t> submoduleMasks,
                               QVector<uint8_t> moduleLevels, uint8_t globalLevel);

    /// Legacy emulog category list
    void updateCategories(const QString& responseData);

signals:
    // Display filter: user toggled module visibility in View tab
    void visibilityChanged(int moduleId, bool visible);
    void submoduleVisibilityChanged(int moduleId, uint16_t visibleMask);

    // Filter request: user double-clicked a module to filter (legacy)
    void filterRequested(int moduleId, int submoduleId);

    // Legacy emulog signals
    void enableRequested(const QString& pattern);
    void disableRequested(const QString& pattern);
    void levelChangeRequested(const QString& pattern, uint8_t level);
    void refreshRequested();

private slots:
    // Settings tab
    void onApply();
    void onCancel();
    void onRefresh();
    void onSettingsItemChanged(QTreeWidgetItem* item, int column);
    void onItemDoubleClicked(QTreeWidgetItem* item, int column);
    void onSettingsSelectAllToggled(bool checked);

    // View tab
    void onViewItemChanged(QTreeWidgetItem* item, int column);
    void onActivityTimerTick();
    void onSelectAllToggled(bool checked);

private:
    void buildSettingsTree();
    void buildViewTree();
    void refreshViewTree();    // rebuild View tree from enabled modules
    void markPending(QTreeWidgetItem* item, bool pending);
    void updateButtons();
    void updateStatusColumn();
    void setActivityDot(QTreeWidgetItem* item, const QElapsedTimer& timer);
    void updateSettingsSelectAll();
    void updateViewSelectAll();

    WebAPIClient* _apiClient = nullptr;
    QString _emulatorId = "0";

    // Tab widget
    QTabWidget* _tabWidget = nullptr;

    // === Settings tab ===
    QTreeWidget* _moduleTree = nullptr;
    QComboBox*   _globalLevelCombo = nullptr;
    QPushButton* _applyBtn = nullptr;
    QPushButton* _cancelBtn = nullptr;
    QLabel*      _statusLabel = nullptr;
    QCheckBox*   _settingsSelectAll = nullptr;
    QComboBox*   _moduleLevelCombos[12] = {};

    // === View tab ===
    QTreeWidget* _viewTree = nullptr;
    QCheckBox*   _selectAllCheckbox = nullptr;
    QTimer*      _activityTimer = nullptr;

    // Activity tracking (indexed 0-11 for modules 1-12)
    int          _activityCounts[12] = {};
    QElapsedTimer _lastActivity[12];

    // Per-submodule activity tracking [module][submodule]
    static constexpr int kMaxSubmodules = 16;
    int          _subActivityCounts[12][16] = {};
    QElapsedTimer _subLastActivity[12][16];

    // Server state
    struct ModuleState {
        bool enabled = false;
        uint16_t submoduleMask = 0;
        QString level = "inherit";
    };
    ModuleState _serverModules[12] = {};  // MODULE_COUNT - 1 (skip MODULE_NONE)
    QString _serverGlobalLevel = "trace";
    bool _hasServerState = false;

    // MODULE_COUNT - 1 (skip MODULE_NONE). Verified by static_assert in CategoryPanel.cpp.
    static constexpr int kModuleCount = 12;
    static const char* kModuleApiNames[];

    bool _hasPendingChanges = false;
    bool _suppressSignals = false;
};
