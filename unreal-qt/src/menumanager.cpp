#include "menumanager.h"
#include "mainwindow.h"
#include "emulator/emulator.h"

#include <QApplication>
#include <QMessageBox>

MenuManager::MenuManager(MainWindow* mainWindow, QMenuBar* menuBar, QObject* parent)
    : QObject(parent)
    , _mainWindow(mainWindow)
    , _menuBar(menuBar)
{
    createFileMenu();
    createEditMenu();
    createViewMenu();
    createRunMenu();
    createDebugMenu();
    createToolsMenu();
    createHelpMenu();
    
    applyPlatformSpecificSettings();
    
    // Set initial states (no emulator at startup)
    updateMenuStates(nullptr);
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
    
    _fileMenu->addSeparator();
    
    // Save Snapshot
    _saveSnapshotAction = _fileMenu->addAction(tr("&Save Snapshot..."));
    _saveSnapshotAction->setShortcut(QKeySequence::Save);
    _saveSnapshotAction->setStatusTip(tr("Save current emulator state to snapshot"));
    _saveSnapshotAction->setEnabled(false); // TODO: Implement save functionality
    
    _fileMenu->addSeparator();
    
    // Recent Files (placeholder)
    _recentFilesAction = _fileMenu->addAction(tr("Recent Files"));
    _recentFilesAction->setEnabled(false); // TODO: Implement recent files
    
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
    _preferencesAction->setEnabled(false); // TODO: Implement preferences dialog
}

void MenuManager::createViewMenu()
{
    _viewMenu = _menuBar->addMenu(tr("&View"));
    
    // Debugger Window
    _debuggerAction = _viewMenu->addAction(tr("&Debugger"));
    _debuggerAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_1));
    _debuggerAction->setStatusTip(tr("Show/hide debugger window"));
    _debuggerAction->setCheckable(true);
    _debuggerAction->setChecked(true);
    connect(_debuggerAction, &QAction::triggered, this, &MenuManager::debuggerToggled);
    
    // Log Window
    _logWindowAction = _viewMenu->addAction(tr("&Log Window"));
    _logWindowAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_2));
    _logWindowAction->setStatusTip(tr("Show/hide log window"));
    _logWindowAction->setCheckable(true);
    _logWindowAction->setChecked(true);
    connect(_logWindowAction, &QAction::triggered, this, &MenuManager::logWindowToggled);
    
    _viewMenu->addSeparator();
    
    // Full Screen
    _fullScreenAction = _viewMenu->addAction(tr("&Full Screen"));
#ifdef Q_OS_MAC
    _fullScreenAction->setShortcut(QKeySequence(Qt::CTRL | Qt::META | Qt::Key_F));
#else
    _fullScreenAction->setShortcut(QKeySequence(Qt::Key_F11));
#endif
    _fullScreenAction->setStatusTip(tr("Toggle full screen mode"));
    _fullScreenAction->setCheckable(true);
    connect(_fullScreenAction, &QAction::triggered, this, &MenuManager::fullScreenToggled);
    
    _viewMenu->addSeparator();
    
    // Zoom controls
    _zoomInAction = _viewMenu->addAction(tr("Zoom &In"));
    _zoomInAction->setShortcut(QKeySequence::ZoomIn);
    _zoomInAction->setStatusTip(tr("Zoom in (2x)"));
    _zoomInAction->setEnabled(false); // TODO: Implement zoom
    
    _zoomOutAction = _viewMenu->addAction(tr("Zoom &Out"));
    _zoomOutAction->setShortcut(QKeySequence::ZoomOut);
    _zoomOutAction->setStatusTip(tr("Zoom out (0.5x)"));
    _zoomOutAction->setEnabled(false); // TODO: Implement zoom
    
    _zoomResetAction = _viewMenu->addAction(tr("&Reset Zoom"));
    _zoomResetAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_0));
    _zoomResetAction->setStatusTip(tr("Reset zoom to 1x"));
    _zoomResetAction->setEnabled(false); // TODO: Implement zoom
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

void MenuManager::createDebugMenu()
{
    _debugMenu = _menuBar->addMenu(tr("&Debug"));
    
    // Debug Mode
    _debugModeAction = _debugMenu->addAction(tr("Debug &Mode"));
    _debugModeAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    _debugModeAction->setStatusTip(tr("Enable debug mode (slower, instrumented)"));
    _debugModeAction->setCheckable(true);
    connect(_debugModeAction, &QAction::triggered, this, &MenuManager::debugModeToggled);
    
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
    _stepOutAction->setEnabled(false); // TODO: Implement step out
    
    // Run to Cursor
    _runToCursorAction = _debugMenu->addAction(tr("Run to &Cursor"));
    _runToCursorAction->setShortcut(QKeySequence(Qt::Key_F9));
    _runToCursorAction->setStatusTip(tr("Execute until cursor position"));
    _runToCursorAction->setEnabled(false); // TODO: Implement run to cursor
    
    _debugMenu->addSeparator();
    
    // Toggle Breakpoint
    _toggleBreakpointAction = _debugMenu->addAction(tr("&Toggle Breakpoint"));
    _toggleBreakpointAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_B));
    _toggleBreakpointAction->setStatusTip(tr("Toggle breakpoint at current address"));
    _toggleBreakpointAction->setEnabled(false); // TODO: Implement
    
    // Clear All Breakpoints
    _clearAllBreakpointsAction = _debugMenu->addAction(tr("&Clear All Breakpoints"));
    _clearAllBreakpointsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B));
    _clearAllBreakpointsAction->setStatusTip(tr("Remove all breakpoints"));
    _clearAllBreakpointsAction->setEnabled(false); // TODO: Implement
    
    // Show Breakpoints
    _showBreakpointsAction = _debugMenu->addAction(tr("Show &Breakpoints..."));
    _showBreakpointsAction->setStatusTip(tr("Show breakpoints window"));
    _showBreakpointsAction->setEnabled(false); // TODO: Implement
    
    _debugMenu->addSeparator();
    
    // Show Registers
    _showRegistersAction = _debugMenu->addAction(tr("Show &Registers"));
    _showRegistersAction->setStatusTip(tr("Show CPU registers"));
    _showRegistersAction->setEnabled(false); // TODO: Implement
    
    // Show Memory
    _showMemoryAction = _debugMenu->addAction(tr("Show &Memory"));
    _showMemoryAction->setStatusTip(tr("Show memory viewer"));
    _showMemoryAction->setEnabled(false); // TODO: Implement
}

void MenuManager::createToolsMenu()
{
    _toolsMenu = _menuBar->addMenu(tr("&Tools"));
    
    // Settings
    _settingsAction = _toolsMenu->addAction(tr("&Settings..."));
    _settingsAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_S));
    _settingsAction->setStatusTip(tr("Configure emulator settings"));
    _settingsAction->setEnabled(false); // TODO: Implement settings dialog
    
    _toolsMenu->addSeparator();
    
    // Screenshot
    _screenshotAction = _toolsMenu->addAction(tr("Take &Screenshot"));
    _screenshotAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    _screenshotAction->setStatusTip(tr("Save screenshot to file"));
    _screenshotAction->setEnabled(false); // TODO: Implement screenshot
    
    // Record Video
    _recordVideoAction = _toolsMenu->addAction(tr("Record &Video..."));
    _recordVideoAction->setStatusTip(tr("Start/stop video recording"));
    _recordVideoAction->setCheckable(true);
    _recordVideoAction->setEnabled(false); // TODO: Implement video recording
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
            "F11 - Full Screen\n"
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
               "<p>&copy; 2024 Unreal Speccy Project</p>").arg(QT_VERSION_STR));
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
    
    // File menu - always enabled
    
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
}

void MenuManager::setActiveEmulator(std::shared_ptr<Emulator> emulator)
{
    // Simply update menu states - they will query the emulator directly
    updateMenuStates(emulator);
}

