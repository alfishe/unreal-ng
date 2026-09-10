#pragma once

#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMutex>
#include <QPointer>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "debugger/debuggerwindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"
#include "emulator/guiemulatorcontext.h"
#include "emulator/soundmanager.h"
#include "logviewer/logwindow.h"
#include "menumanager.h"
#include "common/displayrefreshrate.h"
#include "statusbarmanager.h"
#include "toolbarmanager.h"
#include "tape/tapemanagerwindow.h"
#include "ui/intparametersdialog.h"
#include "ui_mainwindow.h"
#include "widgets/devicescreen.h"

#ifdef ENABLE_AUTOMATION
// Avoid name conflicts between Python and Qt "slot"
#undef slots
#include "automation/automation.h"
#define slots Q_SLOTS
#endif  // ENABLE_AUTOMATION

class AudioSettingsWidget;
class DockingManager;
#ifdef ENABLE_RECORDING
class VideoRecordingWidget;
#endif

QT_BEGIN_NAMESPACE
namespace Ui
{
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    virtual ~MainWindow() override;

    // Disable object copy
    MainWindow(const MainWindow&) = delete;
    MainWindow& operator=(const MainWindow&) = delete;

    // region <Slots>
private slots:
    void toggleEmulatorStartStop();
    void tryAdoptRemainingEmulator();
    void handleMessageScreenRefresh(int id, Message* message);
    void handleVideoModeChanged(int id, Message* message);
    void handleFileOpenRequest(int id, Message* message);
    void handleEmulatorStateChanged(int id, Message* message);
    void handleEmulatorInstanceDestroyed(int id, Message* message);
    void handleEmulatorInstanceCreated(int id, Message* message);
    void handleEmulatorSelectionChanged(int id, Message* message);
    void openFileDialog();
    void openSnapshotDialog();
    void openTapeDialog();
    void openDiskDialog();
    void openSpecificFile(const QString& filepath);
    void loadFile(const QString& filePath);
    void saveFileDialog();
    void saveFileDialogZ80();
    void saveDiskDialog();
    void saveDiskAsTRDDialog();
    void saveDiskAsSCLDialog();
    void saveDiskAsUDIDialog();
    void resetEmulator();
    void handleFullScreenShortcut();

    // Menu action handlers
    void handleStartEmulator();
    void handlePauseEmulator();
    void handleResumeEmulator();
    void handleStopEmulator();
    void handleSpeedMultiplierChanged(int multiplier);
    void handleTurboModeToggled(bool enabled);
    void handleTapeTrapsToggled(bool enabled);
    void handleTurboTapeToggled(bool enabled);
    void handleStepIn();
    void handleStepOver();
    void handleToolBarToggled(bool visible);
    void handleScaleRequested(int scale);
    void handleScreenshotRequested();
    void handleStatusBarToggled(bool visible);
    void handleDebuggerToggled(bool visible);
    void handleDebuggerVisibilityChanged(bool visible);
    void handleLogWindowToggled(bool visible);
    void handleTapeManagerToggled(bool visible);
    void handleImportAudioTapeRequested();  // tape-audio-bridge §7.3
    void handleIntParametersRequested();
    void handleAudioSettingsRequested();
    void handleOverscanModeToggled(bool enabled);
    void handleViewportChanged(int presetIndex);
    void handleMachineModelChangeRequested(const QString& modelShortName);

    // Toolbar (transport) handlers
    void handleStartOrResumeRequested();
    void handleRestartRequested();
#ifdef ENABLE_RECORDING
    void handleVideoRecordingRequested();
    void handleQuickRecord(const QString& presetName);
#endif
    void updateMenuStates();

    // Binding state handler
    void onBindingStateChanged(EmulatorStateEnum state);
    // endregion <Slots>

    // region <QWidget events override>
protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void moveEvent(QMoveEvent* event) override;
    void changeEvent(QEvent* event) override;

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;

    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    // endregion </QWidget events override>

protected:
    void updatePosition(QWidget* widget, QWidget* parent, float xscale, float yscale)
    {
        int w = parent->size().width();
        int h = parent->size().height();
        widget->move(
            QPoint(static_cast<int>(static_cast<float>(w) * xscale), static_cast<int>(static_cast<float>(h) * yscale)) -
            widget->rect().center());
    }

    void arrangeWindows();

    /// Resize the window so the emulator screen is shown at an integer scale of its
    /// native (viewport) size, plus menu / toolbar / status bar chrome
    void fitWindowToScreen(int scale);

    /// Switch the emulator's debug instrumentation (debug memory interface, breakpoint
    /// dispatch) on or off - it follows the debugger window's visibility
    void applyDebugInstrumentation(bool enabled);

    /// Query the refresh rate of the display this window is on and hand it to the
    /// emulator as the upper bound for turbo-mode rendering (re-run on screen change)
    void applyDisplayRefreshRate();
    void adjust(QEvent* event, const QPoint& delta = QPoint{});

private:
    // Save the last directory path to settings
    void saveLastDirectory(const QString& path);

    // Clean up automation resources
    void cleanupAutomation();

    // Unsubscribe from all message bus events
    void unsubscribeFromMessageBus();

    // Subscribe/unsubscribe from per-emulator-instance events
    void subscribeToPerEmulatorEvents();
    void unsubscribeFromPerEmulatorEvents();

    // Bind audio callback to emulator (audio device runs continuously)
    void bindEmulatorAudio(std::shared_ptr<Emulator> emulator);

    /// @brief Adopt an emulator as the active emulator for this window.
    /// This is the SINGLE point of emulator binding. All emulator adoption
    /// (UI-triggered, automation-triggered, or selection-changed) must go through here.
    /// Handles: binding, audio, screen, debugger, menu, and UI state.
    /// @param emulator The emulator to adopt
    void adoptEmulator(std::shared_ptr<Emulator> emulator);

    /// @brief Unbind from the currently adopted emulator without destroying it.
    /// Used when switching to a different emulator - old emulator keeps running headless.
    void unbindFromEmulator();

    /// @brief Release and destroy the currently adopted emulator.
    /// This is the SINGLE point of emulator destruction. Use for stop, destroy, or close.
    /// Handles: unbinding, audio cleanup, screen detach, debugger reset, UI state, and emulator destruction.
    void releaseEmulator();

    // Platform-specific initialization methods
    void initializePlatformMacOS();
    void initializePlatformWindows();
    void initializePlatformLinux();

    // Platform-specific window state handling methods
    void handleWindowStateChangeMacOS(Qt::WindowStates oldState, Qt::WindowStates newState);
    void handleWindowStateChangeWindows(Qt::WindowStates oldState, Qt::WindowStates newState);
    void handleWindowStateChangeLinux(Qt::WindowStates oldState, Qt::WindowStates newState);

    void handleFullScreenShortcutMacOS();
    void handleFullScreenShortcutWindows();
    void handleFullScreenShortcutLinux();

private:
    Ui::MainWindow* ui = nullptr;
    DebuggerWindow* debuggerWindow = nullptr;
    LogWindow* logWindow = nullptr;
    TapeManagerWindow* tapeManagerWindow = nullptr;
    DeviceScreen* deviceScreen = nullptr;
    QMutex lockMutex;
    QMutex _audioMutex;              // Protects audio operations from race conditions
    bool _audioInitialized = false;  // Tracks if audio device is initialized

#ifdef ENABLE_AUTOMATION
    Automation* _automation = nullptr;
#endif  // ENABLE_AUTOMATION

    EmulatorManager* _emulatorManager = nullptr;
    EmulatorBinding* m_binding = nullptr;  // Central state binding for UI
    AppSoundManager* _soundManager = nullptr;
    GUIEmulatorContext* _guiContext = nullptr;
    std::shared_ptr<Emulator> _emulator = nullptr;  // TODO: Remove after full binding migration
    uint32_t _lastFrameCount = 0;
    bool _switchingModel = false;  // True while model switch is in progress (prevents notification handler interference)

    QPoint _lastCursorPos;
    QPalette _originalPalette;

    bool _inHandler = false;
    bool _initialFitDone = false;  // Window sized to the screen once, on first show
    DisplayRefreshInfo _displayRefresh;  // Last queried refresh characteristics of our display

    // Stores window geometry before going fullscreen / maximized
    QRect _normalGeometry;
    QRect _maximizedGeometry;
    Qt::WindowStates _preFullScreenState = Qt::WindowNoState;
    bool _isFullScreen = false;
    bool _inTransitionToFullScreen = false;

    // Last directory used for file operations
    QString _lastDirectory;
    QString _lastSaveDirectory;

    DockingManager* _dockingManager = nullptr;
    MenuManager* _menuManager = nullptr;
    ToolBarManager* _toolBarManager = nullptr;
    StatusBarManager* _statusBarManager = nullptr;

    // Audio settings dialog (singleton, toggled via menu)
    QPointer<AudioSettingsWidget> _audioSettingsWidget;
#ifdef ENABLE_RECORDING
    QPointer<VideoRecordingWidget> _videoRecordingWidget;
#endif
};
