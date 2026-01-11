# Emulator Lifecycle Management Architecture

## Design Principle

**The UI must ALWAYS use `EmulatorManager` for lifecycle operations, NEVER call methods directly on `Emulator` instances.**

## Why This Matters

### Single vs Multiple Instances

- **`Emulator`**: Represents a **single** emulator instance
- **`EmulatorManager`**: Manages **multiple** emulator instances

### Ownership and Lifecycle

```
EmulatorManager (singleton)
    │
    ├── owns → Emulator Instance 1 (std::shared_ptr)
    ├── owns → Emulator Instance 2 (std::shared_ptr)
    └── owns → Emulator Instance N (std::shared_ptr)

MainWindow
    │
    └── references → Emulator Instance (std::shared_ptr)
         (read-only reference, NOT owner!)
```

**Key Point:** `MainWindow` holds a **reference** to an emulator, but `EmulatorManager` **owns** it. Therefore, all lifecycle operations must go through the manager.

## The Problem (Before Fix)

### ❌ **Bad Pattern - Direct Emulator Calls:**

```cpp
// In MainWindow
if (_emulator && _emulator->IsPaused())
{
    _emulator->Resume();  // ❌ WRONG: Bypasses manager!
}

_emulator->StartAsync();  // ❌ WRONG: Direct call!
_emulator->Stop();        // ❌ WRONG: Manager doesn't know!
_emulator->Pause();       // ❌ WRONG: Breaks ownership model!
```

### Why This Is Bad:

1. **Breaks Ownership Model**: UI shouldn't directly manipulate instances it doesn't own
2. **No Multi-Instance Support**: Can't work with multiple emulators
3. **No Centralized State**: Manager loses track of emulator states
4. **Thread Safety Issues**: Manager's mutex protection is bypassed
5. **Harder to Debug**: Operations happen outside manager's logging
6. **Architectural Violation**: Tight coupling between UI and Emulator

## The Solution (After Fix)

### ✅ **Good Pattern - Through EmulatorManager:**

```cpp
// In MainWindow
if (_emulator && _emulator->IsPaused())
{
    std::string emulatorId = _emulator->GetId();
    _emulatorManager->ResumeEmulator(emulatorId);  // ✅ CORRECT: Through manager!
}

std::string emulatorId = _emulator->GetId();
_emulatorManager->StartEmulatorAsync(emulatorId);  // ✅ CORRECT: Centralized!
_emulatorManager->StopEmulator(emulatorId);        // ✅ CORRECT: Manager tracks state!
_emulatorManager->PauseEmulator(emulatorId);       // ✅ CORRECT: Proper ownership!
```

### Why This Is Good:

1. **Respects Ownership**: Manager controls lifecycle of instances it owns
2. **Multi-Instance Ready**: Works with multiple emulators (future-proof)
3. **Centralized State**: Manager tracks all operations
4. **Thread-Safe**: Manager's mutex protects all operations
5. **Better Logging**: Manager logs all lifecycle events
6. **Clean Architecture**: Loose coupling, proper separation of concerns

## EmulatorManager Lifecycle API

### Methods Added (All Thread-Safe)

```cpp
// Start operations
bool StartEmulator(const std::string& emulatorId);
bool StartEmulatorAsync(const std::string& emulatorId);

// Stop operations
bool StopEmulator(const std::string& emulatorId);

// Pause/Resume operations
bool PauseEmulator(const std::string& emulatorId);
bool ResumeEmulator(const std::string& emulatorId);

// Reset operation
bool ResetEmulator(const std::string& emulatorId);

// Remove operation (already existed)
bool RemoveEmulator(const std::string& emulatorId);
```

### Implementation Pattern

Each method follows this pattern:

```cpp
bool EmulatorManager::PauseEmulator(const std::string& emulatorId)
{
    std::lock_guard<std::mutex> lock(_emulatorsMutex);  // 1. Thread-safe

    auto it = _emulators.find(emulatorId);              // 2. Find emulator
    if (it != _emulators.end())
    {
        if (it->second->IsRunning() && !it->second->IsPaused())  // 3. Validate state
        {
            it->second->Pause();                        // 4. Call emulator method
            LOGINFO("EmulatorManager::PauseEmulator - Paused emulator with ID '%s'", 
                    emulatorId.c_str());                // 5. Log operation
            return true;                                // 6. Return success
        }
        else
        {
            LOGDEBUG("EmulatorManager::PauseEmulator - Emulator with ID '%s' not running", 
                     emulatorId.c_str());
            return false;                               // State check failed
        }
    }

    LOGDEBUG("EmulatorManager::PauseEmulator - No emulator found with ID '%s'", 
             emulatorId.c_str());
    return false;                                       // Emulator not found
}
```

**Benefits:**
- ✅ Thread-safe (mutex protection)
- ✅ State validation before operation
- ✅ Comprehensive logging
- ✅ Clear error handling
- ✅ Returns success/failure status

## UI Usage Pattern

### Typical UI Code (MainWindow)

```cpp
void MainWindow::handlePauseEmulator()
{
    // 1. Check if we have a valid emulator reference
    if (_emulator && _emulator->IsRunning() && !_emulator->IsPaused())
    {
        // 2. Get emulator ID
        std::string emulatorId = _emulator->GetId();
        
        // 3. Call EmulatorManager method
        _emulatorManager->PauseEmulator(emulatorId);
        
        // 4. Update UI state
        updateMenuStates();
        
        qDebug() << "Emulator paused";
    }
}
```

### Window Close Event

```cpp
void MainWindow::closeEvent(QCloseEvent* event)
{
    // ... other cleanup ...

    // ✅ CORRECT: Just call RemoveEmulator - it's thread-safe!
    if (_emulator)
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->RemoveEmulator(emulatorId);  // Handles Stop + Release
        _emulator = nullptr;
    }
}
```

**No need to:**
- ❌ Call `Stop()` manually
- ❌ Call `Release()` manually
- ❌ Worry about thread synchronization

`RemoveEmulator()` handles all of this correctly.

## Architecture Diagram

```
┌─────────────────────────────────────────────────────┐
│                    MainWindow (UI)                  │
│  - Displays emulator state                          │
│  - Handles user input                               │
│  - Holds reference to active emulator               │
└────────────────┬────────────────────────────────────┘
                 │
                 │ All lifecycle ops go through manager ↓
                 │
┌────────────────▼────────────────────────────────────┐
│              EmulatorManager (Manager)              │
│  - Singleton instance                               │
│  - Owns all emulator instances                      │
│  - Thread-safe operations (mutex protected)         │
│  - Centralized logging                              │
│  - State tracking                                   │
└────────────────┬────────────────────────────────────┘
                 │
                 │ Manages lifecycle ↓
                 │
┌────────────────▼────────────────────────────────────┐
│  Emulator Instances (std::shared_ptr)               │
│  ┌─────────────────┐  ┌─────────────────┐          │
│  │  Emulator #1    │  │  Emulator #2    │  ...     │
│  │  (UUID: xxx)    │  │  (UUID: yyy)    │          │
│  │  State: Running │  │  State: Paused  │          │
│  └─────────────────┘  └─────────────────┘          │
└─────────────────────────────────────────────────────┘
```

## Thread Safety

### EmulatorManager Mutex Protection

```cpp
class EmulatorManager
{
private:
    std::map<std::string, std::shared_ptr<Emulator>> _emulators;
    std::mutex _emulatorsMutex;  // ← Protects all operations

public:
    bool PauseEmulator(const std::string& emulatorId)
    {
        std::lock_guard<std::mutex> lock(_emulatorsMutex);  // ← Thread-safe!
        // ... find and pause ...
    }
};
```

**All lifecycle methods use the mutex** → Thread-safe from multiple UI threads or external calls.

## Future-Proofing: Multiple Emulator Support

This architecture supports multiple emulators seamlessly:

```cpp
// Create multiple emulators
auto emu1 = manager->CreateEmulator("ZX48K");
auto emu2 = manager->CreateEmulator("ZX128K");

// Control them independently
manager->StartEmulatorAsync(emu1->GetId());
manager->StartEmulatorAsync(emu2->GetId());

// Pause one
manager->PauseEmulator(emu1->GetId());

// Both continue running independently
// emu1 is paused, emu2 is still running

// Clean shutdown
manager->RemoveEmulator(emu1->GetId());
manager->RemoveEmulator(emu2->GetId());
```

## Code Changes Summary

### Files Modified

1. **`core/src/emulator/emulatormanager.h`**
   - Added 6 new lifecycle methods: `StartEmulator`, `StartEmulatorAsync`, `StopEmulator`, `PauseEmulator`, `ResumeEmulator`, `ResetEmulator`

2. **`core/src/emulator/emulatormanager.cpp`**
   - Implemented all 6 lifecycle methods with thread-safe, validated, logged operations

3. **`unreal-qt/src/mainwindow.cpp`**
   - Replaced 6 direct `_emulator->` calls with `_emulatorManager->` calls:
     - `handleStartButton()`: Resume → `ResumeEmulator()`
     - `handleStartButton()`: StartAsync → `StartEmulatorAsync()`
     - `handleStartButton()`: Stop → `StopEmulator()`
     - `handlePauseEmulator()`: Pause → `PauseEmulator()`
     - `handleResumeEmulator()`: Resume → `ResumeEmulator()`
     - `handleStopEmulator()`: Stop → `StopEmulator()`

### Lines Changed

- **Added:** ~150 lines (EmulatorManager lifecycle methods)
- **Modified:** ~20 lines (MainWindow UI code)
- **Net Effect:** Cleaner architecture, better maintainability

## Checklist for UI Developers

When working with emulators in the UI:

- ✅ **DO** call `EmulatorManager::CreateEmulator()` to create
- ✅ **DO** call `EmulatorManager::*Emulator(id)` for lifecycle ops
- ✅ **DO** call `EmulatorManager::RemoveEmulator(id)` to destroy
- ✅ **DO** use `_emulator->GetId()` to get the ID for manager calls
- ✅ **DO** use `_emulator->IsRunning()` / `IsPaused()` for UI state checks

- ❌ **DON'T** call `_emulator->Start()` directly
- ❌ **DON'T** call `_emulator->Stop()` directly
- ❌ **DON'T** call `_emulator->Pause()` / `Resume()` directly
- ❌ **DON'T** call `_emulator->Release()` directly
- ❌ **DON'T** delete emulator instances manually

## Testing Checklist

- [x] Start → works through manager ✅
- [x] Pause → works through manager ✅
- [x] Resume → works through manager ✅
- [x] Stop → works through manager ✅
- [x] Close window → `RemoveEmulator()` handles cleanup ✅
- [x] Pause → Close → No race condition ✅
- [x] All operations thread-safe ✅
- [x] Manager logs all operations ✅

## Related Documents

- `PAUSE_STOP_RACE_CONDITION_FIX.md` - Thread safety during pause/stop
- `SHUTDOWN_RACE_CONDITION_FIX.md` - App exit cleanup
- `MULTIPLE_EMULATOR_ARCHITECTURE.md` - Multi-instance design

## Date
January 4, 2026

## Summary

**Core Principle:** UI code should **never** directly call lifecycle methods on `Emulator` instances. All lifecycle operations must go through `EmulatorManager`.

**Why:** Proper ownership model, thread safety, multi-instance support, centralized state management, better logging, clean architecture.

**Result:** More maintainable, scalable, and robust emulator management system.

