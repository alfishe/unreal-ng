#pragma once

#include <QAction>
#include <QObject>
#include <QPointer>
#include <QToolBar>
#include <chrono>
#include <memory>

class Emulator;
class MainWindow;
class MenuManager;
class MouseCaptureController;

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

    /// The screen's host mouse capture (replaced with the screen wrapper): the
    /// mouse button shows its state and drives its gate
    void setMouseCapture(MouseCaptureController* capture);

    void setVideoRecordingActive(bool active);
    void setTtdRecordingActive(bool active);
    void updateRecordingStates();

    /// Automation sessions (execution arbiter design §6a): a bar of its own under the toolbar (the accent colour; a
    /// row of its own never overflows) says who holds the machine, from where, since when, with Take Back Control.
    /// After a take-back it says so, with Allow Automation Again while the client is in its re-take cooldown, until
    /// dismissed. The actions are also in the Run menu
    void setRemoteControl(bool active, const QString& brief, const QString& details, bool cooldowns,
                          qint64 sinceMs);
    /// The user took control back from `who`: the bar says so until dismissed or the cooldown ends
    void noteControlTakenBack(const QString& who);
    /// The bar stays usable while every other control is locked
    QList<QObject*> remoteControlWidgets() const;
    QAction* takeBackAction() const { return _takeBackAction; }
    QAction* allowAgainAction() const { return _allowAgainAction; }

    bool isVideoRecordingActive() const { return _videoRecordingActive; }
    bool isTtdRecordingActive() const { return _ttdRecordingActive; }

signals:
    void startOrResumeRequested();
    void pauseRequested();
    void restartRequested();
    void takeBackControlRequested();
    void allowRetakeRequested();
    void ttdToggled(bool visible);
#ifdef ENABLE_RECORDING
    void recordingToggled(bool visible);
#endif

private slots:
    void onBreathingTick();
    void onMouseActionTriggered();
    void refreshMouseAction();

private:
    void startBreathingAnimationIfNeeded();
    void stopBreathingAnimationIfIdle();
    QIcon createBreathingRecordIcon(qreal intensity, qreal bloom);
    QIcon createBreathingTtdIcon(qreal intensity, qreal bloom);

    bool isVideoRecording() const;
    bool isTtdRecording() const;
    std::shared_ptr<Emulator> getActiveEmulator() const;

    QToolBar* _remoteBar = nullptr;
    class QLabel* _remoteIcon = nullptr;
    class QLabel* _remoteLabel = nullptr;
    QString _remoteBrief;                    // the line before the ticking time
    QList<QPair<class QToolButton*, QAction*>> _remoteButtons;  // the bar's buttons and their actions
    QTimer* _remoteTick = nullptr;           // once a second while the bar shows a session, stopped when hidden
    qint64 _remoteSinceMs = 0;
    void updateRemoteElapsed();
    void updateRemoteTick();
    QAction* _takeBackAction = nullptr;
    QAction* _allowAgainAction = nullptr;
    QAction* _dismissAction = nullptr;
    QString _takenBackFrom;                // who the user took control back from, while its cooldown runs
    bool _remoteDismissed = false;
    void updateActiveTooltips();

    MainWindow* _mainWindow;
    MenuManager* _menuManager;
    QToolBar* _toolBar = nullptr;

    QAction* _startAction = nullptr;
    QAction* _pauseAction = nullptr;
    QAction* _restartAction = nullptr;
    QAction* _ttdAction = nullptr;
    QAction* _mouseAction = nullptr;          // indicator + gate (mouse-manager design §3.3)
    QAction* _mouseGateMenuAction = nullptr;  // View -> Host Mouse: the same gate as a menu item
    QAction* _mouseMatchMenuAction = nullptr; // View -> the captured mouse follows the host pointer's speed
    bool _mouseMatchHostPointer = true;       // persisted; applied to every new capture controller
    QString _mouseStatusMessage;              // the status bar hint shown while captured (cleared on release)
    QPointer<MouseCaptureController> _mouseCapture;
    bool _mouseGateOpen = true;               // persisted; applied to every new capture controller
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
