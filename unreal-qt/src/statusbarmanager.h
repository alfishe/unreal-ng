#pragma once

#include <QLabel>
#include <QObject>
#include <QStatusBar>
#include <QTimer>
#include <atomic>
#include <memory>

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

    /// The emulator whose devices are shown (nullptr when none is adopted)
    void setActiveEmulator(std::shared_ptr<Emulator> emulator);

    /// Called on every frame refresh event (any thread) with the emulator's own frame
    /// counter, so the FPS readout reports emulated frames per second - in turbo mode
    /// that is far above the ~50 refreshes per second the screen actually repaints
    void notifyFrameRendered(uint32_t emulatorFrameCounter)
    {
        _frameCounter.store(emulatorFrameCounter, std::memory_order_relaxed);
    }

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

private:
    MainWindow* _mainWindow;
    MenuManager* _menuManager;
    QStatusBar* _statusBar = nullptr;
    std::weak_ptr<Emulator> _emulator;

    StatusIndicator* _tape = nullptr;
    StatusIndicator* _disk = nullptr;
    StatusIndicator* _hdd = nullptr;
    StatusIndicator* _sound = nullptr;
    QLabel* _fps = nullptr;

    QTimer _pollTimer;

    FDDStateInfo _fddState;
    bool _fddStateValid = false;

    QString _modelName;          // Full machine model name of the active emulator
    double _measuredFps = 0.0;   // Last measured emulated frames per second
    std::atomic<uint32_t> _frameCounter{0};  // Latest emulator frame counter seen
    uint32_t _lastFrameCounter = 0;
    bool _haveFrameSample = false;
    qint64 _lastFpsSampleMs = 0;

    bool _visibleByUser = true;
};
