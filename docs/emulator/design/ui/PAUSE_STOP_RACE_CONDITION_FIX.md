# Pause → Stop → Release Race Condition Fix

## Problem Description

Crash when **pausing the emulator, then closing the application** (or calling Stop + Release in sequence):

```
Exception Type:        EXC_BAD_ACCESS (SIGSEGV)
Exception Codes:       KERN_INVALID_ADDRESS at 0x000000000000046e
```

### Crash Scenario

1. **User action**: Start → Pause → Close Window
2. **Result**: Segmentation fault in `MainLoop::Run()` accessing destroyed memory

### Stack Trace

**Thread 0 (Main):**
```
MainWindow::closeEvent()
  → EmulatorManager::RemoveEmulator()
    → Emulator::Stop()
    → Emulator::Release()
      → Memory::~Memory()  ← Destroying memory
```

**Thread 13 (Emulator - CRASHED):**
```
MainLoop::Run()
  → MainLoop::RunFrame()  ← Trying to access destroyed memory!
    → CRASH: KERN_INVALID_ADDRESS
```

## Root Cause Analysis

### The Race Condition

When the emulator is **paused**, the emulator thread is in a busy-wait loop:

```cpp
// In MainLoop::Run() - BUGGY CODE
while (_pauseRequested)
{
    sleep_ms(20);  // Thread is waiting here when paused
}

continue;  // Goes back to start of main loop
```

When `Stop()` is called while paused:

```cpp
// In Emulator::Stop()
_stopRequested = true;           // 1. Set stop flag

if (_isPaused)
{
    _mainloop->Resume();         // 2. Wake up thread (_pauseRequested = false)
    _isPaused = false;
}

if (_asyncThread->joinable())
{
    _asyncThread->join();        // 3. Wait for thread... BUT!
}
```

### The Race Window

**The Problem:** After `Resume()`, the thread exits the pause loop and does `continue`, which goes back to the **start** of the main while loop:

```cpp
while (!stopRequested)           // Thread hasn't reached here yet!
{
    // ... measure time ...
    
    RunFrame();                  // ← Thread executes THIS first!
                                 //   Accessing memory that's being destroyed!
    
    if (_pauseRequested) { ... }
    
    // ... audio sync ...
}
```

**Timeline of the race:**

```
Time  Thread 0 (Main)              Thread 13 (Emulator)
────────────────────────────────────────────────────────────
t0    Stop() called
t1    _stopRequested = true
t2    Resume() called
t3                                 _pauseRequested = false
t4                                 Exit pause loop
t5                                 continue; (back to top)
t6    join() returns (?!)         
t7    Release() called            
t8    Memory destroyed             
t9                                 RunFrame() ← ACCESSING DESTROYED MEMORY!
t10                                CRASH: SIGSEGV
```

**Why join() returns early:** The thread is still in `MainLoop::Run()`, but might be past the pause loop, so `join()` thinks it's about to exit. However, it still has to execute `RunFrame()` and other code before checking `stopRequested`.

## The Fix

### 1. Check Stop Flag WHILE Paused

**File:** `core/src/emulator/mainloop.cpp`

```cpp
/// region <Handle Pause>
if (_pauseRequested)
{
    MLOGINFO("Pause requested");

    while (_pauseRequested && !stopRequested)  // ✅ CHECK STOP WHILE PAUSED!
    {
      sleep_ms(20);
    }

    // Check if we were woken up to stop (not just resume)
    if (stopRequested)
    {
        MLOGINFO("Stop requested while paused");
        break;  // ✅ EXIT IMMEDIATELY, don't run another frame
    }

    continue;
}
/// endregion </Handle Pause>
```

**What this does:**
- **While paused**, the thread checks BOTH `_pauseRequested` AND `stopRequested`
- If `Stop()` is called, `stopRequested` becomes true
- Thread **immediately breaks out** of the pause loop AND the main loop
- **No more frames are executed** - thread goes straight to cleanup and exit

### 2. Make Release() Idempotent

**File:** `core/src/emulator/emulator.h`

```cpp
// Emulator state
volatile bool _isPaused = false;
volatile bool _isRunning = false;
volatile bool _isDebug = false;
volatile bool _isReleased = false;  // ✅ NEW: Track release state
```

**File:** `core/src/emulator/emulator.cpp`

```cpp
void Emulator::Release()
{
    // Lock mutex until exiting current scope
    std::lock_guard<std::mutex> lock(_mutexInitialization);

    // ✅ Guard against double-release (thread safety)
    if (_isReleased)
    {
        MLOGDEBUG("Emulator::Release - Already released, ignoring");
        return;
    }

    _isReleased = true;

    ReleaseNoGuard();
}
```

**What this does:**
- Prevents double-release if `Release()` is called multiple times
- Thread-safe: uses existing `_mutexInitialization` mutex
- Idempotent: safe to call multiple times

### 3. Clean UI Code

**File:** `unreal-qt/src/mainwindow.cpp`

```cpp
void MainWindow::closeEvent(QCloseEvent* event)
{
    // ... other cleanup ...

    // ✅ Just call RemoveEmulator - it handles everything!
    if (_emulator)
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->RemoveEmulator(emulatorId);  // Thread-safe
        _emulator = nullptr;
    }
}
```

**What this does:**
- UI code stays **simple and clean**
- No need to call `Stop()` manually
- All thread safety is handled in the emulator core
- `EmulatorManager::RemoveEmulator()` already has mutex protection

## Why This Works

### Correct Shutdown Sequence Now

```
User Closes Window (while paused)
  ↓
MainWindow::closeEvent()
  ↓
EmulatorManager::RemoveEmulator()
  ↓
Emulator::Stop()
  ├─ _stopRequested = true
  ├─ Resume() → _pauseRequested = false
  └─ join()
       ↓
       [EMULATOR THREAD]
       ├─ Exit pause loop (stopRequested = true)
       ├─ Check stopRequested → break immediately
       ├─ Exit MainLoop::Run()
       ├─ Thread function returns
       └─ join() completes ✅
  ↓
Emulator::Release()
  ├─ Check _isReleased (false, first call)
  ├─ Set _isReleased = true
  └─ ReleaseNoGuard() - thread is FULLY exited ✅
```

**No race condition because:**
1. Thread checks `stopRequested` **while paused**, not after
2. Thread **breaks immediately** when stop is requested
3. `join()` only returns when thread has **fully exited** `MainLoop::Run()`
4. `Release()` is called **after** `join()` returns
5. Memory is destroyed **after** thread has finished using it

## Testing Checklist

- [x] Start → Close → No crash ✅
- [x] Start → Pause → Close → No crash ✅
- [x] Start → Run for a while → Close → No crash ✅
- [x] Start → Pause → Resume → Close → No crash ✅
- [x] Multiple Start/Stop cycles → No crash ✅

## Key Design Principles

### 1. Thread Safety at Core Level

**All thread synchronization logic belongs in the emulator core, NOT in the UI.**

```cpp
// ✅ GOOD: UI is simple
_emulatorManager->RemoveEmulator(id);

// ❌ BAD: UI trying to manage threads
_emulator->Stop();
// ... complex synchronization ...
_emulator->Release();
```

### 2. Idempotent Operations

**Core operations like `Stop()` and `Release()` must be safe to call multiple times.**

```cpp
// These should all be safe:
emulator->Stop();
emulator->Stop();  // No-op, returns early
emulator->Release();
emulator->Release();  // No-op, already released
```

### 3. Check Stop Flag Everywhere

**Any loop or wait must check the stop flag, especially when paused.**

```cpp
// ✅ GOOD
while (condition && !stopRequested)
{
    // Can be interrupted
}

// ❌ BAD
while (condition)
{
    // Stuck forever if stop is requested
}
```

### 4. Exit Immediately on Stop

**When stop is requested, thread should exit ASAP without executing more frames.**

```cpp
if (stopRequested)
{
    break;  // ✅ Exit immediately
}

// ❌ Don't do more work
RunFrame();  // BAD: Might access destroyed memory
```

## Related Issues

| Issue | Description | Fix |
|-------|-------------|-----|
| **Shutdown Race** | App exit while running | RemoveEmulator before exit |
| **Pause/Resume Crash** | Destroying paused emulator | Check paused before destroy |
| **This Issue** | Pause → Stop → Release race | Check stop while paused |

## Performance Impact

**Negligible:**
- Added one boolean check in pause loop: `&& !stopRequested`
- Pause loop already runs at 20ms intervals (50Hz)
- Extra check adds ~1 nanosecond per iteration
- No impact on normal (non-paused) execution

## Backward Compatibility

**Fully compatible:**
- No API changes
- Existing code continues to work
- Only internal behavior changes (faster exit when paused)

## Date
January 4, 2026

## See Also

- `SHUTDOWN_RACE_CONDITION_FIX.md` - App exit crash fix
- `STATE_MANAGEMENT_FIX.md` - Resume menu item fix
- `MULTIPLE_EMULATOR_ARCHITECTURE.md` - Ownership model

