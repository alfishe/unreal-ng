# Debugger Widget Startup Crash - Root Cause Analysis

**Discovery Branch:** `disasm-table`  
**Date:** 2026-01-11  
**Status:** Hotfix Applied

## Problem Description

Application crashes on startup with `SIGSEGV` when the debugger window attempts to disassemble code before the emulator is fully initialized.

### Stack Trace
```
MainWindow::handleStartButton()
  → DebuggerWindow::setEmulator()
    → DisassemblerWidget::setEmulator()
      → DisassemblerTableModel::setEmulator()
        → setVisibleRange() → loadDisassemblyRange() → disassembleForward()
          → disassembleAt()
            → m_emulator->GetContext()->pCore->GetZ80()  // CRASH: pCore or Z80 null
```

### Root Cause

`DebuggerWindow::setEmulator()` was immediately triggering:
1. `updateState()` - which reads Z80 registers and memory
2. `ui->disassemblerWidget->setEmulator()` - which triggers full disassembly

This happened **before** the emulator finished initialization - the context, core, and Z80 state were not yet available.

## Changes Required in Master

| File | Change Type | Description |
|------|-------------|-------------|
| `debuggerwindow.cpp` | Modify | Remove `updateState()` call from `setEmulator()`. Defer to state change events. |
| `debuggerwindow.cpp` | Modify | Add lazy emulator setup in `updateState()` for disassembler widget |
| `disassemblerwidget.h` | Add | `emulator()` getter method |
| `disassemblertablemodel.cpp` | Add | Null guards in `disassembleAt()`, `disassembleForward()`, `disassembleBackward()` |

### Diff Summary (Branch vs Master)
```
unreal-qt/src/debugger/debuggerwindow.cpp         |  113 ++++---
unreal-qt/src/debugger/debuggerwindow.h           |    2 +
unreal-qt/src/debugger/disassemblertablemodel.cpp |  985 ++++++
unreal-qt/src/debugger/disassemblertablemodel.h   |   66 +
unreal-qt/src/debugger/disassemblerwidget.cpp     | 1203 ++++----
unreal-qt/src/debugger/disassemblerwidget.h       |  213 ++--
6 files changed, 1532 insertions(+), 1050 deletions(-)
```

## Architecture Issue

The original design had **3 layers of emulator references** with redundant null checks:

```
DebuggerWindow._emulator
  └── DisassemblerWidget.m_emulator  
        └── DisassemblerTableModel.m_emulator
```

Each layer stored its own pointer and performed its own validity checks, creating race conditions when the emulator was being initialized.

## References

- [Proposal: Centralized DebuggerState Architecture](./proposal-debugger-state.md)
- [Hotfix Details](./hotfix-deferred-disassembly.md)
