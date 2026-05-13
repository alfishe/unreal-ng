# Hotfix: Deferred Disassembly Initialization

**Branch:** `disasm-table`  
**Date:** 2026-01-11

## Summary

Deferred all disassembly operations until the emulator sends a state change event through MessageCenter, ensuring the emulator is fully initialized before any memory/register access.

## Changes Made

### 1. `DebuggerWindow::setEmulator()` - Removed Eager Initialization

**Before:**
```cpp
void DebuggerWindow::setEmulator(Emulator* emulator) {
    _emulator = emulator;
    if (_emulator) {
        loadState();
        updateToolbarActions(...);
        updateState();  // TRIGGERS DISASSEMBLY - CRASH!
        ui->disassemblerWidget->setEmulator(emulator);  // TRIGGERS DISASSEMBLY - CRASH!
    }
}
```

**After:**
```cpp
void DebuggerWindow::setEmulator(Emulator* emulator) {
    _emulator = emulator;
    if (_emulator) {
        loadState();
        updateToolbarActions(...);
        // NO updateState() - wait for state change event
        ui->disassemblerWidget->setEmulator(nullptr);  // Clear, don't trigger
    }
}
```

### 2. `DebuggerWindow::updateState()` - Lazy Widget Setup

Added emulator binding on first valid state change:

```cpp
void DebuggerWindow::updateState() {
    // ...
    if (ui->disassemblerWidget && ui->disassemblerWidget->emulator() != _emulator) {
        ui->disassemblerWidget->setEmulator(_emulator);  // Safe - after MessageCenter event
    }
    ui->disassemblerWidget->setDisassemblerAddress(currentPC);
}
```

### 3. `DisassemblerWidget` - Added Getter

```cpp
// disassemblerwidget.h
Emulator* emulator() const { return m_emulator; }
```

### 4. `DisassemblerTableModel` - Defensive Guards

Added null checks in `disassembleAt()`, `disassembleForward()`, `disassembleBackward()`:

```cpp
if (!m_emulator || !m_emulator->GetContext() || 
    !m_emulator->GetContext()->pCore || 
    !m_emulator->GetContext()->pDebugManager ||
    !m_emulator->GetContext()->pMemory) {
    qWarning() << "Emulator not ready for disassembly - deferring";
    return;
}
```

## Event Flow (After Fix)

```
1. MainWindow::handleStartButton()
   → DebuggerWindow::setEmulator()  // Just stores reference, no disassembly
   
2. Emulator initializes fully...
   → Sends NC_EMULATOR_STATE_CHANGE via MessageCenter
   
3. MainWindow receives event
   → DebuggerWindow::notifyEmulatorStateChanged()
     → updateState()
       → Sets up DisassemblerWidget (lazy)
       → Triggers disassembly (SAFE - emulator ready)
```

## Testing

1. Application starts without crash
2. Pause emulator → disassembly populates correctly
3. Step → disassembly follows PC
