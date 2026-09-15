#pragma once

#include <QLabel>
#include <QObject>
#include <QStatusBar>
#include <QTimer>
#include <chrono>
#include <deque>
#include <memory>
#include <mutex>

#include "3rdparty/message-center/messagecenter.h"
#include "emulator/notifications.h"

class Emulator;
class MainWindow;
class MenuManager;
class StatusIndicator;

/// @brief StatusBarManager - device LEDs and FPS readout at the bottom of the window.
///
/// Mirrors the new-gui design: tape / disk / HDD / sound indicators plus an FPS
/// label as permanent widgets. Unlike the mockup, the LEDs reflect the active
/// emulator's real state, the sound LED toggles the core mute on click, and FPS
/// is measured from frame refreshes.
///
/// Floppy state is never polled: it is read once from the FDC when the status bar
/// binds to an emulator, then the FDC posts NC_FDD_STATE_CHANGED whenever the
/// selected drive, side, track, sector or motor changes (and disk insert / eject
/// events adjust the media flag). The cached FDDStateInfo drives the LED and tooltip.
class StatusBarManager : public QObject, public Observer
{
    Q_OBJECT

public:
    StatusBarManager(MainWindow* mainWindow, MenuManager* menuManager, QObject* parent = nullptr);
    ~StatusBarManager() override;

    /// MessageCenter observer callbacks (any thread)
    void handleFDDStateChanged(int id, Message* message);  // NC_FDD_STATE_CHANGED
    void handleFDDDiskInserted(int id, Message* message);  // NC_FDD_DISK_INSERTED
    void handleFDDDiskEjected(int id, Message* message);   // NC_FDD_DISK_EJECTED
    void handleSystemReset(int id, Message* message);      // NC_SYSTEM_RESET
    void handleCPUFreqChanged(int id, Message* message);   // NC_CPU_FREQ_CHANGED

    /// Drop the FPS measurement window; call whenever the emulator's frame counter is
    /// discontinuous (reset, snapshot load, time-travel seek) so a jump is not read as speed
    Q_INVOKABLE void resetFpsMeasurement();

    /// The emulator whose devices are shown (nullptr when none is adopted)
    void setActiveEmulator(std::shared_ptr<Emulator> emulator);

    /// Called on every frame refresh event (any thread) with the emulator's own frame
    /// counter. The counter is timestamped here, at arrival, so the FPS readout divides
    /// a frame delta by the time between those two frames rather than by a GUI timer
    /// interval - an integer frame count over an unrelated ~1 s window can only show
    /// 48.0 or 49.0, never the 48.83 Hz a Pentagon actually runs at. In turbo mode the
    /// counter is the emulated frame count, so the readout is the true emulated rate.
    void notifyFrameRendered(uint32_t emulatorFrameCounter);

    // User preference (View -> Status bar). Persisted in QSettings.
    void setVisibleByUser(bool visible);
    bool isVisibleByUser() const { return _visibleByUser; }
    void restoreSettings();
    void saveSettings() const;

    // Full screen hides the status bar regardless of the preference; restore brings it back
    void hideForFullScreen();
    void restoreVisibility();

private slots:
    void refresh();
    void toggleSound();
    void applyFddState(FDDStateInfo state);  // GUI thread: update cache, LED and tooltip
    void applyDiskMediaChange(uint8_t driveId, bool inserted);
    void updateDiskToolTip();
    void updateFpsToolTip(std::shared_ptr<Emulator> emulator);
    void updateCpuFreqToolTip(EmulatorContext* context);

private:
    MainWindow* _mainWindow;
    MenuManager* _menuManager;
    QStatusBar* _statusBar = nullptr;
    std::weak_ptr<Emulator> _emulator;

    StatusIndicator* _tape = nullptr;
    StatusIndicator* _disk = nullptr;
    StatusIndicator* _hdd = nullptr;
    StatusIndicator* _sound = nullptr;
    QLabel* _cpuFreq = nullptr;
    QLabel* _fps = nullptr;

    QTimer _pollTimer;

    FDDStateInfo _fddState;
    bool _fddStateValid = false;

    QString _modelName;          // Full machine model name of the active emulator
    double _measuredFps = 0.0;   // Last measured emulated frames per second

    /// Frame-aligned sample: emulator frame counter and its arrival time
    struct FrameSample
    {
        uint32_t counter = 0;
        std::chrono::steady_clock::time_point time;
    };
    std::mutex _frameSampleMutex;
    FrameSample _latestFrame;            // Written by notifyFrameRendered (emulator thread)
    bool _haveLatestFrame = false;
    std::deque<FrameSample> _fpsWindow;  // One sample per ~1 s, oldest first (GUI thread)
    qint64 _lastFpsSampleMs = 0;

    bool _visibleByUser = true;
};
