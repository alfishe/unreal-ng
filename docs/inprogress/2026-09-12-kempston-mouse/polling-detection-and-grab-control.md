# Kempston Mouse — Polling Detection & Cursor Grab Control

Enhancement to [design.md](design.md): automatic cursor grab based on actual mouse usage,
plus manual override.

**Review status:** Revised per architectural review 2026-09-13.

---

## 1. Problem statement

The current design assumes cursor grabbing is controlled by `CONFIG::lockmouse` alone.
This leads to two UX problems:

1. **Unnecessary grab on reset / focus.** If the loaded software does not use the Kempston
   mouse, grabbing the cursor is unwanted — the user cannot interact with the emulator UI
   without manually releasing the grab.
2. **No override once polling starts.** Some software polls the mouse ports even when not
   using the mouse (presence detection, menu systems). There is no way for the user to say
   "I don't want the cursor grabbed even though the software is polling."

## 2. Goals

| # | Goal |
|---|---|
| G-1 | Detect whether Kempston mouse ports were polled in the last ~1 second |
| G-2 | Do not grab cursor if no polling since reset or window focus |
| G-3 | Provide a toolbar icon (crossed mouse) to manually disable grabbing |
| G-4 | Detection must be fast — checked on every focus event and periodically |

## 3. Polling detection architecture

### 3.0 Layer separation

Per project architecture (`AGENTS.md`), `core/` is a pure emulator engine decoupled from
GUI. The core has **no concept of cursor grab, armed states, or host windowing policies**.

| Layer | Responsibility |
|---|---|
| **Core (`Mouse` peripheral)** | Tracks polling state, exposes via `EmulatorState`, emits `NC_MOUSE_POLLING_ACTIVE` / `NC_MOUSE_POLLING_INACTIVE` events on state transitions |
| **UI (`CursorGrabController`)** | Subscribes to polling events, owns grab state machine, decides if/when to grab host cursor |

**Event-driven, not polling:** UI subscribes to core events. No UI→core polling.

### 3.1 The metric: port read timestamp

The simplest reliable signal is **the most recent T-state at which a Kempston mouse port
was read**. This is already the point where the port decoder dispatches the read, so the
instrumentation is minimal.

```cpp
// In Mouse class (core/src/emulator/io/mouse/mouse.h)
class Mouse {
public:
    // Called from port decoder on every Kempston mouse port read
    void recordPoll(uint64_t tState, uint32_t frameNumber);
    
    // Called from frame loop to check for polling timeout
    void checkPollingTimeout(uint64_t currentTState, uint64_t timeoutTStates);
    
    void resetPollTracking();
    
    // State exposed via EmulatorState for debugger/UI inspection
    bool isPollingActive() const { return _pollingActive; }
    
private:
    EmulatorContext* _context;
    uint64_t _lastPollTState = 0;
    uint32_t _firstPollFrame = 0;
    bool _pollingActive = false;
    
    void setPollingActive(bool active);  // Emits event on state change
};
```

```cpp
// mouse.cpp
void Mouse::recordPoll(uint64_t tState, uint32_t frameNumber) {
    uint64_t prev = _lastPollTState;
    _lastPollTState = tState;
    
    if (prev == 0) {
        _firstPollFrame = frameNumber;
    } else if (!_pollingActive && frameNumber != _firstPollFrame) {
        // Reads in 2+ distinct frames → sustained polling confirmed
        setPollingActive(true);
    }
}

void Mouse::checkPollingTimeout(uint64_t currentTState, uint64_t timeoutTStates) {
    if (_pollingActive && _lastPollTState != 0) {
        // Guard against TTD underflow
        if (currentTState >= _lastPollTState && 
            (currentTState - _lastPollTState) > timeoutTStates) {
            setPollingActive(false);
        }
    }
}

void Mouse::setPollingActive(bool active) {
    if (_pollingActive == active) return;
    _pollingActive = active;
    
    // Update EmulatorState for debugger visibility
    _context->emulatorState.mousePollingActive = active;
    
    // Emit event for UI subscription
    auto* payload = new TargetedPayload(_context->pEmulator->GetUUID());
    MessageCenter::DefaultMessageCenter().Post(
        active ? NC_MOUSE_POLLING_ACTIVE : NC_MOUSE_POLLING_INACTIVE,
        payload
    );
}

void Mouse::resetPollTracking() {
    _lastPollTState = 0;
    _firstPollFrame = 0;
    if (_pollingActive) {
        setPollingActive(false);
    }
}
```

**Events emitted:**
- `NC_MOUSE_POLLING_ACTIVE` — sustained polling started (reads in 2+ frames)
- `NC_MOUSE_POLLING_INACTIVE` — polling timeout (1 second silence)

**State exposed:** `EmulatorState::mousePollingActive` — available for:
- Debugger/inspector UI display
- **Backup polling** on focus gain (UI can check current state without waiting for event)

Events are primary; state query is the fallback for cases like focus regain where the
UI needs to know current state immediately.

Plain `uint64_t` would cause undefined behavior and potential torn reads on 32-bit targets.

### 3.2 Where to record

In the port decoder's Kempston mouse arm, after confirming the port decodes as mouse:

```cpp
// In Default_Port_KempstonMouse_In() or per-model equivalent
if (_mouse && _mouse->isPresent()) {
    _mouse->recordPoll(_context->pCore->GetZ80()->GetTotalTStates());
    result = _mouse->readRegister(reg);
}
```

### 3.3 Polling window calculation

A "1 second window" in T-states depends on CPU frequency:

| Mode | T-states per second |
|---|---|
| 3.5 MHz (base) | 3,500,000 |
| 7 MHz (turbo) | 7,000,000 |

The check uses the current effective frequency:

```cpp
uint64_t windowTStates = _context->pCore->GetEffectiveClockHz();  // ~1 second
bool polledRecently = _mouse->wasPolledWithin(currentTState, windowTStates);
```

### 3.4 When to reset the tracker

| Event | Action |
|---|---|
| Machine reset | `resetPollTracking()` — fresh start |
| Snapshot load | `resetPollTracking()` — cannot assume snapshot's polling state |
| Focus loss | No action — retain the information for focus regain |
| Focus regain | Check `wasPolledWithin()` to decide whether to grab |

### 3.5 Integration with focus events

```cpp
// In MainWindow or DeviceScreen focus handling
void onFocusIn() {
    if (!_grabManuallyDisabled && _mouse && _grabEnabled) {
        uint64_t now = _emulator->GetCurrentTState();
        if (_mouse->wasPolledWithin(now, _emulator->GetEffectiveClockHz())) {
            engageCursorGrab();
        }
        // else: wait for polling to start
    }
}
```

### 3.6 Deferred grab — wait for sustained polling

When focus is gained with no recent polling, the grab should **arm** but not engage.
The grab engages only after **sustained polling** is detected.

```cpp
// State machine (UI layer — CursorGrabController helper in DeviceScreen)
enum class GrabState {
    Disabled,       // Manual override or no mouse configured or emulator paused
    Armed,          // Waiting for sustained polling
    Engaged,        // Cursor grabbed
    Released        // Temporarily released (e.g., Escape pressed, menu open)
};
```

**Why sustained polling, not first poll?** Many Spectrum titles (and the Scorpion ROM
Service Monitor) probe ports `#FBDF`/`#FFDF`/`#FADF` once or twice at startup to detect
hardware presence. If no mouse is detected, they revert to keyboard/joystick and never
poll again. Engaging grab on the first read would cause every launch of such software to
snap the cursor to center and hide it, only to un-grab 1 second later.

**Sustained polling threshold:** Reads detected in **2 distinct frames**. The core
`Mouse::recordPoll()` tracks `_firstPollFrame` and sets `_pollActive = true` only when
a read occurs in a different frame. Single-frame boot probes never trigger the
notification. See §3.1.

**UI subscribes to events:**

```cpp
// In CursorGrabController setup
MessageCenter::DefaultMessageCenter().Subscribe(NC_MOUSE_POLLING_ACTIVE, this);
MessageCenter::DefaultMessageCenter().Subscribe(NC_MOUSE_POLLING_INACTIVE, this);
```

```cpp
// Event handlers (marshalled to Qt main thread)
void CursorGrabController::onPollingActive(const std::string& emulatorId) {
    if (emulatorId != _boundEmulatorId) return;
    
    QMetaObject::invokeMethod(this, [this]() {
        if (_grabState == GrabState::Armed) {
            transitionTo(GrabState::Engaged);
        }
    }, Qt::QueuedConnection);
}

void CursorGrabController::onPollingInactive(const std::string& emulatorId) {
    if (emulatorId != _boundEmulatorId) return;
    
    QMetaObject::invokeMethod(this, [this]() {
        if (_grabState == GrabState::Engaged) {
            transitionTo(GrabState::Armed);
        }
    }, Qt::QueuedConnection);
}
```

**On focus gain (backup poll):**

```cpp
void CursorGrabController::onFocusIn() {
    // Check current state immediately — don't wait for event
    if (_emulator->GetState().mousePollingActive) {
        transitionTo(GrabState::Engaged);
    } else {
        transitionTo(GrabState::Armed);
    }
}
```

## 4. Manual grab disable — toolbar icon

### 4.1 Design

A toggle action in the toolbar showing a mouse icon when grabbing is allowed, and a
crossed-out mouse when grabbing is manually disabled.

| State | Icon | Tooltip |
|---|---|---|
| Grab allowed (default) | `mouse` | "Mouse capture enabled — click to disable" |
| Grab disabled | `mouse-off` | "Mouse capture disabled — click to enable" |

The icon is **always visible**, not hidden when the mouse peripheral is absent — this
makes the control discoverable and allows pre-emptive disable before loading software.

### 4.2 Placement

**Menu + shortcut (primary):** Define the action in `MenuManager` under the existing
`Control` menu (alongside Start/Pause/Restart), with a default shortcut (e.g., `Ctrl+G`).
This ensures accessibility even when the toolbar is hidden (`View → Toolbar` toggle).

```cpp
// In MenuManager (add to existing _controlMenu)
_mouseCaptureAction = new QAction(tr("Mouse Capture"), this);
_mouseCaptureAction->setCheckable(true);
_mouseCaptureAction->setChecked(true);
_mouseCaptureAction->setShortcut(QKeySequence(tr("Ctrl+G")));
_controlMenu->addSeparator();
_controlMenu->addAction(_mouseCaptureAction);
```

**Toolbar (secondary):** Borrow the action from `MenuManager` (matching how
`fullScreenAction` and `overscanAction` are shared).

```cpp
// In ToolBarManager
QAction* mouseCapture = _menuManager->mouseCaptureAction();
mouseCapture->setIcon(tintedSvgIcon(QStringLiteral("mouse")));
mouseCapture->setIconVisibleInMenu(false);

_toolBar->addSeparator();
_toolBar->addAction(mouseCapture);
```

### 4.3 Icon assets

Two SVG icons needed in `unreal-qt/resources/icons/`:

- `mouse.svg` — standard mouse pointer or mouse device icon
- `mouse-off.svg` — same icon with a diagonal strike-through

The `tintedSvgIcon()` helper handles light/dark theme tinting.

### 4.4 Status bar feedback

When the mouse is captured, update `StatusBarManager` with a status indicator:

```cpp
// In StatusBarManager or via signal from CursorGrabController
void StatusBarManager::setMouseCaptureStatus(bool captured) {
    if (captured) {
        showMessage(tr("Mouse captured (Press Esc to release)"), 3000);
        // Or use a persistent indicator widget
    }
}
```

This provides immediate feedback, especially useful when the toolbar is hidden.

### 4.5 State persistence

Stored in settings alongside other toolbar state:

```cpp
// In ToolBarManager
constexpr const char* kMouseGrabKey = "Input/MouseGrabEnabled";

void ToolBarManager::restoreSettings() {
    QSettings settings(...);
    bool grabEnabled = settings.value(QLatin1String(kMouseGrabKey), true).toBool();
    _mouseGrabAction->setChecked(grabEnabled);
}

void ToolBarManager::saveSettings() const {
    QSettings settings(...);
    settings.setValue(QLatin1String(kMouseGrabKey), _mouseGrabAction->isChecked());
}
```

### 4.6 Signal flow

```
ToolBarManager::mouseGrabToggled(bool enabled)
    → MainWindow::setMouseGrabEnabled(enabled)
        → if (!enabled) releaseCursorGrab();
        → _mouseInputEnabled = enabled;
        → update icon + tooltip
```

### 4.7 Input suppression when disabled

When the toolbar icon is toggled off (grab disabled):

1. **Release cursor grab immediately** — cursor reappears
2. **Stop posting mouse events to MessageCenter** — no `NC_MOUSE_MOVE`, `NC_MOUSE_BUTTON`
3. **Stop calling `recordPoll()` instrumentation** — polling detection freezes (irrelevant
   while input is suppressed)

The `_mouseInputEnabled` flag is checked at the earliest point in the input path:

```cpp
// In DeviceScreen::mouseMoveEvent / mouseButtonEvent
void DeviceScreen::mouseMoveEvent(QMouseEvent* event) {
    if (!_mouseInputEnabled) {
        event->ignore();  // Let parent handle (UI interaction)
        return;
    }
    
    // ... normal mouse delta processing and MessageCenter post
}
```

This ensures:
- No stale deltas accumulate while disabled
- Re-enabling starts fresh (no jump)
- The emulated mouse sees exactly nothing while the icon is crossed out

The toolbar icon state is the **master switch** — it overrides everything else:

| Icon state | Grab | Mouse events to emulator |
|---|---|---|
| Enabled (mouse icon) | Automatic (polling-based) | Yes |
| Disabled (crossed mouse) | Never | Never |

## 5. Grab lifecycle

### 5.1 State transitions

```
                    ┌──────────────────────────────────────────┐
                    │                                          │
                    ▼                                          │
    [Disabled] ◄──────► [Armed] ──(first poll)──► [Engaged]    │
        ▲                  ▲                          │        │
        │                  │                          │        │
        │                  └────────(timeout)─────────┘        │
        │                                                      │
        └───────────(manual toggle)────────────────────────────┘
```

| Transition | Trigger |
|---|---|
| → Disabled | Manual toggle off (menu/toolbar), no mouse configured, or emulator paused |
| → Armed | Focus gained + `!mousePollingActive`, grab enabled |
| → Engaged | Focus gained + `mousePollingActive`, grab enabled |
| Armed → Engaged | `NC_MOUSE_POLLING_ACTIVE` event received |
| Engaged → Armed | `NC_MOUSE_POLLING_INACTIVE` event received |
| Engaged → Released | Esc pressed, menu opened, or dialog shown |
| Released → Engaged | Menu/dialog closed + `mousePollingActive` still true |
| Released (via Esc) → Engaged | Explicit left-click on canvas (see §5.4) |

**Disabled is special:** In `Disabled` state, mouse input is completely suppressed — no
events reach the emulator, no grab occurs, no polling detection runs. This is the "I don't
want mouse emulation right now" state, controlled by the toolbar icon. The other states
(`Armed`, `Engaged`, `Released`) all allow mouse input to flow; they differ only in
whether the cursor is grabbed.

### 5.2 Click-to-focus handling

When the window is unfocused and the user clicks into `DeviceScreen` to focus it:

**The activating click is consumed, not injected into emulation.** This prevents
accidental in-game actions from the focus click.

```cpp
void CursorGrabController::onCanvasMousePress(QMouseEvent* event) {
    if (_consumeNextClick) {
        _consumeNextClick = false;
        event->accept();
        return;  // Swallow the focus-gaining click
    }
    
    // ... normal button handling
}

void CursorGrabController::onFocusIn() {
    _consumeNextClick = true;  // Next click is the focus-gaining click
    // ... rest of focus handling
}
```

### 5.3 Release triggers

The grab should release (without disabling) when:

- User presses Escape (common "release grab" convention) — see §5.3
- Menu bar activated
- Dialog opened (file open, settings, etc.)
- Focus lost
- **Emulator paused, stopped, or single-stepping** — auto-grab must be inhibited during
  debugging, otherwise the cursor gets grabbed while the user is trying to inspect code

**Cursor position restoration:** On release, the cursor reappears at the position it
was grabbed. Store `_preGrabPos = QCursor::pos()` on engagement, restore via
`QCursor::setPos(_preGrabPos)` on release.

**Re-engagement after release:**
- After Escape: requires explicit left-click on canvas (§5.3)
- After menu/dialog: re-engages if polling still active when menu closes
- After focus regain: transitions to Armed, requires sustained polling

### 5.4 Escape key handling

Escape must be caught **before** it reaches the emulated keyboard, to release the grab:

```cpp
// In CursorGrabController key handling
if (key == Qt::Key_Escape && _grabState == GrabState::Engaged) {
    releaseCursorGrab();
    _grabState = GrabState::Released;
    _requireClickToReEngage = true;  // CRITICAL: prevent re-grab loop
    return;  // Do not inject Escape into emulator
}
```

This is a deliberate key steal — the user's intent to escape the grab takes priority.
Note: this matches the original Unreal Speccy behaviour (`input.cpp` Escape handling).

**The Escape re-grab loop problem:** If a game is actively polling the mouse when the
user presses Escape, the window still has focus and polling continues. Without the
`_requireClickToReEngage` flag, the grab would re-engage within 20 ms, trapping the user.

**Fix:** Once released via Escape, the grab stays released until:
1. User explicitly **left-clicks inside the emulation screen area**, or
2. Focus is lost and regained (resets to Armed state)

```cpp
void CursorGrabController::onCanvasClick() {
    if (_grabState == GrabState::Released && _requireClickToReEngage) {
        _requireClickToReEngage = false;
        if (isPollingActive())
            transitionTo(GrabState::Engaged);
    }
}
```

## 6. Gaps identified in the base design

### 6.1 ~~Gap: No cursor grab implementation at all~~ RESOLVED

**Now specified in [design.md §5.6a](design.md#56a-cursor-grab-implementation-qt):**

- Location: `DeviceScreen` with `CursorGrabController` helper class (for testability)
- Cursor hidden via `setCursor(Qt::BlankCursor)`, restored via `setCursor(Qt::ArrowCursor)`
- Warp-event discard via **position matching** (±1px tolerance), not a flag
- Re-centre on every event via `QCursor::setPos(mapToGlobal(rect().center()))`
- Pre-grab cursor position stored in `_preGrabPos`, restored on release
- Full code for `engageCursorGrab()`, `releaseCursorGrab()`, `mouseMoveEvent()`,
  `computeUpscaleFactors()`, button handling, and focus events

### 6.2 ~~Gap: Warp-event detection needs specification~~ RESOLVED

**Now fully implemented in [design.md §5.6a](design.md#56a-cursor-grab-implementation-qt).**

The `mouseMoveEvent()` code handles warp-event detection via **position matching** (not a
flag — flags race with queued events), computes delta from `_warpTarget`, applies DPI and
upscale conversion, accumulates in float with remainder carry, and posts to MessageCenter.

### 6.3 Gap: Multi-monitor cursor escape

When the cursor is grabbed (hidden + re-centred), moving the mouse quickly can still
"escape" onto another monitor before the re-centre fires. Qt has no confinement API.

**Mitigations (not solutions):**

1. Re-centre on every event (already planned)
2. Ignore events with very large deltas (heuristic, can drop fast movements)
3. Accept that Qt grab is leaky and document the limitation

This is a known Qt limitation — SDL's `SDL_SetRelativeMouseMode` handles it at the
platform level. For a Qt front end, option 1 + 3 is the pragmatic choice.

### 6.3a Limitation: Linux Wayland

Under Linux Wayland, `QCursor::setPos()` is a **no-op** by compositor security policy.
Without warping, relative mouse mode via Qt re-centring cannot function on Wayland.

**Recommendation:** Document this explicitly. For Linux Wayland, pointer confinement
requires SDL or the `zwp_confined_pointer_v1` Wayland protocol. This is a platform
limitation, not something this design can work around in Qt.

### 6.3b No grabMouse() — menu accessibility

`QWidget::grabMouse()` intercepts all mouse events for that widget. Clicking on
`QMenuBar`, toolbars, or dock widgets would fail.

**Fix:** Do NOT call `grabMouse()`. Rely on:
- `setCursor(Qt::BlankCursor)` for hiding
- Event filtering for input capture
- `QApplication::focusChanged` signal to detect menu/dialog activation and release grab

This is already reflected in the [design.md §5.6a](design.md#56a-cursor-grab-implementation-qt)
`engageCursorGrab()` code.

### 6.4 Event-driven polling notifications

Core emits events on polling state transitions. UI subscribes and reacts.

```cpp
// platform.h additions
constexpr char const* NC_MOUSE_POLLING_ACTIVE = "MOUSE_POLLING_ACTIVE";
constexpr char const* NC_MOUSE_POLLING_INACTIVE = "MOUSE_POLLING_INACTIVE";
```

**Why events over UI polling:**
1. Immediate response — no frame-rate delay
2. Clear separation — core pushes state changes, UI reacts
3. Subscription model — only interested UIs receive notifications

**Thread marshalling:** MessageCenter delivers on worker thread. UI handlers use
`QMetaObject::invokeMethod(..., Qt::QueuedConnection)` to marshal to Qt main thread.

**Backup query:** `EmulatorState::mousePollingActive` available for focus-gain
scenarios where UI needs current state immediately. See §3.6.

### 6.5 Polling timeout handling

**Core checks timeout in frame loop:**

```cpp
// In Core::onFrameComplete() or equivalent
void Core::onFrameComplete() {
    if (_mouse) {
        uint64_t now = GetCurrentTState();
        uint64_t timeout = GetEffectiveClockHz();  // ~1 second in T-states
        _mouse->checkPollingTimeout(now, timeout);
    }
}
```

This emits `NC_MOUSE_POLLING_INACTIVE` when polling times out. UI receives the event
and transitions from `Engaged → Armed`. See §3.1 for `checkPollingTimeout()` impl.

## 7. Test plan

| Test | Layer | Assertion |
|---|---|---|
| recordPoll updates timestamp | Unit | `lastPollTState()` returns the recorded value |
| wasPolledWithin window check | Unit | True within window, false outside |
| resetPollTracking clears state | Unit | `wasPolledWithin()` returns false after reset |
| **TTD_ReverseStep_NoUnderflow** | Unit | `wasPolledWithin` handles `currentTState < _lastPollTState` safely (no underflow) |
| First poll triggers notification | Integration | `NC_MOUSE_POLL_STARTED` posted on first read |
| Timeout triggers notification | Integration | `NC_MOUSE_POLL_STOPPED` posted after silence |
| **MultiInstance_UUIDFilter** | Integration | Polling in instance A does not trigger grab in instance B |
| Manual disable prevents grab | UI | Clicking icon prevents engageCursorGrab |
| Icon state persists across restart | UI | Setting survives app restart |
| Escape releases grab | UI | Grab released, key not sent to emulator |
| **EscapeKey_NoReGrabLoop** | UI | Pressing Esc releases grab; remains released until explicit canvas click |
| Focus loss releases grab | UI | Grab released on FocusOut event |
| Focus regain with recent poll grabs | UI | Grab engages if sustained poll detected |
| Focus regain without poll arms | UI | Grab armed, not engaged |
| **Pause_InhibitsGrab** | Integration | Gaining focus while emulator is paused does not engage cursor capture |
| **WarpEvent_PositionDiscard** | UI | Movement event matching `_warpTarget` (±1px) does not accumulate delta |
| **PresenceProbe_NoTransientGrab** | Integration/UI | Isolated 1-2 port reads during boot do not engage grab (sustained polling required) |
| **ClickToFocus_Consumed** | UI | Click that gains window focus is not injected into emulation |
| **PreGrabPos_Restored** | UI | Cursor returns to pre-grab position on release |

## 8. Implementation order

1. **Mouse class polling tracker** — pure core logic, no UI
2. **Port decoder instrumentation** — call `recordPoll()` at read time
3. **Frame-loop timeout check** — detect polling silence
4. **MessageCenter notifications** — wire core → UI signalling
5. **CursorGrabController** — state machine in DeviceScreen
6. **Toolbar action** — icon + toggle + persistence
7. **Warp-event handling** — re-centre loop with discard
8. **Test suite** — per §7

## 9. Open questions

- **Q-1:** Should the timeout be configurable? 1 second is reasonable for presence
  detection loops, but some software may poll less frequently. Proposed: fixed at 1s
  initially, configurable via `Mouse=KEMPSTON:timeout=2` syntax if needed later.

- **Q-2:** Should the toolbar icon appear only when mouse emulation is enabled? Proposed
  no — always visible so users can pre-emptively disable before loading software.

- **Q-3: RESOLVED — No.** If mouse emulation is disabled in settings, or the `Mouse`
  peripheral reports absent (not fitted), grab must **never** engage. Rationale: grabbing
  the host cursor for a device that does not exist or do nothing is pointless and
  frustrating. The `_mouseInputEnabled` master switch already handles this — when the
  toolbar icon is disabled, no grab occurs. Similarly, if `CONFIG::mouse == 0` (no mouse
  emulation), the grab system should not even initialize.
