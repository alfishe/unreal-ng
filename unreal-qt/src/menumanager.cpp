#include "menumanager.h"

#include <QActionGroup>
#include <QApplication>
#include <QMessageBox>
#include <set>

#include "widgets/crtprofiles.h"

#include "base/featuremanager.h"
#include "emulator/emulator.h"
#include "emulator/emulatormanager.h"
#include "emulator/platform.h"
#include "emulator/notifications.h"
#include "recordingmanager.h"
// Avoid Qt 'signals' macro conflict with WD1793State::signals member
#undef signals
#include "emulator/io/fdc/wd1793.h"
#include "emulator/io/fdc/fdd.h"
#define signals Q_SIGNALS
#include "mainwindow.h"

MenuManager::MenuManager(MainWindow* mainWindow, QMenuBar* menuBar, QObject* parent)
    : QObject(parent), _mainWindow(mainWindow), _menuBar(menuBar)
{
    createFileMenu();
    createEditMenu();
    createViewMenu();
    createRunMenu();
    createMachineMenu();
    createDebugMenu();
    createToolsMenu();
    createHelpMenu();

    applyPlatformSpecificSettings();

    // Set initial states (no emulator at startup)
    updateMenuStates(nullptr);

    // Subscribe to emulator state changes
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod stateCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleEmulatorStateChanged);
    messageCenter.AddObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    // Subscribe to emulator instance creation events
    ObserverCallbackMethod createCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleEmulatorInstanceCreated);
    messageCenter.AddObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

    // Subscribe to FDD disk insert/eject events
    ObserverCallbackMethod diskCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleFDDDiskChanged);
    messageCenter.AddObserver(NC_FDD_DISK_INSERTED, observerInstance, diskCallback);
    messageCenter.AddObserver(NC_FDD_DISK_EJECTED, observerInstance, diskCallback);
    messageCenter.AddObserver(NC_FDD_DISK_PENDING_WRITE, observerInstance, diskCallback);
    messageCenter.AddObserver(NC_FDD_DISK_WRITTEN, observerInstance, diskCallback);
}

MenuManager::~MenuManager()
{
    // Unsubscribe from emulator state changes
    MessageCenter& messageCenter = MessageCenter::DefaultMessageCenter();
    Observer* observerInstance = static_cast<Observer*>(this);
    ObserverCallbackMethod stateCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleEmulatorStateChanged);
    messageCenter.RemoveObserver(NC_EMULATOR_STATE_CHANGE, observerInstance, stateCallback);

    ObserverCallbackMethod createCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleEmulatorInstanceCreated);
    messageCenter.RemoveObserver(NC_EMULATOR_INSTANCE_CREATED, observerInstance, createCallback);

    // Unsubscribe from FDD disk events
    ObserverCallbackMethod diskCallback =
        static_cast<ObserverCallbackMethod>(&MenuManager::handleFDDDiskChanged);
    messageCenter.RemoveObserver(NC_FDD_DISK_INSERTED, observerInstance, diskCallback);
    messageCenter.RemoveObserver(NC_FDD_DISK_EJECTED, observerInstance, diskCallback);
    messageCenter.RemoveObserver(NC_FDD_DISK_PENDING_WRITE, observerInstance, diskCallback);
    messageCenter.RemoveObserver(NC_FDD_DISK_WRITTEN, observerInstance, diskCallback);
}

void MenuManager::createFileMenu()
{
    _fileMenu = _menuBar->addMenu(tr("&File"));

    // Open (generic)
    _openAction = _fileMenu->addAction(tr("&Open..."));
    _openAction->setShortcut(QKeySequence::Open);
    _openAction->setStatusTip(tr("Open a file (snapshot, tape, or disk)"));
    connect(_openAction, &QAction::triggered, this, &MenuManager::openFileRequested);

    _fileMenu->addSeparator();

    // Open Snapshot
    _openSnapshotAction = _fileMenu->addAction(tr("Open &Snapshot..."));
    _openSnapshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    _openSnapshotAction->setStatusTip(tr("Load a snapshot file (.z80, .sna, .szx)"));
    connect(_openSnapshotAction, &QAction::triggered, this, &MenuManager::openSnapshotRequested);

    // Open Tape
    _openTapeAction = _fileMenu->addAction(tr("Open &Tape..."));
    _openTapeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_T));
    _openTapeAction->setStatusTip(tr("Load a tape file (.tap, .tzx)"));
    connect(_openTapeAction, &QAction::triggered, this, &MenuManager::openTapeRequested);

    // Open Disk
    _openDiskAction = _fileMenu->addAction(tr("Open &Disk..."));
    _openDiskAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_D));
    _openDiskAction->setStatusTip(tr("Load a disk image (.trd, .scl, .fdi)"));
    connect(_openDiskAction, &QAction::triggered, this, &MenuManager::openDiskRequested);

    // Import audio → tape image (tape-audio-bridge §7.3): recognize a
    // WAV/FLAC/MP3 recording back into a .tzx/.tap image
    _importAudioTapeAction = _fileMenu->addAction(tr("Import &Audio to Tape..."));
    _importAudioTapeAction->setStatusTip(tr("Recognize a WAV/FLAC/MP3 recording into a .tzx/.tap tape image"));
    connect(_importAudioTapeAction, &QAction::triggered, this, &MenuManager::importAudioTapeRequested);

    _fileMenu->addSeparator();

    // Save Snapshot submenu
    _saveSnapshotMenu = _fileMenu->addMenu(tr("&Save Snapshot"));
    
    // Save as SNA
    _saveSnapshotSNAAction = _saveSnapshotMenu->addAction(tr("Save as .sna..."));
    _saveSnapshotSNAAction->setShortcut(QKeySequence::Save);
    _saveSnapshotSNAAction->setStatusTip(tr("Save current emulator state to SNA snapshot format"));
    connect(_saveSnapshotSNAAction, &QAction::triggered, this, &MenuManager::saveSnapshotRequested);
    
    // Save as Z80
    _saveSnapshotZ80Action = _saveSnapshotMenu->addAction(tr("Save as .z80..."));
    _saveSnapshotZ80Action->setStatusTip(tr("Save current emulator state to Z80 v3 snapshot format"));
    connect(_saveSnapshotZ80Action, &QAction::triggered, this, &MenuManager::saveSnapshotZ80Requested);

    // Save Disk submenu
    _saveDiskMenu = _fileMenu->addMenu(tr("Save &Disk"));
    
    // Save Disk (to original path)
    _saveDiskAction = _saveDiskMenu->addAction(tr("Save Disk"));
    _saveDiskAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    _saveDiskAction->setStatusTip(tr("Save disk image to its original file (TRD / SCL / UDI); non TR-DOS content is re-targeted to UDI"));
    connect(_saveDiskAction, &QAction::triggered, this, &MenuManager::saveDiskRequested);
    
    // Save as TRD
    _saveDiskTRDAction = _saveDiskMenu->addAction(tr("Save as .trd..."));
    _saveDiskTRDAction->setStatusTip(tr("Save disk image in TRD format"));
    connect(_saveDiskTRDAction, &QAction::triggered, this, &MenuManager::saveDiskAsTRDRequested);
    
    // Save as SCL
    _saveDiskSCLAction = _saveDiskMenu->addAction(tr("Save as .scl..."));
    _saveDiskSCLAction->setStatusTip(tr("Save disk image in SCL format"));
    connect(_saveDiskSCLAction, &QAction::triggered, this, &MenuManager::saveDiskAsSCLRequested);

    // Save as UDI (lossless raw track image)
    _saveDiskUDIAction = _saveDiskMenu->addAction(tr("Save as .udi..."));
    _saveDiskUDIAction->setStatusTip(tr("Save disk image in UDI format (lossless: keeps any track layout)"));
    connect(_saveDiskUDIAction, &QAction::triggered, this, &MenuManager::saveDiskAsUDIRequested);

    _fileMenu->addSeparator();

    // Recent Files (placeholder)
    _recentFilesAction = _fileMenu->addAction(tr("Recent Files"));
    _recentFilesAction->setEnabled(false);  // TODO: Implement recent files

    _fileMenu->addSeparator();

    // Exit
    _exitAction = _fileMenu->addAction(tr("E&xit"));
    _exitAction->setShortcut(QKeySequence::Quit);
    _exitAction->setStatusTip(tr("Exit the application"));
    connect(_exitAction, &QAction::triggered, _mainWindow, &QMainWindow::close);
}

void MenuManager::createEditMenu()
{
    _editMenu = _menuBar->addMenu(tr("&Edit"));

    // Preferences
    _preferencesAction = _editMenu->addAction(tr("&Preferences..."));
#ifdef Q_OS_MAC
    _preferencesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma));
#else
    _preferencesAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
#endif
    _preferencesAction->setStatusTip(tr("Configure emulator settings"));
    _preferencesAction->setEnabled(false);  // TODO: Implement preferences dialog
}

void MenuManager::createViewMenu()
{
    _viewMenu = _menuBar->addMenu(tr("&View"));

    // Log Window
    _logWindowAction = _viewMenu->addAction(tr("&Log Window"));
    _logWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    _logWindowAction->setStatusTip(tr("Show/hide log window"));
    _logWindowAction->setCheckable(true);
    _logWindowAction->setChecked(true);
    connect(_logWindowAction, &QAction::triggered, this, &MenuManager::logWindowToggled);

    _viewMenu->addSeparator();

    // Toolbar (transport toolbar under the menu bar)
    _toolBarAction = _viewMenu->addAction(tr("&Toolbar"));
    _toolBarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_T));
    _toolBarAction->setStatusTip(tr("Show/hide toolbar"));
    _toolBarAction->setCheckable(true);
    _toolBarAction->setChecked(true);
    connect(_toolBarAction, &QAction::triggered, this, &MenuManager::toolBarToggled);

    // Status bar (device LEDs and FPS at the bottom of the window)
    _statusBarAction = _viewMenu->addAction(tr("&Status Bar"));
    _statusBarAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Slash));
    _statusBarAction->setStatusTip(tr("Show/hide status bar"));
    _statusBarAction->setCheckable(true);
    _statusBarAction->setChecked(true);
    connect(_statusBarAction, &QAction::triggered, this, &MenuManager::statusBarToggled);

    // HUD overlay (on-screen toasts, indicators, picture augmentation)
    _hudOverlayAction = _viewMenu->addAction(tr("&HUD Overlay"));
    _hudOverlayAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H));
    _hudOverlayAction->setStatusTip(tr("Show/hide on-screen HUD overlay (toasts, indicators)"));
    _hudOverlayAction->setCheckable(true);
    _hudOverlayAction->setChecked(false);
    connect(_hudOverlayAction, &QAction::triggered, this, &MenuManager::hudOverlayToggled);

    _viewMenu->addSeparator();

    // GPU acceleration toggle
    _gpuAccelerationAction = _viewMenu->addAction(tr("&GPU Acceleration"));
    _gpuAccelerationAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G));
    _gpuAccelerationAction->setStatusTip(tr("Use GPU for display rendering (faster scaling, enables CRT effects)"));
    _gpuAccelerationAction->setCheckable(true);
    _gpuAccelerationAction->setChecked(false);
    connect(_gpuAccelerationAction, &QAction::triggered, this, &MenuManager::gpuAccelerationToggled);

    // CRT effects (scanlines, curvature - GPU only)
    _crtEffectsAction = _viewMenu->addAction(tr("C&RT Effects"));
    _crtEffectsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));
    _crtEffectsAction->setStatusTip(tr("Toggle CRT display effects (scanlines, curvature) - requires GPU acceleration"));
    _crtEffectsAction->setCheckable(true);
    _crtEffectsAction->setChecked(false);
    _crtEffectsAction->setEnabled(false);  // Disabled until GPU is enabled
    connect(_crtEffectsAction, &QAction::triggered, this, &MenuManager::crtEffectsToggled);

    // CRT profile submenu
    _crtProfileMenu = _viewMenu->addMenu(tr("CRT &Profile"));
    _crtProfileMenu->setEnabled(false);
    _crtProfileGroup = new QActionGroup(this);
    populateCrtProfileMenu();

    // Temporal blending (gigascreen flicker smoothing) - works for both GPU and software
    _temporalBlendingAction = _viewMenu->addAction(tr("&Temporal Blending"));
    _temporalBlendingAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T));
    _temporalBlendingAction->setStatusTip(tr("Smooth gigascreen flicker by blending multiple frames"));
    _temporalBlendingAction->setCheckable(true);
    _temporalBlendingAction->setChecked(false);
    connect(_temporalBlendingAction, &QAction::triggered, this, &MenuManager::temporalBlendingToggled);

    // Full Screen
    // Single full-screen entry: Cmd+F on macOS, Ctrl+F elsewhere (Qt::CTRL maps to Cmd
    // on macOS). Cocoa's own "Enter Full Screen" View-menu item is suppressed in main().
    _fullScreenAction = _viewMenu->addAction(tr("&Full Screen\tCtrl+F"));
    // Shortcut is handled by app-wide QShortcut in MainWindow (works when menu hidden)
    _fullScreenAction->setStatusTip(tr("Toggle full screen mode"));
    _fullScreenAction->setCheckable(true);
    connect(_fullScreenAction, &QAction::triggered, this, &MenuManager::fullScreenToggled);

    _viewMenu->addSeparator();

    // Fixed scale presets: resize the window so the emulator screen (352x288 frame,
    // the same frame in overscan mode) is shown at an integer scale plus the chrome
    _scaleMenu = _viewMenu->addMenu(tr("&Scale"));
    for (int scale = 1; scale <= 4; ++scale)
    {
        QAction* action = _scaleMenu->addAction(tr("%1x (%2x%3)").arg(scale).arg(352 * scale).arg(288 * scale));
        action->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | (Qt::Key_0 + scale)));
        action->setStatusTip(tr("Resize the window to show the screen at %1x").arg(scale));
        connect(action, &QAction::triggered, this, [this, scale]() { emit scaleRequested(scale); });
        _scaleActions.push_back(action);
    }

    _viewMenu->addSeparator();

    // Overscan mode (Pentagon only - 384x304 with extended border)
    _overscanAction = _viewMenu->addAction(tr("&Overscan Mode"));
    _overscanAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O));
    _overscanAction->setStatusTip(tr("Pentagon overscan mode (384x304) - shows invisible border areas"));
    _overscanAction->setCheckable(true);
    _overscanAction->setEnabled(false);  // Enabled only for Pentagon
    connect(_overscanAction, &QAction::triggered, this, &MenuManager::overscanModeToggled);

    // Viewport submenu (only meaningful in overscan mode)
    _viewportMenu = _viewMenu->addMenu(tr("Display &Viewport"));
    _viewportMenu->setStatusTip(tr("Crop framebuffer for display"));
    _viewportMenu->setEnabled(false);  // Enabled when overscan is active

    _viewportGroup = new QActionGroup(this);
    _viewportGroup->setExclusive(true);

    _viewportFullOverscanAction = _viewportMenu->addAction(tr("&Full Overscan (384x304)"));
    _viewportFullOverscanAction->setCheckable(true);
    _viewportGroup->addAction(_viewportFullOverscanAction);
    connect(_viewportFullOverscanAction, &QAction::triggered, this, [this]() { emit viewportChanged(0); });

    _viewportSymmetricAction = _viewportMenu->addAction(tr("&Symmetric Horizontal (352x304)"));
    _viewportSymmetricAction->setCheckable(true);
    _viewportSymmetricAction->setChecked(true);  // Default viewport
    _viewportGroup->addAction(_viewportSymmetricAction);
    connect(_viewportSymmetricAction, &QAction::triggered, this, [this]() { emit viewportChanged(1); });

    _viewportStandardAction = _viewportMenu->addAction(tr("S&tandard (352x288)"));
    _viewportStandardAction->setCheckable(true);
    _viewportGroup->addAction(_viewportStandardAction);
    connect(_viewportStandardAction, &QAction::triggered, this, [this]() { emit viewportChanged(2); });

    _viewportScreenOnlyAction = _viewportMenu->addAction(tr("Screen &Only (256x192)"));
    _viewportScreenOnlyAction->setCheckable(true);
    _viewportGroup->addAction(_viewportScreenOnlyAction);
    connect(_viewportScreenOnlyAction, &QAction::triggered, this, [this]() { emit viewportChanged(3); });
}

void MenuManager::setDebuggerChecked(bool checked)
{
    if (_debuggerAction)
    {
        _debuggerAction->setChecked(checked);
    }
}

void MenuManager::setToolBarChecked(bool checked)
{
    if (_toolBarAction)
    {
        _toolBarAction->setChecked(checked);
    }
}

void MenuManager::setStatusBarChecked(bool checked)
{
    if (_statusBarAction)
    {
        _statusBarAction->setChecked(checked);
    }
}

void MenuManager::setHudOverlayChecked(bool checked)
{
    if (_hudOverlayAction)
    {
        _hudOverlayAction->setChecked(checked);
    }
}

void MenuManager::setGpuAccelerationChecked(bool checked)
{
    if (_gpuAccelerationAction)
    {
        _gpuAccelerationAction->setChecked(checked);
    }
}

void MenuManager::setGpuAccelerationAvailable(bool available)
{
    if (_gpuAccelerationAction)
    {
        _gpuAccelerationAction->setEnabled(available);
        if (!available)
        {
            _gpuAccelerationAction->setStatusTip(tr("GPU acceleration not available on this system"));
        }
    }
}

void MenuManager::setCrtEffectsChecked(bool checked)
{
    if (_crtEffectsAction)
    {
        _crtEffectsAction->setChecked(checked);
    }
}

void MenuManager::setCrtEffectsEnabled(bool enabled)
{
    if (_crtEffectsAction)
    {
        _crtEffectsAction->setEnabled(enabled);
        if (!enabled)
        {
            _crtEffectsAction->setStatusTip(tr("CRT effects require GPU acceleration"));
        }
        else
        {
            _crtEffectsAction->setStatusTip(tr("Toggle CRT display effects (scanlines, curvature)"));
        }
    }
    if (_crtProfileMenu)
    {
        _crtProfileMenu->setEnabled(enabled);
    }
    // Temporal blending toggle in View menu (for quick toggle, separate from Tools dialog)
    if (_temporalBlendingAction)
    {
        _temporalBlendingAction->setStatusTip(tr("Smooth gigascreen flicker by blending multiple frames"));
    }
}

void MenuManager::setTemporalBlendingChecked(bool checked)
{
    if (_temporalBlendingAction)
    {
        _temporalBlendingAction->setChecked(checked);
    }
}

void MenuManager::setTemporalBlendingEnabled(bool enabled)
{
    if (_temporalBlendingAction)
    {
        _temporalBlendingAction->setEnabled(enabled);
    }
}

void MenuManager::setCrtProfile(int profileIndex)
{
    if (_crtProfileGroup)
    {
        QList<QAction*> actions = _crtProfileGroup->actions();
        if (profileIndex >= 0 && profileIndex < actions.size())
        {
            actions[profileIndex]->setChecked(true);
        }
    }
}

void MenuManager::populateCrtProfileMenu()
{
    if (!_crtProfileMenu || !_crtProfileGroup)
        return;

    _crtProfileMenu->clear();

    // Add built-in profiles
    auto profiles = CRTProfileParams::AllProfiles();
    int index = 0;
    for (CRTProfile profile : profiles)
    {
        QString name = CRTProfileParams::ProfileName(profile);
        QString desc = CRTProfileParams::ProfileDescription(profile);

        QAction* action = _crtProfileMenu->addAction(name);
        action->setCheckable(true);
        action->setStatusTip(desc);
        action->setData(index);
        _crtProfileGroup->addAction(action);

        connect(action, &QAction::triggered, this, [this, index]() {
            emit crtProfileChanged(index);
        });

        if (profile == CRTProfile::None)
            action->setChecked(true);

        index++;
    }

    // Add separator and custom shaders section
    _crtProfileMenu->addSeparator();

    // Scan for custom shaders
    CRTShaderManager::instance().scanShaderDirectory();
    auto customShaders = CRTShaderManager::instance().availableShaders();

    if (!customShaders.empty())
    {
        for (const QString& shaderName : customShaders)
        {
            QAction* action = _crtProfileMenu->addAction(shaderName + " (custom)");
            action->setCheckable(true);
            action->setStatusTip(tr("Custom shader: %1").arg(shaderName));
            action->setData(-1);  // Custom shader marker
            action->setProperty("shaderName", shaderName);
            _crtProfileGroup->addAction(action);

            connect(action, &QAction::triggered, this, [this, shaderName]() {
                emit crtProfileChanged(-1);  // -1 = custom, use property
            });
        }
    }
    else
    {
        QAction* placeholder = _crtProfileMenu->addAction(tr("(No custom shaders)"));
        placeholder->setEnabled(false);
    }
}

void MenuManager::setTapeManagerChecked(bool checked)
{
    // Sync from the TapeManagerWindow's own close box; setChecked never
    // re-emits triggered, so this cannot recurse into the toggle handler
    if (_tapeManagerAction)
    {
        _tapeManagerAction->setChecked(checked);
    }
}

void MenuManager::createRunMenu()
{
    _runMenu = _menuBar->addMenu(tr("&Run"));

    // Start
    _startAction = _runMenu->addAction(tr("&Start"));
    _startAction->setShortcut(QKeySequence(Qt::Key_F5));
    _startAction->setStatusTip(tr("Start emulation"));
    connect(_startAction, &QAction::triggered, this, &MenuManager::startRequested);

    // Pause
    _pauseAction = _runMenu->addAction(tr("&Pause"));
    _pauseAction->setShortcut(QKeySequence(Qt::Key_F6));
    _pauseAction->setStatusTip(tr("Pause emulation"));
    _pauseAction->setEnabled(false);
    connect(_pauseAction, &QAction::triggered, this, &MenuManager::pauseRequested);

    // Resume
    _resumeAction = _runMenu->addAction(tr("Res&ume"));
    _resumeAction->setShortcut(QKeySequence(Qt::Key_F7));
    _resumeAction->setStatusTip(tr("Resume emulation"));
    _resumeAction->setEnabled(false);
    connect(_resumeAction, &QAction::triggered, this, &MenuManager::resumeRequested);

    // Stop
    _stopAction = _runMenu->addAction(tr("S&top"));
    _stopAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    _stopAction->setStatusTip(tr("Stop emulation"));
    _stopAction->setEnabled(false);
    connect(_stopAction, &QAction::triggered, this, &MenuManager::stopRequested);

    _runMenu->addSeparator();

    // Reset
    _resetAction = _runMenu->addAction(tr("&Reset"));
    _resetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_R));
    _resetAction->setStatusTip(tr("Reset emulator"));
    connect(_resetAction, &QAction::triggered, this, &MenuManager::resetRequested);

    _runMenu->addSeparator();

    // Speed submenu
    _speedMenu = _runMenu->addMenu(tr("&Speed"));
    _speedGroup = new QActionGroup(this);
    _speedGroup->setExclusive(true);

    _speed1xAction = _speedMenu->addAction(tr("1x (Normal)"));
    _speed1xAction->setShortcut(QKeySequence(Qt::Key_F1));
    _speed1xAction->setCheckable(true);
    _speed1xAction->setChecked(true);
    _speedGroup->addAction(_speed1xAction);
    connect(_speed1xAction, &QAction::triggered, this, [this]() { emit speedMultiplierChanged(1); });

    _speed2xAction = _speedMenu->addAction(tr("2x (Fast)"));
    _speed2xAction->setShortcut(QKeySequence(Qt::Key_F2));
    _speed2xAction->setCheckable(true);
    _speedGroup->addAction(_speed2xAction);
    connect(_speed2xAction, &QAction::triggered, this, [this]() { emit speedMultiplierChanged(2); });

    _speed4xAction = _speedMenu->addAction(tr("4x (Very Fast)"));
    _speed4xAction->setShortcut(QKeySequence(Qt::Key_F3));
    _speed4xAction->setCheckable(true);
    _speedGroup->addAction(_speed4xAction);
    connect(_speed4xAction, &QAction::triggered, this, [this]() { emit speedMultiplierChanged(4); });

    _speed8xAction = _speedMenu->addAction(tr("8x (Extreme)"));
    _speed8xAction->setShortcut(QKeySequence(Qt::Key_F4));
    _speed8xAction->setCheckable(true);
    _speedGroup->addAction(_speed8xAction);
    connect(_speed8xAction, &QAction::triggered, this, [this]() { emit speedMultiplierChanged(8); });

    _speed16xAction = _speedMenu->addAction(tr("16x (Insane)"));
    _speed16xAction->setCheckable(true);
    _speedGroup->addAction(_speed16xAction);
    connect(_speed16xAction, &QAction::triggered, this, [this]() { emit speedMultiplierChanged(16); });

    _speedMenu->addSeparator();

    // Turbo Mode (max speed)
    _turboModeAction = _speedMenu->addAction(tr("Turbo Mode (Max Speed)"));
    _turboModeAction->setShortcut(QKeySequence(Qt::Key_Tab));
    _turboModeAction->setStatusTip(tr("Hold Tab for maximum speed (no sync)"));
    _turboModeAction->setCheckable(true);
    connect(_turboModeAction, &QAction::triggered, this, &MenuManager::turboModeToggled);
}

void MenuManager::createMachineMenu()
{
    _machineMenu = _menuBar->addMenu(tr("&Machine"));
    _machineModelGroup = new QActionGroup(this);
    _machineModelGroup->setExclusive(true);

    // Get available models from EmulatorManager
    EmulatorManager* manager = EmulatorManager::GetInstance();
    if (!manager)
        return;

    std::vector<TMemModel> models = manager->GetAvailableModels();
    const int ramSizes[] = {48, 128, 256, 512, 1024, 2048, 4096};

    // Only show supported models for now
    // TODO: Enable other models as they become fully supported
    std::set<MEM_MODEL> supportedModels = {
        MM_PENTAGON,      // Pentagon 128K/512K/1024K
        MM_SPECTRUM48,    // ZX-Spectrum 48K
        MM_SPECTRUM128    // ZX-Spectrum 128K
    };

    for (const auto& model : models)
    {
        // Skip unsupported models
        if (supportedModels.find(model.Model) == supportedModels.end())
            continue;

        QString shortName = QString::fromUtf8(model.ShortName);
        QString baseName = QString::fromUtf8(model.FullName);

        // Skip models with empty names (shouldn't happen, but guard against it)
        if (baseName.isEmpty() || shortName.isEmpty())
        {
            qWarning() << "MenuManager::createMachineMenu - Skipping model with empty name:"
                       << "FullName=" << baseName << "ShortName=" << shortName;
            continue;
        }

        // Count available RAM sizes for this model
        int ramCount = 0;
        for (int ram : ramSizes)
        {
            if (model.AvailRAMs & ram)
                ramCount++;
        }

        // Skip models with no matching RAM sizes
        if (ramCount == 0)
        {
            qWarning() << "MenuManager::createMachineMenu - Skipping model with no valid RAM sizes:"
                       << baseName << "AvailRAMs=" << model.AvailRAMs;
            continue;
        }

        // If only one RAM option, show just the model name
        if (ramCount == 1)
        {
            QAction* action = _machineMenu->addAction(baseName);
            action->setCheckable(true);
            // Store as "MODEL:RAM" for parsing
            action->setData(QString("%1:%2").arg(shortName).arg(model.defaultRAM));
            action->setStatusTip(tr("Switch to %1").arg(baseName));
            _machineModelGroup->addAction(action);
            _machineModelActions.push_back(action);

            connect(action, &QAction::triggered, this, [this, shortName, ram = model.defaultRAM]() {
                QString key = QString("%1:%2").arg(shortName).arg(ram);
                if (key != _currentModelShortName)
                {
                    emit machineModelChangeRequested(key);
                }
            });
        }
        else
        {
            // Multiple RAM options - create entry for each
            for (int ram : ramSizes)
            {
                if (!(model.AvailRAMs & ram))
                    continue;

                QString displayName = QString("%1 %2K").arg(baseName).arg(ram);
                QAction* action = _machineMenu->addAction(displayName);
                action->setCheckable(true);
                action->setData(QString("%1:%2").arg(shortName).arg(ram));
                action->setStatusTip(tr("Switch to %1 with %2K RAM").arg(baseName).arg(ram));
                _machineModelGroup->addAction(action);
                _machineModelActions.push_back(action);

                connect(action, &QAction::triggered, this, [this, shortName, ram]() {
                    QString key = QString("%1:%2").arg(shortName).arg(ram);
                    if (key != _currentModelShortName)
                    {
                        emit machineModelChangeRequested(key);
                    }
                });
            }
        }
    }

    // Set default selection (first entry)
    if (!_machineModelActions.empty())
    {
        _machineModelActions[0]->setChecked(true);
        _currentModelShortName = _machineModelActions[0]->data().toString();
    }

    _machineMenu->addSeparator();

    // Fast tape loading trap (LD-BYTES $0556 hook — design:
    // docs/inprogress/2026-08-30-fast-tape-loading). When on, vanilla ROM
    // tape blocks load instantly; custom loaders fall back to full signal
    // emulation. Checked state mirrors the runtime 'fasttape' feature of the
    // active instance (synced in updateMenuStates).
    _tapeTrapsAction = _machineMenu->addAction(tr("&Fast Tape Loading"));
    _tapeTrapsAction->setStatusTip(tr("Serve vanilla ROM tape loads instantly (custom loaders use signal emulation)"));
    _tapeTrapsAction->setCheckable(true);
    connect(_tapeTrapsAction, &QAction::triggered, this, &MenuManager::tapeTrapsToggled);

    // Turbo tape loading (design: docs/inprogress/2026-09-04-turbo-tape-loading).
    // While the tape signal path plays, the machine runs unthrottled (turbo
    // mode) so blocks the trap cannot serve — headerless, custom-timed, pulse
    // streams — still load at warp speed. Warp ends with the read-gap
    // watchdog, end-of-tape or any stop. Checked state mirrors the runtime
    // 'turbotape' feature (synced in updateMenuStates).
    _turboTapeAction = _machineMenu->addAction(tr("Tur&bo Tape Loading"));
    _turboTapeAction->setStatusTip(tr("Run at warp speed while a tape signal is playing (custom loaders included)"));
    _turboTapeAction->setCheckable(true);
    connect(_turboTapeAction, &QAction::triggered, this, &MenuManager::turboTapeToggled);
}

void MenuManager::updateMachineModelSelection(std::shared_ptr<Emulator> activeEmulator)
{
    if (!activeEmulator)
        return;

    // Get current model from emulator context
    EmulatorContext* ctx = activeEmulator->GetContext();
    if (!ctx)
        return;

    MEM_MODEL currentModel = ctx->config.mem_model;
    uint32_t currentRam = ctx->config.ramsize;

    // Find and check the matching action (format: "MODEL:RAM")
    for (QAction* action : _machineModelActions)
    {
        QString data = action->data().toString();
        QStringList parts = data.split(':');
        if (parts.size() != 2)
            continue;

        QString modelName = parts[0];
        uint32_t ram = parts[1].toUInt();

        // Find model info to get the MEM_MODEL enum
        EmulatorManager* manager = EmulatorManager::GetInstance();
        if (manager)
        {
            std::vector<TMemModel> models = manager->GetAvailableModels();
            for (const auto& model : models)
            {
                if (QString::fromUtf8(model.ShortName) == modelName &&
                    model.Model == currentModel && ram == currentRam)
                {
                    action->setChecked(true);
                    _currentModelShortName = data;
                    return;
                }
            }
        }
    }
}

void MenuManager::createDebugMenu()
{
    _debugMenu = _menuBar->addMenu(tr("&Debug"));

    // Debugger window. Hidden at start; while hidden the emulator runs without
    // debug instrumentation (see MainWindow::handleDebuggerVisibilityChanged)
    _debuggerAction = _debugMenu->addAction(tr("&Debugger Window"));
    _debuggerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    _debuggerAction->setStatusTip(tr("Show/hide the debugger window (debug features are active only while it is shown)"));
    _debuggerAction->setCheckable(true);
    _debuggerAction->setChecked(false);
    connect(_debuggerAction, &QAction::triggered, this, &MenuManager::debuggerToggled);

    _debugMenu->addSeparator();

    // Step In
    _stepInAction = _debugMenu->addAction(tr("Step &In"));
    _stepInAction->setShortcut(QKeySequence(Qt::Key_F8));
    _stepInAction->setStatusTip(tr("Execute one instruction"));
    connect(_stepInAction, &QAction::triggered, this, &MenuManager::stepInRequested);

    // Step Over
    _stepOverAction = _debugMenu->addAction(tr("Step &Over"));
    _stepOverAction->setShortcut(QKeySequence(Qt::Key_F10));
    _stepOverAction->setStatusTip(tr("Execute instruction, skip calls"));
    connect(_stepOverAction, &QAction::triggered, this, &MenuManager::stepOverRequested);

    // Step Out
    _stepOutAction = _debugMenu->addAction(tr("Step O&ut"));
    _stepOutAction->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F8));
    _stepOutAction->setStatusTip(tr("Execute until return from current function"));
    _stepOutAction->setEnabled(false);  // TODO: Implement step out

    // Run to Cursor
    _runToCursorAction = _debugMenu->addAction(tr("Run to &Cursor"));
    _runToCursorAction->setShortcut(QKeySequence(Qt::Key_F9));
    _runToCursorAction->setStatusTip(tr("Execute until cursor position"));
    _runToCursorAction->setEnabled(false);  // TODO: Implement run to cursor

    _debugMenu->addSeparator();

    // Toggle Breakpoint
    _toggleBreakpointAction = _debugMenu->addAction(tr("&Toggle Breakpoint"));
    _toggleBreakpointAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    _toggleBreakpointAction->setStatusTip(tr("Toggle breakpoint at current address"));
    _toggleBreakpointAction->setEnabled(false);  // TODO: Implement

    // Clear All Breakpoints
    _clearAllBreakpointsAction = _debugMenu->addAction(tr("&Clear All Breakpoints"));
    _clearAllBreakpointsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    _clearAllBreakpointsAction->setStatusTip(tr("Remove all breakpoints"));
    _clearAllBreakpointsAction->setEnabled(false);  // TODO: Implement

    // Show Breakpoints
    _showBreakpointsAction = _debugMenu->addAction(tr("Show &Breakpoints..."));
    _showBreakpointsAction->setStatusTip(tr("Show breakpoints window"));
    _showBreakpointsAction->setEnabled(false);  // TODO: Implement

    _debugMenu->addSeparator();

    // Show Registers
    _showRegistersAction = _debugMenu->addAction(tr("Show &Registers"));
    _showRegistersAction->setStatusTip(tr("Show CPU registers"));
    _showRegistersAction->setEnabled(false);  // TODO: Implement

    // Show Memory
    _showMemoryAction = _debugMenu->addAction(tr("Show &Memory"));
    _showMemoryAction->setStatusTip(tr("Show memory viewer"));
    _showMemoryAction->setEnabled(false);  // TODO: Implement
}

void MenuManager::createToolsMenu()
{
    _toolsMenu = _menuBar->addMenu(tr("&Tools"));

    // Settings
    _settingsAction = _toolsMenu->addAction(tr("&Settings..."));
    _settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    _settingsAction->setStatusTip(tr("Configure emulator settings"));
    _settingsAction->setEnabled(false);  // TODO: Implement settings dialog

    _toolsMenu->addSeparator();

    // INT Parameters
    _intParametersAction = _toolsMenu->addAction(tr("&INT Parameters..."));
    _intParametersAction->setStatusTip(tr("Configure interrupt timing parameters"));
    connect(_intParametersAction, &QAction::triggered, this, &MenuManager::intParametersRequested);

    // Audio Settings
    _audioSettingsAction = _toolsMenu->addAction(tr("&Audio Settings..."));
    _audioSettingsAction->setStatusTip(tr("Configure audio DSP: punch, FIR filter, room simulation"));
    connect(_audioSettingsAction, &QAction::triggered, this, &MenuManager::audioSettingsRequested);

    // Temporal Effects Settings (works for both GPU and software rendering)
    _temporalEffectsAction = _toolsMenu->addAction(tr("&Temporal Effects..."));
    _temporalEffectsAction->setStatusTip(tr("Configure frame blending for gigascreen smoothing"));
    connect(_temporalEffectsAction, &QAction::triggered, this, &MenuManager::temporalEffectsRequested);

    _toolsMenu->addSeparator();

    // Tape Manager Window (design §9.2 — checkable show/hide, hidden until
    // first opened; lives in Tools beside the other auxiliary windows, r7)
    _tapeManagerAction = _toolsMenu->addAction(tr("Tape &Manager"));
    _tapeManagerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_3));
    _tapeManagerAction->setStatusTip(tr("Show/hide tape manager window"));
    _tapeManagerAction->setCheckable(true);
    _tapeManagerAction->setChecked(false);
    connect(_tapeManagerAction, &QAction::triggered, this, &MenuManager::tapeManagerToggled);

    _toolsMenu->addSeparator();

    // Screenshot of the emulator framebuffer to clipboard
    _screenshotAction = _toolsMenu->addAction(tr("Take &Screenshot"));
    _screenshotAction->setShortcut(QKeySequence(Qt::Key_F12));
    _screenshotAction->setStatusTip(tr("Copy the emulator screen to the clipboard"));
    connect(_screenshotAction, &QAction::triggered, this, &MenuManager::screenshotRequested);

#ifdef ENABLE_RECORDING
    // Recording (dialog toggle)
    _videoRecordingAction = _toolsMenu->addAction(tr("&Recording"));
    _videoRecordingAction->setStatusTip(tr("Open recording panel"));
    connect(_videoRecordingAction, &QAction::triggered, this, &MenuManager::videoRecordingRequested);

    _toolsMenu->addSeparator();

    // Quick Record submenu with preset shortcuts
    _quickRecordMenu = _toolsMenu->addMenu(tr("&Quick Record"));
    _quickRecordMenu->setStatusTip(tr("Quickly start recording with a preset"));

    // Populate with preset names (will be connected via lambda to emit quickRecordRequested)
    auto addPresetAction = [this](const QString& name) {
        QAction* action = _quickRecordMenu->addAction(name);
        connect(action, &QAction::triggered, this, [this, name]() {
            emit quickRecordRequested(name);
        });
    };

    addPresetAction(tr("Gameplay (MP4)"));
    addPresetAction(tr("GIF Animation"));
    addPresetAction(tr("High Quality (MKV)"));
    // TODO: Re-add "Music (FLAC)" / "Audio Only (WAV)" when audio-only
    // recording lands, and "Multi-Track (MKV)" when multi-track encoding lands.
#endif
}

void MenuManager::createHelpMenu()
{
    _helpMenu = _menuBar->addMenu(tr("&Help"));

    // Documentation
    _documentationAction = _helpMenu->addAction(tr("&Documentation"));
    _documentationAction->setShortcut(QKeySequence::HelpContents);
    _documentationAction->setStatusTip(tr("View documentation"));
    connect(_documentationAction, &QAction::triggered, []() {
        QMessageBox::information(nullptr, "Documentation",
                                 "Documentation is available at:\n"
                                 "docs/emulator/design/\n\n"
                                 "Key files:\n"
                                 "- speed-control.md - Speed multiplier and turbo mode\n"
                                 "- command-interface.md - CLI commands reference");
    });

    // Keyboard Shortcuts
    _keyboardShortcutsAction = _helpMenu->addAction(tr("&Keyboard Shortcuts"));
    _keyboardShortcutsAction->setStatusTip(tr("View keyboard shortcuts"));
    connect(_keyboardShortcutsAction, &QAction::triggered, []() {
        QMessageBox::information(nullptr, "Keyboard Shortcuts",
                                 "Emulation:\n"
                                 "F5 - Start\n"
                                 "F6 - Pause\n"
                                 "F7 - Resume\n"
                                 "Ctrl+R - Reset\n\n"

                                 "Speed:\n"
                                 "F1 - 1x (Normal)\n"
                                 "F2 - 2x (Fast)\n"
                                 "F3 - 4x (Very Fast)\n"
                                 "F4 - 8x (Extreme)\n"
                                 "Tab - Hold for Turbo Mode\n\n"

                                 "Debug:\n"
                                 "F8 - Step In\n"
                                 "F10 - Step Over\n"
                                 "F9 - Run to Cursor\n"
                                 "Ctrl+B - Toggle Breakpoint\n\n"

                                 "View:\n"
                                 "Ctrl+F - Full Screen\n"
                                 "Ctrl+1 - Toggle Debugger\n"
                                 "Ctrl+2 - Toggle Log Window");
    });

    _helpMenu->addSeparator();

    // About
    _aboutAction = _helpMenu->addAction(tr("&About"));
    _aboutAction->setStatusTip(tr("About Unreal Speccy"));
    connect(_aboutAction, &QAction::triggered, [this]() {
        QMessageBox::about(_mainWindow, tr("About Unreal Speccy"),
                           tr("<h3>Unreal Speccy - Next Generation</h3>"
                              "<p>ZX Spectrum emulator</p>"
                              "<p>Version 0.1.0 (alpha)</p>"
                              "<p>Built with Qt %1</p>"
                              "<p>&copy; 2024 Unreal Speccy Project</p>")
                               .arg(QT_VERSION_STR));
    });
}

void MenuManager::applyPlatformSpecificSettings()
{
#ifdef Q_OS_MAC
    // macOS uses native menu bar
    _menuBar->setNativeMenuBar(true);
#endif
}

void MenuManager::updateMenuStates(std::shared_ptr<Emulator> activeEmulator)
{
    // Store weak reference to active emulator (for future queries)
    _activeEmulator = activeEmulator;

    // Query state directly from emulator - single source of truth!
    bool emulatorExists = (activeEmulator != nullptr);
    bool isRunning = emulatorExists && activeEmulator->IsRunning();
    bool isPaused = emulatorExists && activeEmulator->IsPaused();

    // File menu - Save Snapshot requires active emulator
    _saveSnapshotMenu->setEnabled(emulatorExists);
    
    // File menu - Save Disk menu and actions
    bool hasDiskLoaded = false;
    bool isDiskDirty = false;
    if (emulatorExists)
    {
        EmulatorContext* context = activeEmulator->GetContext();
        if (context && context->pBetaDisk)
        {
            FDD* drive = context->pBetaDisk->getDrive();
            if (drive && drive->getDiskImage())
            {
                hasDiskLoaded = true;
                isDiskDirty = drive->getDiskImage()->isDirty();
            }
        }
    }
    
    // Enable/disable the entire Save Disk submenu based on disk presence
    _saveDiskMenu->setEnabled(hasDiskLoaded);
    
    // Update Save Disk action text and state based on dirty status
    if (hasDiskLoaded)
    {
        // Update menu text: show asterisk when dirty
        if (isDiskDirty)
        {
            _saveDiskAction->setText(tr("Save Disk *"));
        }
        else
        {
            _saveDiskAction->setText(tr("Save Disk"));
        }
        
        // Save Disk only enabled when there are unsaved changes
        _saveDiskAction->setEnabled(isDiskDirty);
        
        // Save As options always available when disk is loaded
        _saveDiskTRDAction->setEnabled(true);
        _saveDiskSCLAction->setEnabled(true);
        _saveDiskUDIAction->setEnabled(true);
    }
    else
    {
        // Reset to default text when no disk loaded
        _saveDiskAction->setText(tr("Save Disk"));
        _saveDiskAction->setEnabled(false);
        _saveDiskTRDAction->setEnabled(false);
        _saveDiskSCLAction->setEnabled(false);
    }

    // Run menu states
    _startAction->setEnabled(!emulatorExists);         // Start only when no emulator
    _pauseAction->setEnabled(isRunning && !isPaused);  // Pause when running
    _resumeAction->setEnabled(isPaused);               // Resume when paused (even if IsRunning() is false)
    _stopAction->setEnabled(emulatorExists);           // Stop when emulator exists
    _resetAction->setEnabled(emulatorExists);          // Reset when emulator exists

    // Speed menu - enabled when emulator exists
    _speedMenu->setEnabled(emulatorExists);

    // Debug menu states
    _stepInAction->setEnabled(!isRunning || isPaused);
    _stepOverAction->setEnabled(!isRunning || isPaused);

    // Overscan menu states (Pentagon only)
    // Only update overscan visibility when there's an active emulator
    // Skip update when emulator is null (during transitions) to avoid hiding menu incorrectly
    if (emulatorExists)
    {
        EmulatorContext* context = activeEmulator->GetContext();
        bool isPentagon = (context && context->config.mem_model == MM_PENTAGON);
        bool isOverscanActive = isPentagon && activeEmulator->IsOverscanMode();

        // Lock overscan/viewport controls during recording to prevent mid-recording
        // resolution changes (viewport is captured at recording start)
        bool isRecording = context && context->pRecordingManager &&
                           context->pRecordingManager->IsRecording();

        // Pentagon: show and enable overscan, show viewport when overscan active
        // Non-Pentagon: hide overscan, hide viewport
        // Recording: show but disable overscan/viewport to prevent changes
        _overscanAction->setVisible(isPentagon);
        _overscanAction->setEnabled(isPentagon && !isRecording);
        _overscanAction->setChecked(isOverscanActive);
        // Use menuAction() to control submenu visibility in parent menu
        // (calling setVisible() on QMenu itself can trigger unwanted popup)
        _viewportMenu->menuAction()->setVisible(isPentagon);
        _viewportMenu->setEnabled(isOverscanActive && !isRecording);
    }
    else
    {
        // No emulator: hide Pentagon-only menus
        _overscanAction->setVisible(false);
        _overscanAction->setEnabled(false);
        _viewportMenu->menuAction()->setVisible(false);
        _viewportMenu->setEnabled(false);
    }

    // Machine menu - fast tape loading toggle mirrors the runtime 'fasttape'
    // feature (the trap re-reads it on every LD-BYTES invocation, so only the
    // menu state needs syncing)
    _tapeTrapsAction->setEnabled(emulatorExists);
    if (emulatorExists)
    {
        EmulatorContext* context = activeEmulator->GetContext();
        FeatureManager* featureManager = context ? context->pFeatureManager : nullptr;
        _tapeTrapsAction->setChecked(featureManager && featureManager->isEnabled(Features::kFastTape));
        _turboTapeAction->setChecked(featureManager && featureManager->isEnabled(Features::kTurboTape));
        if (_hudOverlayAction)
        {
            _hudOverlayAction->setChecked(featureManager && featureManager->isEnabled(Features::kHud));
        }
    }
    else if (_hudOverlayAction)
    {
        _hudOverlayAction->setChecked(false);
    }

    // Update machine model selection
    updateMachineModelSelection(activeEmulator);
}

void MenuManager::resetViewportSelection()
{
    _viewportSymmetricAction->setChecked(true);
}

void MenuManager::setActiveEmulator(std::shared_ptr<Emulator> emulator)
{
    // Update menu states - must be done on main thread since it modifies UI menus
    // Store the emulator reference for thread-safe access
    _activeEmulator = emulator;

    QMetaObject::invokeMethod(this, [this, emulator]() {
        updateMenuStates(emulator);
    }, Qt::QueuedConnection);
}

void MenuManager::handleEmulatorStateChanged(int id, Message* message)
{
    Q_UNUSED(id);
    Q_UNUSED(message);

    // Ensure we update UI on the main thread
    QMetaObject::invokeMethod(
        this, [this]() { updateMenuStates(_activeEmulator.lock()); }, Qt::QueuedConnection);
}

void MenuManager::handleEmulatorInstanceCreated(int id, Message* message)
{
    Q_UNUSED(id);

    // Handle emulator instance creation - update menu state if we don't have an active emulator
    if (message && message->obj && !_activeEmulator.lock())
    {
        SimpleTextPayload* payload = dynamic_cast<SimpleTextPayload*>(message->obj);
        if (payload)
        {
            std::string createdId = payload->_payloadText;
            auto* emulatorManager = EmulatorManager::GetInstance();
            auto emulator = emulatorManager->GetEmulator(createdId);

            if (emulator)
            {
                // Update menu state on main thread
                QMetaObject::invokeMethod(
                    this, [this, emulator]() {
                        _activeEmulator = emulator;
                        updateMenuStates(emulator);
                    }, Qt::QueuedConnection);
            }
        }
    }
}

void MenuManager::handleFDDDiskChanged(int id, Message* message)
{
    Q_UNUSED(id);

    // Only update if the event is from our active emulator instance
    auto activeEmulator = _activeEmulator.lock();
    if (!activeEmulator || !message || !message->obj)
    {
        return;
    }

    // Check if this event is for our active emulator
    FDDDiskPayload* payload = dynamic_cast<FDDDiskPayload*>(message->obj);
    if (payload)
    {
        // Compare emulator IDs
        std::string activeId = activeEmulator->GetId();
        std::string eventEmulatorId = payload->_emulatorId.toString();
        
        if (activeId == eventEmulatorId)
        {
            // Disk changed in our active emulator - update menu state on main thread
            QMetaObject::invokeMethod(
                this, [this, activeEmulator]() {
                    updateMenuStates(activeEmulator);
                }, Qt::QueuedConnection);
        }
    }
}
