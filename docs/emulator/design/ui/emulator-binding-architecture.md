# UI Events Wiring Architecture

**Status:** Design Proposal  
**Date:** 2026-01-11

---

## Design Goals

1. **Single source of truth** - One state owner, widgets are consumers
2. **Ready-state gating** - No operations on uninitialized emulators
3. **No propagation loops** - Events flow one direction only
4. **Minimal overhead** - Lazy refresh, no redundant updates
5. **Thread-safe** - All emulator access marshalled to main thread

---

## Architecture Overview

```mermaid
graph TB
    subgraph Core ["Core Layer (Background Threads)"]
        EM[EmulatorManager]
        E1[Emulator Instance]
        MC[MessageCenter]
        E1 -->|NC_STATE_CHANGE| MC
    end
    
    subgraph App ["Qt Application (Main Thread)"]
        EB[EmulatorBinding]
        
        subgraph MainWin ["MainWindow"]
            MW_TB[Toolbar]
            MW_Menu[Menu Manager]
            DS[DeviceScreen Widget]
            MW_Status[Status Bar]
        end
        
        subgraph DebugWin ["DebuggerWindow"]
            DW_TB[Debug Toolbar]
            
            subgraph DebugWidgets ["Debug Widgets"]
                REG[RegistersWidget]
                DASM[DisassemblerWidget]
                STACK[StackWidget]
                MEM[MemoryPagesWidget]
                HEX[HexView]
            end
        end
        
        subgraph VisWindows ["Visualization Windows (opened from Debugger)"]
            VIZ1[MemoryMapWindow]
            VIZ2[WaveformWindow]
            VIZ3[SpriteViewerWindow]
        end
        
        LOG[LoggerWindow]
    end
    
    MC -->|callback| EB
    EB -->|stateChanged| MainWin
    EB -->|stateChanged| DebugWin
    EB -->|stateChanged| VisWindows
    EB -->|stateChanged| LOG
    
    EB -.->|isReady?| REG
    EB -.->|isReady?| DASM
    EB -.->|isReady?| STACK
    EB -.->|isReady?| MEM
    EB -.->|isReady?| HEX
    
    MainWin -->|lifecycle ops| EM
    DebugWin -->|debug ops| E1
```

### Component Hierarchy

| Scope | Component | Widgets/Children |
|-------|-----------|------------------|
| **App** | EmulatorBinding | Owned by MainWindow, shared to children |
| **MainWindow** | Primary UI | DeviceScreen, Toolbar, Menu, StatusBar |
| **DebuggerWindow** | Debug UI | Registers, Disassembler, Stack, Memory, HexView |
| **DebuggerWindow** | Opens → | MemoryMapWindow, WaveformWindow, SpriteViewer |
| **LoggerWindow** | Logging | LogListView, FilterControls |

---

## Window Communication Architecture

### Ownership and Responsibilities

```mermaid
graph LR
    subgraph Owner ["MainWindow (Owner)"]
        EB[EmulatorBinding]
        LC[Lifecycle Control]
    end
    
    subgraph Consumer ["DebuggerWindow (Consumer)"]
        DW[State Display]
        DA[Debug Actions]
    end
    
    subgraph Core
        EM[EmulatorManager]
        E[Emulator]
    end
    
    Owner -->|setBinding| Consumer
    EB -->|stateChanged| DW
    DA -->|pause/step/continue| EM
    LC -->|start/stop| EM
```

### Responsibilities

| Component | Role | Does | Does NOT |
|-----------|------|------|----------|
| **MainWindow** | Owner | Creates/destroys EmulatorBinding, owns lifecycle | Execute debug commands |
| **DebuggerWindow** | Consumer | Displays state, executes debug commands | Create/destroy binding |
| **EmulatorBinding** | Mediator | Caches state, emits signals | Execute any commands |

### Communication Flow

| Direction | Mechanism | Example |
|-----------|-----------|---------|
| MainWindow → DebuggerWindow | `setBinding(m_binding)` | Passes binding at startup |
| Binding → DebuggerWindow | `stateChanged` signal | State updated → refresh display |
| DebuggerWindow → Core | Direct `EmulatorManager` call | User clicks "Step In" |
| Core → Binding | MessageCenter callback | Emulator paused |

### Why DebuggerWindow Calls Core Directly

DebuggerWindow calls `EmulatorManager` directly for debug actions (not through MainWindow) because:
1. **No round-trip needed** - Debug actions are commands, not owned by MainWindow
2. **Response comes via binding** - After step, emulator sends state change → binding → widgets
3. **Cleaner separation** - MainWindow handles lifecycle, DebuggerWindow handles debugging

---

## EmulatorBinding Class

The central binding between Core and UI:

```cpp
class EmulatorBinding : public QObject {
    Q_OBJECT
public:
    explicit EmulatorBinding(QObject* parent = nullptr);
    ~EmulatorBinding();
    
    // === Binding Lifecycle ===
    void bind(Emulator* emulator);    // Connect to emulator events
    void unbind();                     // Disconnect, clear state
    
    // === State Accessors (Always Safe) ===
    bool isReady() const;              // Can widgets safely read state?
    bool isBound() const;              // Is an emulator bound?
    Emulator* emulator() const;        // Nullptr if not ready
    
    // === Cached State (Thread-safe reads) ===
    EmulatorStateEnum state() const;
    uint16_t pc() const;
    const Z80State* z80State() const;  // Nullptr if not ready
    
    // === Operations (Marshalled to main thread) ===
    void requestRefresh();              // Force state re-read
    
signals:
    void bound();                       // Emulator bound (not yet ready)
    void unbound();                     // Emulator unbound
    void stateChanged(EmulatorStateEnum state);  // State transition
    void ready();                       // Emulator is now inspectable
    void notReady();                    // Emulator no longer inspectable
    
private slots:
    void onMessageCenterEvent(int id, Message* msg);
    void doRefresh();                   // Actual refresh on main thread
    
private:
    Emulator* m_emulator = nullptr;
    EmulatorStateEnum m_state = StateUnknown;
    bool m_isReady = false;
    Z80State m_cachedZ80State{};
    uint16_t m_cachedPC = 0;
};
```

---

## Ready-State Gating

Widgets can only access emulator data when `isReady() == true`:

| State | isBound | isReady | Widgets Can |
|-------|---------|---------|-------------|
| No emulator | ❌ | ❌ | Display empty state |
| Initializing | ✅ | ❌ | Show "Loading..." |
| Running | ✅ | ❌ | Show "Running..." (stale data) |
| Paused | ✅ | ✅ | Read live state |
| Stopped | ✅ | ❌ | Display last snapshot |

```cpp
bool EmulatorBinding::isReady() const {
    return m_isReady && m_emulator && 
           (m_state == StatePaused || m_state == StateStopped);
}
```

---

## Event Flow

### Emulator Creation → Widget Ready

```mermaid
sequenceDiagram
    participant MW as MainWindow
    participant EM as EmulatorManager
    participant E as Emulator
    participant MC as MessageCenter
    participant EB as EmulatorBinding
    participant DW as DebuggerWindow

    MW->>EM: CreateEmulator()
    EM->>E: new Emulator()
    MW->>EB: bind(emulator)
    EB-->>DW: bound() [not ready]
    
    MW->>EM: StartEmulatorAsync(id)
    E->>MC: NC_STATE_CHANGE(StateRun)
    MC->>EB: callback
    EB-->>DW: stateChanged(StateRun) [not ready]
    
    Note over E: User pauses
    E->>MC: NC_STATE_CHANGE(StatePaused)
    MC->>EB: callback
    EB->>EB: refresh cached state
    EB-->>DW: stateChanged(StatePaused)
    EB-->>DW: ready()
    DW->>EB: z80State(), pc()
```

### Running → Pause

```mermaid
sequenceDiagram
    participant User
    participant MW as MainWindow
    participant EM as EmulatorManager
    participant E as Emulator
    participant MC as MessageCenter
    participant EB as EmulatorBinding
    participant W as Widgets

    Note over E: Emulator is running, widgets show stale/placeholder
    User->>MW: Click Pause
    MW->>EM: PauseEmulator(id)
    EM->>E: Pause()
    E->>MC: NC_STATE_CHANGE(StatePaused)
    MC->>EB: callback (background thread)
    EB->>EB: marshal to main thread
    EB->>EB: doRefresh() - cache Z80, PC, Memory
    EB-->>W: stateChanged(StatePaused)
    EB-->>W: ready()
    W->>EB: isReady() ✓
    W->>EB: z80State(), pc()
    W->>W: updateDisplay()
```

### Pause → Running (Resume)

```mermaid
sequenceDiagram
    participant User
    participant MW as MainWindow
    participant EM as EmulatorManager
    participant E as Emulator
    participant MC as MessageCenter
    participant EB as EmulatorBinding
    participant W as Widgets

    Note over W: Widgets showing live state (isReady=true)
    User->>MW: Click Continue
    MW->>EM: ResumeEmulator(id)
    EM->>E: Resume()
    E->>MC: NC_STATE_CHANGE(StateResumed)
    MC->>EB: callback
    EB->>EB: m_isReady = false
    EB-->>W: notReady()
    EB-->>W: stateChanged(StateResumed)
    W->>EB: isReady() ✗
    W->>W: showPlaceholder("Running...")
    Note over W: Widgets frozen, showing last snapshot
```

### Switch Active Emulator Instance

```mermaid
sequenceDiagram
    participant User
    participant MW as MainWindow
    participant EB as EmulatorBinding
    participant DW as DebuggerWindow
    participant W as Widgets
    participant E1 as Emulator1 (Paused)
    participant E2 as Emulator2 (Running)

    Note over W: Currently bound to Emulator1, isReady=true
    User->>MW: Select Emulator2 from list
    MW->>EB: unbind()
    EB-->>DW: unbound()
    DW->>W: setBinding(nullptr)
    W->>W: clearDisplay()
    
    MW->>EB: bind(emulator2)
    EB->>EB: check state → Running
    EB->>EB: m_isReady = false
    EB-->>DW: bound()
    EB-->>DW: stateChanged(StateRun)
    EB-->>DW: notReady()
    DW->>W: setBinding(binding)
    W->>EB: isReady() ✗
    W->>W: showPlaceholder("Running...")
    
    Note over E2: User pauses Emulator2
    E2->>EB: stateChanged(StatePaused)
    EB->>EB: doRefresh()
    EB-->>W: ready()
    W->>W: updateDisplay()
```

### Open Visualization Window (Hierarchical Dispatch)

> **Design Principle:** Child windows do NOT subscribe to `EmulatorBinding` directly.  
> The parent window (DebuggerWindow) acts as **controller/router** and dispatches events to its children.

```mermaid
sequenceDiagram
    participant User
    participant DW as DebuggerWindow (Controller)
    participant EB as EmulatorBinding
    participant VW as VisualizationWindow (Child)

    User->>DW: Open Memory Map
    DW->>VW: new VisualizationWindow(this)
    DW->>DW: m_childWindows.append(VW)
    
    alt Emulator Paused (isReady)
        DW->>VW: onParentStateChanged(StatePaused, z80State)
        VW->>VW: updateDisplay() immediately
    else Emulator Running
        DW->>VW: onParentStateChanged(StateRun, nullptr)
        VW->>VW: showPlaceholder("Pause to view")
    end
    
    Note over EB: Later: state changes
    EB-->>DW: stateChanged(StatePaused)
    DW->>DW: for each child in m_childWindows
    DW->>VW: onParentStateChanged(state, data)
    VW->>VW: updateDisplay()
    
    Note over VW: User closes visualization
    VW->>DW: closeEvent()
    DW->>DW: m_childWindows.remove(VW)
    Note over DW: No more dispatches to VW
```

### Child Window Lifecycle Management

```cpp
class DebuggerWindow {
    QList<QWidget*> m_childWindows;  // Active visualization windows
    
    void openVisualization(VisualizationType type) {
        auto* window = new VisualizationWindow(type, this);
        m_childWindows.append(window);
        
        // Connect close signal to cleanup
        connect(window, &QWidget::destroyed, this, [this, window]() {
            m_childWindows.removeOne(window);
        });
        
        // Initial state dispatch
        dispatchToChild(window);
        window->show();
    }
    
    void onStateChanged(EmulatorStateEnum state) {
        // Update own widgets
        updateWidgets();
        
        // Dispatch to all open child windows
        for (auto* child : m_childWindows) {
            dispatchToChild(child);
        }
    }
    
    void dispatchToChild(QWidget* child) {
        if (auto* viz = qobject_cast<VisualizationWindow*>(child)) {
            if (m_binding && m_binding->isReady()) {
                viz->onParentStateChanged(m_binding->state(), m_binding->z80State());
            } else {
                viz->onParentStateChanged(m_binding ? m_binding->state() : StateUnknown, nullptr);
            }
        }
    }
};
```

### Child Window Pattern

```cpp
class VisualizationWindow : public QWidget {
    // NO direct EmulatorBinding reference!
    // All data comes from parent via dispatch
    
public:
    void onParentStateChanged(EmulatorStateEnum state, const Z80State* data) {
        if (state == StatePaused && data) {
            updateDisplay(*data);
        } else {
            showPlaceholder(state == StateRun ? "Running..." : "No data");
        }
    }
};
```

### Widget State Handler Pattern

```cpp
void DebuggerWidget::onStateChanged(EmulatorStateEnum state) {
    if (!m_binding->isReady()) {
        showPlaceholder();  // "Emulator running..." or empty
        return;
    }
    
    // Safe to access cached state
    updateDisplay(m_binding->z80State());
}
```

---

## Integration Points

### MainWindow

```cpp
class MainWindow {
    EmulatorBinding* m_binding;
    
    void onEmulatorCreated(Emulator* emu) {
        m_binding->bind(emu);
        debuggerWindow->setBinding(m_binding);
        visualizationWindow->setBinding(m_binding);
    }
    
    void onEmulatorDestroyed() {
        m_binding->unbind();
    }
};
```

### DebuggerWindow

```cpp
class DebuggerWindow {
    EmulatorBinding* m_binding = nullptr;
    
    void setBinding(EmulatorBinding* binding) {
        if (m_binding) {
            disconnect(m_binding, nullptr, this, nullptr);
        }
        m_binding = binding;
        if (m_binding) {
            connect(m_binding, &EmulatorBinding::stateChanged, 
                    this, &DebuggerWindow::onStateChanged);
            connect(m_binding, &EmulatorBinding::ready, 
                    this, &DebuggerWindow::onReady);
        }
        // Propagate to child widgets
        for (auto* widget : m_widgets) {
            widget->setBinding(binding);
        }
    }
};
```

### Individual Widget

```cpp
class RegistersWidget {
    EmulatorBinding* m_binding = nullptr;
    
    void setBinding(EmulatorBinding* binding) {
        m_binding = binding;
        if (m_binding) {
            connect(m_binding, &EmulatorBinding::stateChanged,
                    this, &RegistersWidget::onStateChanged);
        }
    }
    
    void onStateChanged(EmulatorStateEnum) {
        if (m_binding && m_binding->isReady()) {
            const Z80State* state = m_binding->z80State();
            if (state) updateDisplay(*state);
        } else {
            clearDisplay();
        }
    }
};
```

---

## Thread Safety

### MessageCenter → Main Thread Marshalling

```cpp
void EmulatorBinding::onMessageCenterEvent(int id, Message* msg) {
    // Called from MessageCenter thread - marshal to main thread
    QMetaObject::invokeMethod(this, [this, state = extractState(msg)]() {
        m_state = state;
        
        if (state == StatePaused || state == StateStopped) {
            doRefresh();  // Read emulator state
            m_isReady = true;
            emit ready();
        } else {
            m_isReady = false;
            emit notReady();
        }
        
        emit stateChanged(state);
    }, Qt::QueuedConnection);
}
```

---

## Benefits

| Aspect | Before | After |
|--------|--------|-------|
| **Emulator refs** | Each widget stores pointer | Single binding, passed down |
| **Null checks** | Every widget, every method | One check in `isReady()` |
| **State sync** | Manual refresh calls | Automatic via signals |
| **Thread safety** | Per-widget marshalling | Centralized in binding |
| **Crash safety** | Guard in every accessor | Single gated access point |

---

## Implementation Checklist

1. [ ] Create `EmulatorBinding` class in `unreal-qt/src/emulator/`
2. [ ] Add MessageCenter subscription handling
3. [ ] Implement main-thread marshalling
4. [ ] Wire into `MainWindow`
5. [ ] Update `DebuggerWindow` to use binding
6. [ ] Update individual widgets
7. [ ] Remove `m_emulator` from widgets
8. [ ] Add unit tests for state transitions

---

## Related Documents

- [emulator-lifecycle-management.md](./emulator-lifecycle-management.md) - Emulator ownership model
- [pause-stop-race-condition-fix.md](./pause-stop-race-condition-fix.md) - Thread safety patterns
- [Debugger events analysis](../../inprogress/2026-01-11-debugger-events/) - Root cause of crash

