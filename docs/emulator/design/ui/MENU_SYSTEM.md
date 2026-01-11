# Qt Menu System Implementation

## Overview

A comprehensive cross-platform menu system has been implemented for the Unreal Speccy Qt application. The menu provides easy access to all emulator control functions that were previously only available through the async CLI interface.

## Architecture

### Components

1. **MenuManager** (`menumanager.h/cpp`) - Manages all menu creation and signal handling
2. **MainWindow** - Integrates MenuManager and handles menu actions
3. **Cross-platform support** - Native menu bars on macOS, standard menus on Windows/Linux

### Design Principles

- **Signal/Slot architecture** - Clean separation between UI and logic
- **Platform-specific adaptations** - Native menu bars where appropriate
- **Keyboard shortcuts** - Comprehensive shortcut support
- **State management** - Menus update based on emulator state
- **Extensible** - Easy to add new menu items

## Menu Structure

### File Menu
- **Open...** (Ctrl+O) - Open any supported file
- **Open Snapshot...** (Ctrl+Shift+O) - Load snapshot (.z80, .sna, .szx)
- **Open Tape...** (Ctrl+T) - Load tape (.tap, .tzx)
- **Open Disk...** (Ctrl+D) - Load disk image (.trd, .scl, .fdi)
- **Save Snapshot...** (Ctrl+S) - Save current state [TODO]
- **Recent Files** - Recent file list [TODO]
- **Exit** (Ctrl+Q / Cmd+Q) - Exit application

### Edit Menu
- **Preferences...** (Ctrl+P / Cmd+,) - Configure settings [TODO]

### View Menu
- **Debugger** (Ctrl+1) - Toggle debugger window
- **Log Window** (Ctrl+2) - Toggle log window
- **Full Screen** (F11 / Ctrl+Cmd+F) - Toggle full screen
- **Zoom In** (Ctrl++) - Zoom in [TODO]
- **Zoom Out** (Ctrl+-) - Zoom out [TODO]
- **Reset Zoom** (Ctrl+0) - Reset zoom [TODO]

### Run Menu
- **Start** (F5) - Start emulation
- **Pause** (F6) - Pause emulation
- **Resume** (F7) - Resume emulation
- **Stop** (Shift+F5) - Stop emulation
- **Reset** (Ctrl+R) - Reset emulator
- **Speed** submenu:
  - **1x (Normal)** (F1) - Normal speed (3.5 MHz)
  - **2x (Fast)** (F2) - 2x speed (7 MHz)
  - **4x (Very Fast)** (F3) - 4x speed (14 MHz)
  - **8x (Extreme)** (F4) - 8x speed (28 MHz)
  - **16x (Insane)** - 16x speed (56 MHz)
  - **Turbo Mode** (Tab) - Max speed (no sync)

### Debug Menu
- **Debug Mode** (Ctrl+Shift+D) - Enable debug mode
- **Step In** (F8) - Execute one instruction
- **Step Over** (F10) - Execute instruction, skip calls
- **Step Out** (Shift+F8) - Execute until return [TODO]
- **Run to Cursor** (F9) - Execute until cursor [TODO]
- **Toggle Breakpoint** (Ctrl+B) - Toggle breakpoint [TODO]
- **Clear All Breakpoints** (Ctrl+Shift+B) - Remove all breakpoints [TODO]
- **Show Breakpoints...** - Show breakpoints window [TODO]
- **Show Registers** - Show CPU registers [TODO]
- **Show Memory** - Show memory viewer [TODO]

### Tools Menu
- **Settings...** (Ctrl+Alt+S) - Configure emulator [TODO]
- **Take Screenshot** (Ctrl+Shift+S) - Save screenshot [TODO]
- **Record Video...** - Start/stop video recording [TODO]

### Help Menu
- **Documentation** (F1) - View documentation
- **Keyboard Shortcuts** - Show shortcuts
- **About** - About Unreal Speccy

## Keyboard Shortcuts

### Emulation Control
| Shortcut | Action |
|----------|--------|
| F5 | Start emulation |
| F6 | Pause emulation |
| F7 | Resume emulation |
| Shift+F5 | Stop emulation |
| Ctrl+R | Reset emulator |

### Speed Control
| Shortcut | Action |
|----------|--------|
| F1 | 1x speed (Normal) |
| F2 | 2x speed (Fast) |
| F3 | 4x speed (Very Fast) |
| F4 | 8x speed (Extreme) |
| Tab | Hold for Turbo Mode |

### Debug Control
| Shortcut | Action |
|----------|--------|
| F8 | Step In |
| F10 | Step Over |
| Shift+F8 | Step Out |
| F9 | Run to Cursor |
| Ctrl+B | Toggle Breakpoint |
| Ctrl+Shift+B | Clear All Breakpoints |
| Ctrl+Shift+D | Toggle Debug Mode |

### View Control
| Shortcut | Action |
|----------|--------|
| F11 (Win/Linux) | Full Screen |
| Ctrl+Cmd+F (macOS) | Full Screen |
| Ctrl+1 | Toggle Debugger |
| Ctrl+2 | Toggle Log Window |
| Ctrl++ | Zoom In |
| Ctrl+- | Zoom Out |
| Ctrl+0 | Reset Zoom |

### File Operations
| Shortcut | Action |
|----------|--------|
| Ctrl+O | Open File |
| Ctrl+Shift+O | Open Snapshot |
| Ctrl+T | Open Tape |
| Ctrl+D | Open Disk |
| Ctrl+S | Save Snapshot |
| Ctrl+Q (Win/Linux) | Exit |
| Cmd+Q (macOS) | Exit |

## Implementation Details

### MenuManager Class

**Header**: `unreal-qt/src/menumanager.h`
**Source**: `unreal-qt/src/menumanager.cpp`

**Key Methods**:
```cpp
MenuManager(MainWindow* mainWindow, QMenuBar* menuBar, QObject* parent);
void updateMenuStates(bool isRunning, bool isPaused);
void setEmulator(std::shared_ptr<Emulator> emulator);
```

**Signals**:
- File operations: `openFileRequested()`, `openSnapshotRequested()`, etc.
- Emulator control: `startRequested()`, `pauseRequested()`, `resetRequested()`, etc.
- Speed control: `speedMultiplierChanged(int)`, `turboModeToggled(bool)`
- Debug control: `stepInRequested()`, `stepOverRequested()`, `debugModeToggled(bool)`
- View control: `debuggerToggled(bool)`, `logWindowToggled(bool)`, `fullScreenToggled()`

### MainWindow Integration

**New Methods**:
```cpp
// Menu action handlers
void handleStartEmulator();
void handlePauseEmulator();
void handleResumeEmulator();
void handleStopEmulator();
void handleSpeedMultiplierChanged(int multiplier);
void handleTurboModeToggled(bool enabled);
void handleStepIn();
void handleStepOver();
void handleDebugModeToggled(bool enabled);
void handleDebuggerToggled(bool visible);
void handleLogWindowToggled(bool visible);
void updateMenuStates();
```

**Signal Connections** (in constructor):
```cpp
connect(_menuManager, &MenuManager::openFileRequested, this, &MainWindow::openFileDialog);
connect(_menuManager, &MenuManager::startRequested, this, &MainWindow::handleStartEmulator);
connect(_menuManager, &MenuManager::speedMultiplierChanged, this, &MainWindow::handleSpeedMultiplierChanged);
// ... etc
```

### State Management

Menu items are automatically enabled/disabled based on emulator state:

```cpp
void MenuManager::updateMenuStates(bool isRunning, bool isPaused)
{
    _startAction->setEnabled(!isRunning);
    _pauseAction->setEnabled(isRunning && !isPaused);
    _resumeAction->setEnabled(isRunning && isPaused);
    _stopAction->setEnabled(isRunning);
    // ... etc
}
```

Called from:
- `MainWindow::handleStartButton()` - When emulator starts/stops
- `MainWindow::handlePauseEmulator()` - When emulator pauses
- `MainWindow::handleResumeEmulator()` - When emulator resumes
- `MainWindow::handleStopEmulator()` - When emulator stops

## Platform-Specific Behavior

### macOS
- Native menu bar (`setNativeMenuBar(true)`)
- Cmd key for shortcuts (e.g., Cmd+Q for Quit)
- Cmd+, for Preferences
- Ctrl+Cmd+F for Full Screen

### Windows/Linux
- Standard menu bar
- Ctrl key for shortcuts
- F11 for Full Screen
- Ctrl+P for Preferences

## CLI Command Mapping

The menu system provides GUI access to CLI commands:

| CLI Command | Menu Item | Shortcut |
|-------------|-----------|----------|
| `start` | Run → Start | F5 |
| `pause` | Run → Pause | F6 |
| `resume` | Run → Resume | F7 |
| `reset` | Run → Reset | Ctrl+R |
| `step` | Debug → Step In | F8 |
| `stepover` | Debug → Step Over | F10 |
| `speed 1` | Run → Speed → 1x | F1 |
| `speed 2` | Run → Speed → 2x | F2 |
| `speed 4` | Run → Speed → 4x | F3 |
| `debugmode on` | Debug → Debug Mode | Ctrl+Shift+D |

## Future Enhancements

### Planned Features (marked with [TODO])

1. **File Menu**
   - Save Snapshot functionality
   - Recent Files list with persistence

2. **Edit Menu**
   - Preferences dialog with settings tabs

3. **View Menu**
   - Zoom In/Out/Reset functionality
   - Additional window toggles

4. **Debug Menu**
   - Step Out implementation
   - Run to Cursor implementation
   - Toggle Breakpoint at current address
   - Breakpoints window
   - Registers window
   - Memory viewer window

5. **Tools Menu**
   - Settings dialog
   - Screenshot functionality
   - Video recording

### Extensibility

To add a new menu item:

1. **Add action in MenuManager.h**:
```cpp
QAction* _newAction;
```

2. **Create action in MenuManager.cpp**:
```cpp
_newAction = _menu->addAction(tr("New Action"));
_newAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_N));
connect(_newAction, &QAction::triggered, this, &MenuManager::newActionRequested);
```

3. **Add signal in MenuManager.h**:
```cpp
signals:
    void newActionRequested();
```

4. **Add handler in MainWindow.h**:
```cpp
void handleNewAction();
```

5. **Implement handler in MainWindow.cpp**:
```cpp
void MainWindow::handleNewAction()
{
    // Implementation
}
```

6. **Connect signal in MainWindow constructor**:
```cpp
connect(_menuManager, &MenuManager::newActionRequested, this, &MainWindow::handleNewAction);
```

## Testing

### Manual Testing Checklist

- [ ] File menu opens and closes correctly
- [ ] All shortcuts work as expected
- [ ] Menu items enable/disable based on emulator state
- [ ] Speed multipliers work (1x, 2x, 4x, 8x, 16x)
- [ ] Turbo mode toggles correctly
- [ ] Debug mode toggles correctly
- [ ] Step In/Over execute correctly
- [ ] Window toggles work (Debugger, Log)
- [ ] Full screen toggles correctly
- [ ] Platform-specific shortcuts work (macOS vs Windows/Linux)
- [ ] Menu bar appears native on macOS
- [ ] About dialog displays correctly
- [ ] Help dialogs display correctly

### Automated Testing

Consider adding Qt Test cases for:
- Menu creation
- Signal/slot connections
- State updates
- Shortcut handling

## Known Issues

None currently.

## References

- Qt Documentation: https://doc.qt.io/qt-6/qmenubar.html
- Qt Actions: https://doc.qt.io/qt-6/qaction.html
- Qt Shortcuts: https://doc.qt.io/qt-6/qkeysequence.html
- CLI Commands: `core/automation/cli/README.md`
- Speed Control: `SPEED_MULTIPLIER_IMPLEMENTATION.md`

## Change Log

### 2024-01-04
- Initial implementation of MenuManager
- Added File, Edit, View, Run, Debug, Tools, Help menus
- Integrated with MainWindow
- Added comprehensive keyboard shortcuts
- Platform-specific menu adaptations
- State management for menu items
- Documentation created

