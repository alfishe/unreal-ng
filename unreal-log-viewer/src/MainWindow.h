#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

class WebAPIClient;
class EmulatorList;
class LogViewWidget;
class CategoryPanel;
class FramebufferWidget;
class HeatmapWidget;
class TraceWidget;
class ChipStateWidget;
class QLabel;
class QSplitter;
class QLineEdit;
class QComboBox;
class QToolBar;
class QAction;
class QTabWidget;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onEmulatorSelected(const QString& emulatorId);
    void onEmulatorDeselected();
    void onConnectionStatusChanged(bool connected);
    void onRefreshClicked();
    void onStatsUpdated(uint64_t msgCount, uint64_t dropped);
    void onTabChanged(int index);
    void openSettings();

private:
    void setupUI();
    void setupToolBar();
    void setupMenuBar();
    void setupStatusBar();
    void connectSignals();
    void updateStatusBar();
    void saveSettings();
    void loadSettings();

    // UI Components
    QSplitter* _mainSplitter = nullptr;
    QSplitter* _leftSplitter = nullptr;
    EmulatorList* _emulatorList = nullptr;
    CategoryPanel* _categoryPanel = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _statsLabel = nullptr;
    QLabel* _shmLabel = nullptr;

    // Tab widget + visualization tabs
    QTabWidget* _tabWidget = nullptr;
    LogViewWidget* _logView = nullptr;
    FramebufferWidget* _framebufferView = nullptr;
    HeatmapWidget* _heatmapView = nullptr;
    TraceWidget* _traceView = nullptr;
    ChipStateWidget* _chipView = nullptr;

    // Toolbar
    QToolBar* _toolbar = nullptr;
    QAction* _autoScrollAction = nullptr;

    // Backend
    std::unique_ptr<WebAPIClient> _webApiClient;
    QTimer* _refreshTimer = nullptr;

    // State
    QString _selectedEmulatorId;
    bool _isConnected = false;
    QString _webApiHost = "localhost";
    int _webApiPort = 8090;
};
