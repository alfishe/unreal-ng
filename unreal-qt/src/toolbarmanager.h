#pragma once

#include <QAction>
#include <QObject>
#include <QToolBar>
#include <chrono>
#include <memory>

class Emulator;
class MainWindow;
class MenuManager;

/// @brief ToolBarManager - the transport toolbar under the menu bar.
///
/// Mirrors the new-gui design: Start / Pause / Restart, Overscan / Full Screen,
/// and Recording. Overscan (Pentagon only, hidden for other models), Full Screen
/// and Recording reuse the MenuManager actions so their state stays in sync with
/// the menus; the transport buttons are toolbar-owned and updated from the active
/// emulator's state.
class ToolBarManager : public QObject
{
    Q_OBJECT

public:
    ToolBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent = nullptr);

    /// Refresh enabled / checked states from the active emulator (queried directly, no duplication)
    void updateState(std::shared_ptr<Emulator> activeEmulator);

    // User preference (View -> Toolbar). Persisted in QSettings.
    void setVisibleByUser(bool visible);
    bool isVisibleByUser() const { return _visibleByUser; }
    void restoreSettings();
    void saveSettings() const;

    // Full screen hides the toolbar regardless of the preference; restore brings it back
    void hideForFullScreen();
    void restoreVisibility();

    QAction* ttdAction() const { return _ttdAction; }
#ifdef ENABLE_RECORDING
    QAction* recordAction() const { return _recordAction; }
#endif

    void setVideoRecordingActive(bool active);
    void setTtdRecordingActive(bool active);
    void updateRecordingStates();

    bool isVideoRecordingActive() const { return _videoRecordingActive; }
    bool isTtdRecordingActive() const { return _ttdRecordingActive; }

signals:
    void startOrResumeRequested();
    void pauseRequested();
    void restartRequested();
    void ttdToggled(bool visible);
#ifdef ENABLE_RECORDING
    void recordingToggled(bool visible);
#endif

private slots:
    void onBreathingTick();

private:
    void startBreathingAnimationIfNeeded();
    void stopBreathingAnimationIfIdle();
    QIcon createBreathingRecordIcon(qreal intensity, qreal bloom);
    QIcon createBreathingTtdIcon(qreal intensity, qreal bloom);

    bool isVideoRecording() const;
    bool isTtdRecording() const;
    std::shared_ptr<Emulator> getActiveEmulator() const;
    void updateActiveTooltips();

    MainWindow* _mainWindow;
    MenuManager* _menuManager;
    QToolBar* _toolBar = nullptr;

    QAction* _startAction = nullptr;
    QAction* _pauseAction = nullptr;
    QAction* _restartAction = nullptr;
    QAction* _ttdAction = nullptr;
#ifdef ENABLE_RECORDING
    QAction* _recordAction = nullptr;
#endif

    bool _visibleByUser = true;
    bool _videoRecordingActive = false;
    bool _ttdRecordingActive = false;
    QTimer* _breathingTimer = nullptr;
    qreal _breathingPhase = 0.0;
    int _tickCount = 0;
    std::chrono::steady_clock::time_point _lastTooltipUpdateTime{};

    QIcon _normalRecordIcon;
    QIcon _normalTtdIcon;

    std::weak_ptr<Emulator> _activeEmulator;
};
