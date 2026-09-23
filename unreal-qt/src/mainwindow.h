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
#include <functional>
#include <vector>

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
#include "widgets/devicescreenwrapper.h"

#ifdef ENABLE_AUTOMATION
// Avoid name conflicts between Python and Qt "slot"
#undef slots
#include "automation/automation.h"
#define slots Q_SLOTS
#endif  // ENABLE_AUTOMATION

class AudioSettingsWidget;
class HudOverlayWrapper;
class HudModel;
class TtdWidget;
#ifdef ENABLE_RECORDING
class VideoRecordingWidget;
class RecordingWidget;
#endif
class DockingManager;

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
    // Public screen refresh helper (called by TtdWidget on scrub)
    void refreshViewport();
    std::shared_ptr<Emulator> activeEmulator() const { return _emulator; }

    // region <Slots>
private slots:
    void toggleEmulatorStartStop();
    void tryAdoptRemainingEmulator();
    void handleTtdToggled(bool visible);
    void adjustWindowHeightForTtdWidget();
#ifdef ENABLE_RECORDING
    void adjustWindowHeightForRecordingWidget();
    void openAdvancedRecordingDialog();
#endif
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
    /// Open a file by type. mountOnly: a disk is mounted without the TR-DOS autostart (Shift+drop)
    void loadFile(const QString& filePath, bool mountOnly = false);
    void saveFileDialog();
    void saveFileDialogZ80();
    void saveDiskDialog();
    void saveDiskAsTRDDialog();
    void saveDiskAsSCLDialog();
    void saveDiskAsUDIDialog();
    void resetEmulator();
    void requestMni();  // Machine -> MNI: NMI + service monitor (plain NMI on other models)
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
    void handleFastDiskToggled(bool enabled);
    void handleAutostartDisksToggled(bool enabled);
    void handleStepIn();
    void handleStepOver();
    void handleToolBarToggled(bool visible);
    void handleScaleRequested(int scale);
    void handleScreenshotRequested();
    void handleStatusBarToggled(bool visible);
    void handleHudOverlayToggled(bool visible);
    void handleGpuAccelerationToggled(bool enabled);
    void handleCrtEffectsToggled(bool enabled);
    void handleCrtProfileChanged(int profileIndex);
    void handleTemporalBlendingToggled(bool enabled);
    void handleDebuggerToggled(bool visible);
    void handleDebuggerVisibilityChanged(bool visible);
    void handleLogWindowToggled(bool visible);
    void handleTapeManagerToggled(bool visible);
    void handleImportAudioTapeRequested();  // tape-audio-bridge §7.3
    void handleIntParametersRequested();
    void handleAudioSettingsRequested();
    void handleTemporalEffectsRequested();
    void handleHudSettingsRequested();
    void handleOverscanModeToggled(bool enabled);
    void handleViewportChanged(int presetIndex);
    void handleMachineModelChangeRequested(const QString& modelShortName);

    // Toolbar (transport) handlers
    void handleStartOrResumeRequested();
    void handleRestartRequested();
#ifdef ENABLE_RECORDING
    void handleVideoRecordingRequested();
    void handleVideoRecordingToggled(bool visible);
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

    // region <Mutually Exclusive Top Panels>
    /// Structure representing a top-docked banner widget participating in mutual exclusion.
    /// Only one such panel can be expanded / visible at any time. Opening any panel in this
    /// collection automatically closes all other active panels in the group.
    struct ExclusiveTopPanel
    {
        QWidget* widget = nullptr;
        std::function<bool()> isVisibleByUser;
        std::function<void(bool)> setVisibleByUser;
    };

    /// Register a panel in the mutually exclusive top banners collection.
    void registerExclusiveTopPanel(QWidget* widget,
                                   std::function<bool()> isVisibleByUser,
                                   std::function<void(bool)> setVisibleByUser);

    /// Helper to register a panel and automatically connect its visibilityChanged(bool) signal.
    template <typename TPanel>
    void registerExclusiveTopPanelHelper(TPanel* panel)
    {
        if (!panel)
            return;

        registerExclusiveTopPanel(panel,
            [panel]() { return panel->isVisibleByUser(); },
            [panel](bool visible) { panel->setVisibleByUser(visible); }
        );

        connect(panel, &TPanel::visibilityChanged, this, [this, panel](bool visible) {
            if (visible)
            {
                closeOtherExclusiveTopPanels(panel);
            }
        });
    }

    /// Close all mutually exclusive top panels except the specified one.
    void closeOtherExclusiveTopPanels(QWidget* exceptWidget = nullptr);
    // endregion </Mutually Exclusive Top Panels>

private:
    Ui::MainWindow* ui = nullptr;
    DebuggerWindow* debuggerWindow = nullptr;
    LogWindow* logWindow = nullptr;
    TapeManagerWindow* tapeManagerWindow = nullptr;
    DeviceScreenWrapper* _screenWrapper = nullptr;
    HudOverlayWrapper* _hudWrapper = nullptr;
    std::shared_ptr<HudModel> _hudModel;
    bool _autostartDisks = true;      // TR-DOS disk autostart on open (persisted in settings)
    std::string _nextEmulatorModel;   // Model for the next auto-started emulator (empty = default); consumed once
    bool _hudOverlayVisible = false;  // Session-only HUD visibility state (default off)
    QMutex lockMutex;
    QMutex _audioMutex;              // Protects audio operations from race conditions
    bool _audioInitialized = false;  // Tracks if audio device is initialized
    bool _audioStarted = false;       // Tracks if audio device is started (on-demand)

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
    TtdWidget* _ttdWidget = nullptr;
    int _lastTtdWidgetHeight = 0;
    bool _adjustingTtdHeight = false;
    StatusBarManager* _statusBarManager = nullptr;

    // Audio settings dialog (singleton, toggled via menu)
    QPointer<AudioSettingsWidget> _audioSettingsWidget;
#ifdef ENABLE_RECORDING
    RecordingWidget* _recordingWidget = nullptr;
    int _lastRecordingWidgetHeight = 0;
    bool _adjustingRecordingHeight = false;
    QPointer<VideoRecordingWidget> _videoRecordingWidget;
#endif

    // Collection of top-docked panels participating in mutual exclusion
    std::vector<ExclusiveTopPanel> _exclusiveTopPanels;
    bool _closingOtherPanels = false;
};
