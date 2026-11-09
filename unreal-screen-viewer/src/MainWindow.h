#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

class WebAPIClient;
class EmulatorList;
class ScreenViewer;
class ModeToolbar;
class QLabel;
class QSplitter;

namespace guicontrol
{
class Host;
class ScreenBinding;
}  // namespace guicontrol

/**
 * @brief Main application window for the Screen Viewer
 * 
 * Provides a split-view interface with:
 * - Left panel: List of discovered emulator instances + mode toolbar
 * - Right panel: Screen display for selected emulator
 * - Status bar: Connection status and selected emulator info
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    /// @p endpoint "host:port" of the WebAPI to watch (--connect), not kept in the settings; empty: the settings'
    explicit MainWindow(const QString& endpoint = QString(), QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void onEmulatorSelected(const QString& emulatorId);
    void onEmulatorDeselected();
    void onConnectionStatusChanged(bool connected);
    void onRefreshClicked();
    void openSettings();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void connectSignals();
    void updateStatusBar();
    void saveSettings();
    void loadSettings();
    /// The GUI control plane (docs/emulator/design/gui-control): the window `main` and its view `main/screen`
    void startGuiControl();
    /// The WebAPI watched, as a URL
    QString endpointUrl() const;

    // UI Components
    QSplitter* _splitter = nullptr;
    EmulatorList* _emulatorList = nullptr;
    ScreenViewer* _screenViewer = nullptr;
    ModeToolbar* _modeToolbar = nullptr;
    QLabel* _statusLabel = nullptr;
    QLabel* _emulatorLabel = nullptr;

    // Backend
    std::unique_ptr<WebAPIClient> _webApiClient;
    QTimer* _refreshTimer = nullptr;

    // State
    QString _selectedEmulatorId;
    bool _isConnected = false;
    QString _webApiHost = "localhost";
    int _webApiPort = 8090;
    bool _endpointGiven = false;  // --connect: the settings keep their own endpoint

    std::unique_ptr<guicontrol::Host> _guiHost;
    guicontrol::ScreenBinding* _screenControl = nullptr;  // a child of the screen widget
};

