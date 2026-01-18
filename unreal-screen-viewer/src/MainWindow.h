#pragma once

#include <QMainWindow>
#include <QTimer>
#include <memory>

class WebAPIClient;
class EmulatorList;
class ScreenViewer;
class QLabel;
class QSplitter;

/**
 * @brief Main application window for the Screen Viewer
 * 
 * Provides a split-view interface with:
 * - Left panel: List of discovered emulator instances
 * - Right panel: Screen display for selected emulator
 * - Status bar: Connection status and selected emulator info
 */
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
    void openSettings();

private:
    void setupUI();
    void setupMenuBar();
    void setupStatusBar();
    void connectSignals();
    void updateStatusBar();

    // UI Components
    QSplitter* _splitter = nullptr;
    EmulatorList* _emulatorList = nullptr;
    ScreenViewer* _screenViewer = nullptr;
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
};
