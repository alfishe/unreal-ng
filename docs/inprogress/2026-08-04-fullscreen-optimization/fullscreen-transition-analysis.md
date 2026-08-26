# Fullscreen Transition Analysis and Optimization Proposal

**Date:** 2026-08-04  
**Status:** Analysis Complete, Proposal Ready  
**Platforms:** macOS (primary), Windows, Linux

## Executive Summary

Fullscreen transitions exhibit visual glitches and lack smooth animations due to:
1. Excessive hide/show cycles triggering multiple redraws
2. Synchronous window flag changes causing flicker
3. Multiple deferred geometry restorations via `QTimer::singleShot`
4. Redundant palette/style updates during state changes

## Current Implementation Analysis

### macOS (`handleWindowStateChangeMacOS` + `handleFullScreenShortcutMacOS`)

**Problems Identified:**

1. **Redundant `hide()` call** (line 561):
   ```cpp
   else if (newState & Qt::WindowFullScreen)
   {
       hide();  // <-- PROBLEM: Causes black flash before fullscreen
       _isFullScreen = true;
   ```
   The `hide()` before `showFullScreen()` causes a visible black flash. Qt's `showFullScreen()` handles the transition internally.

2. **Synchronous window flag manipulation** (lines 581, 599-602):
   ```cpp
   setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
   showFullScreen();
   // ...
   setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
   ```
   Changing window flags triggers a hide/show cycle internally. Combined with explicit `hide()`/`show()` calls, this creates 2-3 redraws.

3. **Palette changes during transition** (lines 572-574, 592):
   ```cpp
   QPalette palette;
   palette.setColor(QPalette::Window, Qt::black);
   setPalette(palette);
   ```
   Palette changes trigger immediate widget updates while the window is in flux.

4. **Multiple `QTimer::singleShot` deferrals** (lines 1226-1234):
   ```cpp
   QTimer::singleShot(100, this, [this]() {
       if (_dockingManager)
       {
           _dockingManager->onExitFullscreen();
           QTimer::singleShot(100, this, [this]() {  // Nested!
               if (_dockingManager)
                   _dockingManager->setSnappingLocked(false);
           });
       }
   });
   ```
   Nested timers with 100ms delays create 200-300ms of staged operations, each potentially triggering redraws.

5. **Duplicate state handling** - The shortcut handler (`handleFullScreenShortcutMacOS`) and state change handler (`handleWindowStateChangeMacOS`) both perform similar operations, causing redundant work:
   - Both check/save geometry
   - Both manipulate palette
   - Both show/hide UI elements

### Windows (`handleWindowStateChangeWindows` + `handleFullScreenShortcutWindows`)

**Additional Problems:**

1. **`FramelessWindowHint` manipulation** (lines 1093, 1182):
   - Same issue as macOS - flag changes trigger internal hide/show

2. **Multiple geometry restoration timers** (lines 1115, 1127-1133):
   ```cpp
   QTimer::singleShot(100, this, [guard, savedGeom]() { ... });
   // ...
   QTimer::singleShot(200, this, [guard]() { ... });
   ```
   Staggered timers at 100ms and 200ms create visible geometry jumps.

3. **Focus management overhead** (lines 1136-1143):
   ```cpp
   activateWindow();
   raise();
   if (deviceScreen)
   {
       deviceScreen->setFocus();
   }
   ```
   These calls during state transitions can cause additional redraws.

### Linux (`handleWindowStateChangeLinux` + `handleFullScreenShortcutLinux`)

**Additional Problems:**

1. **Verification timer pattern** (lines 1336-1344):
   ```cpp
   QTimer::singleShot(100, this, [this]() {
       setGeometry(_normalGeometry);
       QTimer::singleShot(50, this, [this]() {  // Verification
           if (actualGeometry != _normalGeometry)
               setGeometry(_normalGeometry);  // Re-apply!
       });
   });
   ```
   Double geometry application with verification adds 150ms+ of visible adjustments.

2. **Window manager async handling** causes multiple resize events during a single transition.

### DockingManager Impact

The `DockingManager::onEnterFullscreen()` and `onExitFullscreen()` methods add overhead:

1. **Window enumeration and hiding** (lines 244-264):
   - Iterates all docked windows
   - Saves geometry for each
   - Hides each window individually (each `hide()` is a potential redraw)

2. **Multi-screen window relocation** (lines 289-302):
   - Moves windows to secondary screen during fullscreen
   - Shows each window with cascading positions
   - Each `move()` + `show()` is a redraw

3. **Exit restoration** (lines 306-336):
   - Restores geometry per window
   - Shows each window with `WA_ShowWithoutActivating`
   - Lowers each window for z-order

## Optimization Proposals

### Priority 1: Eliminate Redundant Operations (High Impact)

#### 1.1 Remove explicit `hide()` calls before `showFullScreen()`

**macOS** - Remove line 561:
```cpp
// Before:
hide();
_isFullScreen = true;
// After:
_isFullScreen = true;
// showFullScreen() handles the transition
```

#### 1.2 Batch UI element visibility changes

Instead of:
```cpp
statusBar()->hide();
startButton->hide();
menuBar()->hide();
```

Use:
```cpp
setUpdatesEnabled(false);
statusBar()->hide();
startButton->hide();
menuBar()->hide();
setUpdatesEnabled(true);
```

#### 1.3 Defer palette changes until after state transition

```cpp
// Move palette changes to after showFullScreen() completes
showFullScreen();
QTimer::singleShot(0, this, [this]() {
    QPalette palette;
    palette.setColor(QPalette::Window, Qt::black);
    setPalette(palette);
});
```

### Priority 2: Consolidate Timer Chains (Medium Impact)

#### 2.1 Replace nested timers with single deferred operation

**Before:**
```cpp
QTimer::singleShot(100, this, [this]() {
    _dockingManager->onExitFullscreen();
    QTimer::singleShot(100, this, [this]() {
        _dockingManager->setSnappingLocked(false);
    });
});
```

**After:**
```cpp
QTimer::singleShot(150, this, [this]() {
    if (_dockingManager) {
        _dockingManager->onExitFullscreen();
        _dockingManager->setSnappingLocked(false);
    }
});
```

#### 2.2 Use single-shot for all post-transition work

Create a unified `finalizeFullscreenTransition()` method:
```cpp
void MainWindow::finalizeFullscreenTransition(bool entering)
{
    if (entering) {
        // All entering-fullscreen post-work
    } else {
        // All exiting-fullscreen post-work
    }
}
```

### Priority 3: Window Flag Optimization (Medium Impact)

#### 3.1 Avoid FramelessWindowHint manipulation

On macOS, `showFullScreen()` already provides frameless behavior. The explicit `FramelessWindowHint` toggle is unnecessary:

```cpp
// Remove these lines:
setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
// ...
setWindowFlags(windowFlags() & ~Qt::FramelessWindowHint);
```

If frameless is required for other reasons, batch the flag change with visibility:
```cpp
setUpdatesEnabled(false);
setWindowFlags(newFlags);
showFullScreen();
setUpdatesEnabled(true);
```

### Priority 4: DockingManager Optimization (Low-Medium Impact)

#### 4.1 Batch docked window operations

```cpp
void DockingManager::onEnterFullscreen()
{
    // Disable updates for all windows first
    for (auto* window : _dockableWindows.keys())
        window->setUpdatesEnabled(false);
    
    // Do all hide operations
    for (auto* window : _dockableWindows.keys())
        window->hide();
    
    // Re-enable updates
    for (auto* window : _dockableWindows.keys())
        window->setUpdatesEnabled(true);
}
```

#### 4.2 Consider async window management

For multi-screen scenarios, defer secondary screen window placement to avoid blocking the main transition.

### Priority 5: Platform-Specific Native APIs (Future Enhancement)

#### 5.1 macOS: Use NSWindow animations

```objc
// Objective-C++ integration
NSWindow* nsWindow = (__bridge NSWindow*)winId();
[nsWindow setAnimationBehavior:NSWindowAnimationBehaviorDocumentWindow];
```

#### 5.2 Windows: Use DWM transitions

```cpp
// Windows-specific smooth transition
DwmSetWindowAttribute(hwnd, DWMWA_TRANSITIONS_FORCEDISABLED, &FALSE, sizeof(FALSE));
```

## Platform Comparison Matrix

| Issue | macOS | Windows | Linux |
|-------|-------|---------|-------|
| Redundant hide() | Yes | No | No |
| FramelessWindowHint toggle | Yes | Yes | No |
| Nested timer chains | Yes | Yes | Yes |
| Verification re-apply | No | No | Yes |
| Docking manager overhead | Yes | Yes | Yes |
| Palette during transition | Yes | Yes | Yes |

## Recommended Implementation Order

1. **Phase 1 (Quick Wins):**
   - Remove `hide()` before `showFullScreen()` on macOS
   - Add `setUpdatesEnabled()` wrapping for UI element changes
   - Remove unnecessary `FramelessWindowHint` toggling

2. **Phase 2 (Timer Consolidation):**
   - Replace nested timer chains with single deferred calls
   - Create unified `finalizeFullscreenTransition()` method

3. **Phase 3 (DockingManager):**
   - Batch docked window hide/show operations
   - Consider async secondary-screen placement

4. **Phase 4 (Platform Native):**
   - Investigate NSWindow animation APIs for macOS
   - Investigate DWM for Windows

## Metrics to Track

- Time from shortcut press to fullscreen completion
- Number of paint events during transition
- Visual frame drops (user perception)

## Testing Checklist

- [ ] Normal → Fullscreen → Normal cycle
- [ ] Maximized → Fullscreen → Maximized cycle
- [ ] Normal → Maximized → Fullscreen → Maximized → Normal chain
- [ ] Multi-monitor: docked windows behavior
- [ ] Keyboard focus preserved after transition
- [ ] No geometry drift after multiple cycles
