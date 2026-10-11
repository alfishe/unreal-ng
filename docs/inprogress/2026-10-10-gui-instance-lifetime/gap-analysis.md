# Gap analysis

All numbers are from master `cca7d28c0` (2026-10-10).

## 1. Removal today

`EmulatorManager::RemoveEmulatorInstance()` (`core/src/emulator/emulatormanager.cpp`) runs on the removing thread
(a WebAPI / CLI / MCP worker, or the Qt thread for a GUI-initiated close):

| Step | What happens | Thread |
|------|--------------|--------|
| 1 | `BeginRetirement()`: `LeaseContext()` refuses from now on | remover |
| 2 | Post `NC_EMULATOR_INSTANCE_DESTROYED`, `WaitForOutstandingMessages()` (with a timeout) | remover waits for the MessageCenter worker |
| 3 | `WaitForContextLeases()`: exclusive lock on `_leaseMutex`, waits for live leases | remover |
| 4 | Out of `_emulators` under `_emulatorsMutex`; clear the selection | remover |
| 5 | `Stop()` (joins the emulation thread), then `Release()` | remover |

`Emulator::Release()` → `ReleaseNoGuard()` (`core/src/emulator/emulator.cpp`) detaches host input and then
**deletes** the debug manager, the TTD controller and manager, the main loop, the RZX session, the floppy and tape
slots, the drives, `Core` (and with it every device), the media manager and the config. Last, it nulls `_context`
and deletes it. The `Emulator` object itself survives as long as someone holds a `shared_ptr<Emulator>`, but it is
an empty shell: `GetContext()` returns null, and `GetMemory()` / `GetDebugManager()` etc. dereference null.

## 2. The GUI side

`MainWindow::handleEmulatorInstanceDestroyed()` runs on the **MessageCenter worker** inside step 2:

- `_screenWrapper->clearFrameSource()` runs synchronously, inside the drain, so the paint path is safe.
- Everything else (`m_binding->unbind()`, `debuggerWindow->setEmulator(nullptr)`, the menu, the audio and video
  settings widgets, `_emulator = nullptr`) is a **queued** lambda on the Qt thread.

The drain in step 2 waits for the MessageCenter handler, not for the queued lambda. So the order on a typical
removal is:

```
remover:      BeginRetirement ─ post DESTROYED ─ drain ─ wait leases ─ Stop ─ Release (free) ─────────────
MC worker:                       handler: clearFrameSource, queue unbind ┘
Qt thread:    ... timer / queued signal / click → GetContext() → freed memory ... unbind (too late) ...
```

The **window** is everything the Qt thread runs between the free and the queued unbind: timers (29 GUI `.cpp`
files use `QTimer`), queued signals from `EmulatorBinding`, `updateMenuStates` queued by other notifications,
user input, and slots of dialogs that are not bound through the binding at all.

## 3. What protects what today

| Mechanism | Covers | Gap |
|-----------|--------|-----|
| `ContextLease` (`Emulator::LeaseContext`) | 13 calls in 12 files: status bar, tool bar, menu, screen widgets, TTD observer, FDC widget, host keyboard menu, debugger window (one site), `MainWindow` (one site) | Opt-in per site. A site that forgets is silent until a stress hits it; the rule "never hold one across a wait" is easy to break |
| `shared_ptr<Emulator>` in `MainWindow::_emulator` | Keeps the `Emulator` object | Not the context: `Release()` frees it regardless |
| `_screenWrapper->clearFrameSource()` in the drain | The paint path | Only the paint path |
| `IsReleased()` checks | A handful of sites | Check-then-use race: the free can happen right after the check |
| `GetContext()` nulls `_context` before the delete | A reader that loads the pointer after the free sees null | A reader that loaded it before the free uses a freed pointer, and most sites do not check for null either |

## 4. Inventory

Reads that reach the context without a lease, by enclosing function: `GetContext()` plus the `Emulator` accessors
that dereference `_context` (`GetMemory`, `GetDebugManager`, `GetBreakpointManager`, `GetZ80State`,
`GetFeatureManager`, `GetLabelManager`, `GetMainLoop`, `GetDisassembler`).

| | Count |
|-|-------|
| `GetContext()` calls in `unreal-qt/src` | 144 |
| Other context-dereferencing accessor calls | ~55 |
| Total unleased reads | **199** |
| Enclosing functions | **159** |
| Files | 38 |
| `LeaseContext()` calls | 13 (12 files) |

Raw pointers stored in GUI objects that outlive a removal unless someone clears them:

| Kind | Members |
|------|---------|
| `Emulator*` | `EmulatorBinding::m_emulator`, `DebuggerWindow::_emulator` (legacy), `GuiEmulatorContext::emulator`, and the dialogs and widgets `PcHistoryDialog`, `BreakpointDialog`, `BreakpointGroupDialog`, `BreakpointEditor`, `DeviceMemoryDialog`, `DiskFilesDialog`, `DiskSectorDialog`, `DebugVisualizationWindow`, `MemoryPagesVisWidget`, `MemoryWidget`, `Memory16KBWidget`, `BorderTimingWidget`, `ULABeamWidget`, `FloppyDiskWidget`, `SpeedControlWidget` |
| `EmulatorContext*` | `AudioSettingsWidget::_context`, `VideoRecordingWidget::_context`, `HudModel::_context`, `HudSettingsDialog::_context`, `GuiEmulatorContext::context`, `StatusBarManager::_biosIssuesContext` (identity only), `Ft812DebugWindow::_measuring` |

Only the binding, the debugger window, the menu, the screen and the two settings widgets are cleared in the
queued unbind. The dialogs keep a raw `Emulator*` even after the `shared_ptr` is gone, so they can read a freed
`Emulator`, not just a freed context.

Full list: [appendix-inventory.md](appendix-inventory.md).

## 5. Why per-site fixing does not converge

- **Coverage.** The WebAPI stress (`tools/sanitize/stress_round.sh`) runs an offscreen GUI with no windows open.
  It reaches the main window, the menu, the status and tool bars and the screen. The debugger, the TTD widget,
  the media panel, the memory viewers and the dialogs (most of the inventory) are never open, so the stress
  cannot find them.
- **Timing.** A site fails only when a removal lands inside its few-microsecond window. ASan reports the hit;
  a clean run proves nothing.
- **Growth.** New GUI code copies the existing `GetContext()` idiom. Nothing flags a new unleased read.
- **The lease rule is hard to keep.** A lease must not be held across anything that waits for a thread that
  may be removing the instance. Long handlers (load a file, reset, step out) do exactly that.

## 6. Other holders (outside the GUI)

WebAPI, CLI and MCP handlers hold a `shared_ptr<Emulator>` for the duration of a request and read the context
the same way: `core/automation` has 771 `GetContext()` calls and no `LeaseContext()`. They are not part of #11, but the core-side change in [proposal.md](proposal.md) covers them
automatically: a request that is in flight during a removal reads a retired machine, not freed memory.
