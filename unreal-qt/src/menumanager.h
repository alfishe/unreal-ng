#pragma once

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <memory>
#include <vector>

class Emulator;
class MainWindow;
struct TMemModel;

#include "3rdparty/message-center/messagecenter.h"

/// @brief MenuManager - Manages all menu-related functionality for the main window
///
/// This class creates and manages a comprehensive cross-platform menu system that provides
/// shortcuts to all emulator control functions.
class MenuManager : public QObject, public Observer
{
    Q_OBJECT

public:
    explicit MenuManager(MainWindow* mainWindow, QMenuBar* menuBar, QObject* parent = nullptr);
    virtual ~MenuManager();

    // Update menu states based on active emulator
    // Queries emulator directly - no state duplication!
    void updateMenuStates(std::shared_ptr<Emulator> activeEmulator);
    /// The TURBO Switch item follows the machine (CLI / WebAPI / TTD replay flip it too)
    void updateFrontPanelSwitches(const std::shared_ptr<Emulator>& activeEmulator);

    // Update machine model selection based on active emulator's model
    void updateMachineModelSelection(std::shared_ptr<Emulator> activeEmulator);

    // Set the current active emulator instance
    void setActiveEmulator(std::shared_ptr<Emulator> emulator);

    // Reset viewport selection to default (Full Overscan)
    void resetViewportSelection();

    // Sync the Tape Manager check state from the window's own close box
    // (setChecked does not re-emit triggered)
    void setTapeManagerChecked(bool checked);
    void setMediaPanelChecked(bool checked);
    /// The route as set ("AUTO" .. "BOTH"), the route in force, and whether a PS/2 controller is fitted
    void setHostKeyboardRoute(const QString& route, const QString& effective, bool ps2Controller);
    void setNetworkWindowChecked(bool checked);

    // Sync the Debug -> Debugger Window check state from the window's own show / hide
    // (setChecked does not re-emit triggered)
    void setDebuggerChecked(bool checked);

    // Sync the View -> Toolbar / Status Bar / HUD check state (setChecked does not re-emit triggered)
    void setToolBarChecked(bool checked);
    void setStatusBarChecked(bool checked);
    void setHudOverlayChecked(bool checked);
    /// Reflect the persisted "Autostart disks" preference in the Machine menu
    void setAutostartDisksChecked(bool checked);
    void setGpuAccelerationChecked(bool checked);
    void setGpuAccelerationAvailable(bool available);
    void setCrtEffectsChecked(bool checked);
    void setCrtEffectsEnabled(bool enabled);
    void setCrtProfile(int profileIndex);
    void populateCrtProfileMenu();
    void setTemporalBlendingChecked(bool checked);
    void setTemporalBlendingEnabled(bool enabled);

    // Actions shared with the transport toolbar (ToolBarManager). Their visible /
    // enabled / checked state is maintained by updateMenuStates()
    QAction* fullScreenAction() const { return _fullScreenAction; }
    QAction* overscanAction() const { return _overscanAction; }  // Pentagon only (hidden otherwise)
    QAction* mediaPanelAction() const { return _mediaPanelAction; }
    QMenu* viewMenu() const { return _viewMenu; }
#ifdef ENABLE_RECORDING
    QAction* videoRecordingAction() const { return _videoRecordingAction; }
#endif

    // Observer callback for emulator state changes
    void handleEmulatorStateChanged(int id, Message* message);
    void handleEmulatorInstanceCreated(int id, Message* message);
    void handleFDDDiskChanged(int id, Message* message);
    /// NC_FEATURE_CHANGED / NC_SPEED_CHANGED: TTD recording start/stop and speed changes
    void handleSpeedOrFeatureChanged(int id, Message* message);
#ifdef ENABLE_RECORDING
    void handleRecordingStateChanged(int id, Message* message);
#endif

signals:
    // Signal emitted when user requests to open a file
    void openFileRequested();
    void openSnapshotRequested();
    void openTapeRequested();
    void openDiskRequested();
    void insertMediumRequested();
    void openZXPolyRequested();  // ZX-Poly: four synchronized instances (.zxp / multiloader disk)
    void importAudioTapeRequested();  // tape-audio-bridge §7.3: WAV/FLAC/MP3 → .tzx/.tap
    void stopRzxRequested();          // stop RZX playback, the machine runs live
    void saveSnapshotRequested();
    void saveSnapshotZ80Requested();
    
    // Disk save signals
    void saveDiskRequested();       // Save to original path
    void saveDiskAsTRDRequested();  // Save As TRD
    void saveDiskAsSCLRequested();  // Save As SCL
    void saveDiskAsUDIRequested();  // Save As UDI (lossless)

    // Emulator control signals
    void startRequested();
    void pauseRequested();
    void resumeRequested();
    void resetRequested();
    void stopRequested();

    // Machine control signals
    void fastDiskToggled(bool enabled);
    void autostartDisksToggled(bool enabled);
    void contentionToggled(bool enabled);
    /// Machine > TURBO Switch: the Profi front-panel switch
    void frontPanelTurboToggled(bool on);
    /// Machine > CP/M Switch: the Profi v5 front-panel switch
    void frontPanelCpmToggled(bool on);
    /// Machine > Host Keyboard: "auto" | "matrix" | "ps2" | "both"
    void hostKeyboardRouteRequested(const QString& route);
    /// The Machine menu opens: the owner refreshes the route check marks
    void machineMenuAboutToShow();

    // Speed control signals
    void speedMultiplierChanged(int multiplier);
    void turboModeToggled(bool enabled);

    // Debug signals
    void stepInRequested();
    void stepOverRequested();

    // View signals
    void toolBarToggled(bool visible);
    void statusBarToggled(bool visible);
    void hudOverlayToggled(bool visible);
    void gpuAccelerationToggled(bool enabled);
    void crtEffectsToggled(bool enabled);
    void crtProfileChanged(int profileIndex);
    void temporalBlendingToggled(bool enabled);
    void debuggerToggled(bool visible);
    void logWindowToggled(bool visible);
    void tapeManagerToggled(bool visible);
    void mediaPanelToggled(bool visible);
    void networkWindowToggled(bool visible);
    void fullScreenToggled();
    void scaleRequested(int scale);  // View -> Scale -> Nx
    void overscanModeToggled(bool enabled);
    void viewportChanged(int presetIndex);

    // Machine signals
    void machineModelChangeRequested(const QString& modelShortName);
    void zxpolyConfigurationRequested(const QString& configurationName);  // Machine -> ZXPoly-48k / 128k / Pentagon
    void tapeTrapsToggled(bool enabled);
    void turboTapeToggled(bool enabled);
    void mniRequested();  // Machine -> MNI: NMI + service monitor (plain NMI on other models)

    // Tools signals
    void intParametersRequested();
    void audioSettingsRequested();
    void temporalEffectsRequested();
    void hudSettingsRequested();
    void screenshotRequested();
#ifdef ENABLE_RECORDING
    void videoRecordingRequested();
    void quickRecordRequested(const QString& presetName);
    void recordingStateChanged(bool isRecording);
#endif

private:
    void createFileMenu();
    void createEditMenu();
    void createViewMenu();
    void createRunMenu();
    void createMachineMenu();
    void createDebugMenu();
    void createToolsMenu();
    void createHelpMenu();

    // Platform-specific menu adjustments
    void applyPlatformSpecificSettings();

private:
    MainWindow* _mainWindow;
    QMenuBar* _menuBar;
    std::weak_ptr<Emulator> _activeEmulator;  // Weak reference - don't own the emulator

    // Menus
    QMenu* _fileMenu;
    QMenu* _editMenu;
    QMenu* _viewMenu;
    QMenu* _runMenu;
    QMenu* _machineMenu;
    QMenu* _debugMenu;
    QMenu* _toolsMenu;
    QMenu* _helpMenu;

    // File Menu Actions
    QAction* _openAction;
    QAction* _openSnapshotAction;
    QAction* _openTapeAction;
    QAction* _openDiskAction;
    QAction* _insertMediumAction;
    QAction* _openZXPolyAction;
    QAction* _importAudioTapeAction;
    QAction* _stopRzxAction = nullptr;  ///< enabled while an RZX recording plays
    QMenu* _saveSnapshotMenu;
    QAction* _saveSnapshotSNAAction;
    QAction* _saveSnapshotZ80Action;
    QMenu* _saveDiskMenu;
    QAction* _saveDiskAction;       // Save (to original path)
    QAction* _saveDiskTRDAction;    // Save as TRD
    QAction* _saveDiskSCLAction;    // Save as SCL
    QAction* _saveDiskUDIAction;    // Save as UDI
    QAction* _recentFilesAction;
    QAction* _exitAction;

    // Edit Menu Actions
    QAction* _preferencesAction;

    // View Menu Actions
    QAction* _toolBarAction;
    QAction* _statusBarAction;
    QAction* _hudOverlayAction = nullptr;
    QAction* _gpuAccelerationAction = nullptr;
    QAction* _crtEffectsAction = nullptr;
    QMenu* _crtProfileMenu = nullptr;
    QActionGroup* _crtProfileGroup = nullptr;
    QAction* _temporalBlendingAction = nullptr;
    QAction* _debuggerAction;
    QAction* _logWindowAction;
    QAction* _tapeManagerAction;
    QAction* _mediaPanelAction = nullptr;
    QAction* _networkWindowAction = nullptr;
    QAction* _fullScreenAction;
    QMenu* _scaleMenu = nullptr;
    std::vector<QAction*> _scaleActions;

    // Overscan Menu Actions (Pentagon only)
    QAction* _overscanAction;
    QMenu* _viewportMenu;
    QActionGroup* _viewportGroup;
    QAction* _viewportFullOverscanAction;
    QAction* _viewportSymmetricAction;
    QAction* _viewportStandardAction;
    QAction* _viewportScreenOnlyAction;

    // Run Menu Actions
    QAction* _startAction;
    QAction* _pauseAction;
    QAction* _resumeAction;
    QAction* _stopAction;
    QAction* _resetAction;
    QMenu* _speedMenu;
    QActionGroup* _speedGroup;
    QAction* _speed1xAction;
    QAction* _speed2xAction;
    QAction* _speed4xAction;
    QAction* _speed8xAction;
    QAction* _speed16xAction;
    QAction* _turboModeAction;

    // Machine Menu Actions
    QActionGroup* _machineModelGroup;
    std::vector<QAction*> _machineModelActions;
    std::vector<QAction*> _zxpolyConfigurationActions;  // data: the configuration name (ZXPolyGroup::Configurations)
    std::vector<QAction*> _machineVariantActions;  // machine variants (MachineVariants): TS-Conf + VDAC2
    QString _currentModelShortName;
    QAction* _tapeTrapsAction;
    QAction* _mniAction;
    QAction* _turboTapeAction;
    QAction* _fastDiskAction = nullptr;
    QAction* _autostartDisksAction = nullptr;
    QAction* _contentionAction = nullptr;
    QAction* _frontPanelTurboAction = nullptr;
    QAction* _frontPanelCpmAction = nullptr;
    QMenu* _hostKeyboardMenu = nullptr;
    QActionGroup* _hostKeyboardGroup = nullptr;

    // Debug Menu Actions
    QAction* _stepInAction;
    QAction* _stepOverAction;
    QAction* _stepOutAction;
    QAction* _runToCursorAction;
    QAction* _toggleBreakpointAction;
    QAction* _clearAllBreakpointsAction;
    QAction* _showBreakpointsAction;
    QAction* _showRegistersAction;
    QAction* _showMemoryAction;

    // Tools Menu Actions
    QAction* _settingsAction;
    QAction* _intParametersAction;
    QAction* _audioSettingsAction;
    QAction* _temporalEffectsAction = nullptr;
    QAction* _hudSettingsAction;
    QAction* _screenshotAction;
#ifdef ENABLE_RECORDING
    QAction* _videoRecordingAction;

    // Quick Record submenu
    QMenu* _quickRecordMenu = nullptr;
#endif

    // Help Menu Actions
    QAction* _aboutAction;
    QAction* _documentationAction;
    QAction* _keyboardShortcutsAction;
};
