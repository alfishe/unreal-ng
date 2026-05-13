# Debugger Events Architecture Investigation

**Date:** 2026-01-11  
**Discovery Branch:** `disasm-table`

## Issue Summary

Startup crash (SIGSEGV) when `DebuggerWindow::setEmulator()` triggers disassembly before emulator is fully initialized.

## Documents

| File | Description |
|------|-------------|
| [crash-root-cause-analysis.md](./crash-root-cause-analysis.md) | Root cause analysis and master branch changes required |
| [hotfix-deferred-disassembly.md](./hotfix-deferred-disassembly.md) | Hotfix applied in `disasm-table` branch |
| [proposal-debugger-state.md](./proposal-debugger-state.md) | Long-term architectural improvement proposal |

## Branch Strategy

1. **Hotfix** (current): Applied in `disasm-table` branch  
2. **Master fix**: Port defensive guards and deferred init pattern
3. **Merge**: Merge master back into feature branches

## Master Branch Changes Required

```
unreal-qt/src/debugger/debuggerwindow.cpp     - Remove eager updateState()
unreal-qt/src/debugger/disassemblerwidget.h   - Add emulator() getter
unreal-qt/src/debugger/disassemblertablemodel.cpp - Add null guards
```

## Change Statistics (disasm-table vs master)
```
6 files changed, 1532 insertions(+), 1050 deletions(-)
```
