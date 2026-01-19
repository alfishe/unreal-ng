#pragma once

#include <QAction>
#include <QActionGroup>
#include <QMenu>
#include <QMenuBar>
#include <QObject>
#include <memory>

class Emulator;
class MainWindow;

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

    // Set the current active emulator instance
    void setActiveEmulator(std::shared_ptr<Emulator> emulator);

    // Observer callback for emulator state changes
    void handleEmulatorStateChanged(int id, Message* message);
    void handleEmulatorInstanceCreated(int id, Message* message);

signals:
    // Signal emitted when user requests to open a file
    void openFileRequested();
    void openSnapshotRequested();
    void openTapeRequested();
    void openDiskRequested();

    // Emulator control signals
    void startRequested();
    void pauseRequested();
    void resumeRequested();
    void resetRequested();
    void stopRequested();

    // Speed control signals
    void speedMultiplierChanged(int multiplier);
    void turboModeToggled(bool enabled);

    // Debug signals
    void stepInRequested();
    void stepOverRequested();
    void debugModeToggled(bool enabled);

    // View signals
    void debuggerToggled(bool visible);
    void logWindowToggled(bool visible);
    void fullScreenToggled();

    // Tools signals
    void intParametersRequested();

private:
    void createFileMenu();
    void createEditMenu();
    void createViewMenu();
    void createRunMenu();
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
    QMenu* _debugMenu;
    QMenu* _toolsMenu;
    QMenu* _helpMenu;

    // File Menu Actions
    QAction* _openAction;
    QAction* _openSnapshotAction;
    QAction* _openTapeAction;
    QAction* _openDiskAction;
    QAction* _saveSnapshotAction;
    QAction* _recentFilesAction;
    QAction* _exitAction;

    // Edit Menu Actions
    QAction* _preferencesAction;

    // View Menu Actions
    QAction* _debuggerAction;
    QAction* _logWindowAction;
    QAction* _fullScreenAction;
    QAction* _zoomInAction;
    QAction* _zoomOutAction;
    QAction* _zoomResetAction;

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

    // Debug Menu Actions
    QAction* _debugModeAction;
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
    QAction* _screenshotAction;
    QAction* _recordVideoAction;

    // Help Menu Actions
    QAction* _aboutAction;
    QAction* _documentationAction;
    QAction* _keyboardShortcutsAction;
};
