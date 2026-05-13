# Shutdown Race Condition Fix

## Problem Description

When closing the application while the emulator is running, the application crashes with a segmentation fault:

```
Exception Type:        EXC_BAD_ACCESS (SIGSEGV)
Exception Codes:       KERN_INVALID_ADDRESS at 0x000000000000046e
```

### Crash Stack Trace

**Thread 0 (Main):**
```
EmulatorManager::~EmulatorManager()
  → EmulatorManager::ShutdownAllEmulators()
    → Emulator::Release()
      → Core::Release()
        → Memory::~Memory()
          → MemoryAccessTracker::~MemoryAccessTracker()  ← Destroying memory
```

**Thread 7 (Emulator):**
```
MainLoop::Run(bool volatile&)  ← Still trying to access memory being destroyed!
  → CRASH: KERN_INVALID_ADDRESS
```

## Root Cause

The crash occurs because of a shutdown race condition:

1. **User closes window** → `MainWindow::closeEvent()` called
2. `closeEvent()` calls `_emulator->Stop()` (good!)
3. `closeEvent()` sets `_emulator = nullptr` (releases reference)
4. **BUT** - Emulator is still registered in `EmulatorManager`
5. Application exits → `EmulatorManager` destructor fires
6. `EmulatorManager::~EmulatorManager()` calls `ShutdownAllEmulators()`
7. Tries to destroy the emulator **again**
8. Emulator thread is still running or not fully stopped
9. Memory being destroyed while emulator thread tries to access it → **CRASH**

## The Bug

**File:** `unreal-qt/src/mainwindow.cpp`

### Before (Buggy Code):

```cpp
void MainWindow::closeEvent(QCloseEvent* event)
{
    // ... other cleanup ...
    
    // Stop emulator
    if (_emulator)
    {
        _emulator->Stop();  // Stops the thread
    }
    
    // ... more cleanup ...
    
    // Shutdown emulator
    if (_emulator)
    {
        _emulator = nullptr;  // ❌ BUG: EmulatorManager still has reference!
    }
}
```

**Problems:**
- Emulator is stopped but still registered in EmulatorManager
- When app exits, EmulatorManager tries to shut it down again
- Race condition between destructor cleanup and emulator thread

### After (Fixed Code):

```cpp
void MainWindow::closeEvent(QCloseEvent* event)
{
    // ... other cleanup ...
    
    // Stop emulator
    if (_emulator)
    {
        _emulator->Stop();  // Stops the thread and waits
    }
    
    // ... more cleanup ...
    
    // Remove emulator from manager before destroying it
    if (_emulator)
    {
        std::string emulatorId = _emulator->GetId();
        _emulatorManager->RemoveEmulator(emulatorId);  // ✅ FIX: Properly remove from manager
        _emulator = nullptr;
    }
}
```

## Why This Fixes It

### EmulatorManager::RemoveEmulator() Does:

1. **Checks if emulator is running** → calls `Stop()` if needed (safe, already stopped)
2. **Calls `Release()`** → releases all emulator resources properly
3. **Removes from manager's map** → no more references in EmulatorManager
4. **Destructs cleanly** → no double-destroy when app exits

### Shutdown Sequence Now:

```
User Closes Window
  ↓
MainWindow::closeEvent()
  ↓
_emulator->Stop()  ← Thread stops and joins
  ↓
_emulatorManager->RemoveEmulator()  ← Properly unregisters
  ├─ Stop() (no-op, already stopped)
  ├─ Release() (clean resource cleanup)
  └─ Remove from map
  ↓
_emulator = nullptr  ← Last reference released
  ↓
Application exits
  ↓
EmulatorManager::~EmulatorManager()
  ↓
ShutdownAllEmulators()  ← Map is empty, nothing to do! ✅
```

## Related Fixes

This is related to the previous **Pause/Resume crash fix** but different:

| Issue | Cause | When | Fix |
|-------|-------|------|-----|
| **Pause/Resume Crash** | Destroying emulator while paused thread running | During operation | Check paused state before destroy |
| **Shutdown Crash** | Double-destroy during app exit | Application exit | Remove from manager before exit |

## Testing Checklist

- [x] Start emulator → close window → app exits cleanly ✅
- [x] Start emulator → pause → close window → app exits cleanly ✅
- [x] Start emulator → run for a while → close window → no crash ✅
- [x] Close window without starting emulator → app exits cleanly ✅

## Additional Safety

### EmulatorManager::RemoveEmulator() is Safe to Call:

```cpp
bool EmulatorManager::RemoveEmulator(const std::string& emulatorId)
{
    // Stop if running (idempotent - safe to call multiple times)
    if (it->second->IsRunning())
    {
        it->second->Stop();  // If already stopped, returns immediately
    }
    
    // Release resources (proper cleanup)
    it->second->Release();
    
    // Remove from map
    _emulators.erase(it);
}
```

### Emulator::Stop() is Idempotent:

```cpp
void Emulator::Stop()
{
    if (!_isRunning)
        return;  // Already stopped, safe early return
    
    _stopRequested = true;
    
    // Wait for thread to finish
    if (_asyncThread && _asyncThread->joinable())
    {
        _asyncThread->join();  // Blocks until thread exits
    }
}
```

## Lessons Learned

### Resource Ownership Best Practices:

1. **Clear Ownership**: EmulatorManager owns emulators, MainWindow just references them
2. **Proper Cleanup**: Always unregister from manager before releasing
3. **Idempotent Operations**: Make Stop() and cleanup safe to call multiple times
4. **Thread Safety**: Always join threads before destroying resources they access

### Window Cleanup Pattern:

```cpp
void MainWindow::closeEvent(QCloseEvent* event)
{
    // 1. Stop active operations
    if (_emulator)
    {
        _emulator->Stop();  // Wait for thread
    }
    
    // 2. Unregister from managers
    if (_emulator)
    {
        _emulatorManager->RemoveEmulator(_emulator->GetId());
    }
    
    // 3. Release references
    _emulator = nullptr;
    
    // 4. Cleanup UI resources
    // ...
}
```

## Related Files

- `unreal-qt/src/mainwindow.cpp` (line ~266) - closeEvent fix
- `core/src/emulator/emulator.cpp` (line 624) - Stop() implementation  
- `core/src/emulator/emulatormanager.cpp` (line 97) - RemoveEmulator()
- `core/src/emulator/mainloop.cpp` - MainLoop::Run()

## References

- Previous fix: `PAUSE_RESTART_CRASH_FIX.md` (pause/resume during operation)
- Architecture: `MULTIPLE_EMULATOR_ARCHITECTURE.md` (ownership model)

## Date
January 4, 2026

