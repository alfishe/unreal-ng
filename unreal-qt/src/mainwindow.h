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
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

#include "3rdparty/message-center/messagecenter.h"
#include "common/modulelogger.h"
#include "debugger/debuggerwindow.h"
#include "emulator/emulator.h"
#include "emulator/emulatorbinding.h"
#include "emulator/emulatormanager.h"
#include "emulator/zxpoly/zxpolygroup.h"
#include "emulator/guiemulatorcontext.h"
#include "emulator/media/mediatargets.h"
#include "emulator/soundmanager.h"
#include "logviewer/logwindow.h"
#include "menumanager.h"
#include "common/displayrefreshrate.h"
#include "statusbarmanager.h"
#include "toolbarmanager.h"
#include "tape/tapemanagerwindow.h"
#include "media/mediapanelwindow.h"
#include "network/networkwindow.h"
#include "debugger/vdac2/ft812debugwindow.h"
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
class MidiActivityWindow;
class SlotChangeController;
class SlotsWindow;
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

/// Where an emulator instance adopted by the main window came from
enum class EmulatorOrigin
{
    CreatedByGui,  ///< This window created it (startup, model switch): user preferences apply
    Adopted        ///< Created elsewhere (WebAPI / MCP / CLI), picked up by the GUI: left as it is
};

class MainWindow : public QMainWindow, public Observer
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

    /// Where a file to open comes from: it decides how a question is asked and a refusal is told
    enum class LoadOrigin
    {
        Interactive,  ///< a menu or a dialog: a slot chooser at the cursor, a refusal in a message box
        Drop,         ///< drag and drop: the chooser at the cursor, the refusal in the status bar (the drag showed it)
        Unattended,   ///< the command line, the CLI's open: no question (the chooser's first entry), the log
    };

    /// A file named on the command line. zxpolyModel: machine of a ZX-Poly group
    /// (skips the model question); a .trd/.scl with it boots as a ZX-Poly disk
    void openFromCommandLine(const QString& filePath, const QString& zxpolyModel);
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
    void openZXPolyDialog();

    void openTapeDialog();
    void openDiskDialog();
    void openSpecificFile(const QString& filepath);
    /// Open a file by type. mountOnly: a disk is mounted without the TR-DOS autostart (Shift+drop)
    void loadFile(const QString& filePath, bool mountOnly = false, LoadOrigin origin = LoadOrigin::Interactive);
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
    void handleContentionToggled(bool enabled);
    void handleFrontPanelTurboToggled(bool on);
    void handleFrontPanelCpmToggled(bool on);
    void handleStepIn();
    void handleStepOver();
    void handleToolBarToggled(bool visible);
    void handleScaleRequested(int scale);
    void handleScreenshotRequested();
    void handleSaveScreenshotRequested();
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
    void handleMediaPanelToggled(bool visible);
    void handleNetworkWindowToggled(bool visible);
    void handleSlotsWindowToggled(bool visible);
    void handleMidiActivityToggled(bool visible);
    void handleFt812DebugToggled(bool visible);
    /// Dock the FT812 Debug window level with the picture (opening: also when undocked)
    void placeFt812DebugWindow(bool opening);
    void handleImportAudioTapeRequested();  // tape-audio-bridge §7.3
    void handleIntParametersRequested();
    void handleAudioSettingsRequested();
    void handleTemporalEffectsRequested();
    void handleHudSettingsRequested();
    void handleOverscanModeToggled(bool enabled);
    void handleViewportChanged(int presetIndex);
    void handleMachineModelChangeRequested(const QString& modelShortName);
    /// Replace the running machine by `modelName` / `ramSize` (the media follow);
    /// no question asked. False when it did not happen (the user was told why)
    bool switchMachineModel(const std::string& modelName, uint32_t ramSize);
    /// Play an RZX recording: another model is replaced by the recording's first
    /// (as for an SZX); false when it did not start (the user was told why)
    bool playRzxFile(const std::string& file);
    void handleStopRzxRequested();
    void handleZXPolyConfigurationRequested(const QString& configurationName);

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
    /// A medium (floppy, tape, hard disk, card, CD) into the slot the core's plan
    /// names (media-drop-targets design §4-§6): `slot` when the user picked one,
    /// else the plan's default, else the slot chooser (the call ends there and
    /// comes back with the pick); unattended callers take the first entry
    void placeMedium(const QString& filePath, const FileClass& fileClass, bool mountOnly, LoadOrigin origin,
                     const std::string& slot = {});
    void refuseFile(const QString& filePath, const QString& reason, LoadOrigin origin);
    /// Several dropped files: floppy images go to the drives in order (A first,
    /// booted last), anything else takes the first file only
    void dropFiles(const QStringList& paths, bool mountOnly);
    /// While a file is dragged over the window: the highlight; nothing takes it:
    /// the red refusal over the screen at once; several slots take it: the drop
    /// zones after a 1.5 s hold
    void showDropVerdict(const QString& filePath);
    void clearDropVerdict();
    void onDropDragLeft();
    /// The screen area in global coordinates (where the overlay goes)
    QRect dropArea() const;
    /// File > Insert Medium...: a file, then the slot chooser with every slot that takes it
    void insertMediumDialog();

    /// The slot chooser / drop zones / refusal over the screen
    class DropTargetOverlay* _dropOverlay = nullptr;
    QTimer _dropHoldTimer;  ///< 1.5 s hold before the drop zones appear
    QString _dragPath;      ///< the file being dragged over the window
    bool _dragActive = false;
    /// The placement waiting for the chooser's answer
    struct PendingPlacement
    {
        QString path;
        bool mountOnly = false;
        LoadOrigin origin = LoadOrigin::Interactive;
        bool active = false;
    } _pendingPlacement;

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
    /// @param origin CreatedByGui when this window created the instance - only
    ///        then are the user's saved sound preferences applied; an instance
    ///        created through automation keeps its own values (Adopted)
    void adoptEmulator(std::shared_ptr<Emulator> emulator, EmulatorOrigin origin);

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
    MediaPanelWindow* mediaPanelWindow = nullptr;
    NetworkWindow* networkWindow = nullptr;
    SlotsWindow* _slotsWindow = nullptr;                   // Machine -> Slots (ZX-bus slots)
    MidiActivityWindow* _midiActivityWindow = nullptr;     // Tools -> MIDI Activity
    SlotChangeController* _slotChangeController = nullptr;   // plan, confirm, restart, Undo (every slot change)
    Ft812DebugWindow* _ft812DebugWindow = nullptr;  // Debug -> FT812 Debug (VDAC2 machines only)
    DeviceScreenWrapper* _screenWrapper = nullptr;
    HudOverlayWrapper* _hudWrapper = nullptr;
    std::shared_ptr<HudModel> _hudModel;
    bool _autostartDisks = true;      // TR-DOS disk autostart on open (persisted in settings)
    std::string _nextEmulatorModel;   // Model for the next auto-started emulator (empty = default); consumed once
    uint32_t _nextEmulatorRamKb = 128;  // Its RAM size in KB; reset with the model
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

    /// Instances the manager announced as destroyed (NC_EMULATOR_INSTANCE_DESTROYED,
    /// recorded on the MessageCenter worker before the instance is released). An
    /// adoption queued before the removal may run after it: adoptEmulator()
    /// refuses these instead of binding the UI to a released instance
    std::mutex _destroyedEmulatorIdsMutex;
    std::unordered_set<std::string> _destroyedEmulatorIds;
    /// Id of the instance the UI finished adopting (guarded by the mutex above).
    /// The destroy notice runs on the MessageCenter worker and reads this, never
    /// _emulator (written by the UI thread); adoptEmulator() commits it under the
    /// same lock it checks the destroyed set with, so a removal racing an
    /// adoption is seen by exactly one side
    std::string _adoptedEmulatorId;
    void setAdoptedEmulatorId(const std::string& id);
    bool isEmulatorGone(const std::shared_ptr<Emulator>& emulator);
    uint32_t _lastFrameCount = 0;
    /// model: a ZX-Poly configuration name or a base model; empty asks. An empty
    /// filePath starts the bare machine
    void startZXPoly(const QString& filePath, const QString& model = QString());
    void releaseZXPolyGroup();
    bool attachScreenToZXPolyDisplay();
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
