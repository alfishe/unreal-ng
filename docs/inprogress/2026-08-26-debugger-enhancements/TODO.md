# TODO — Debugger enhancements / xpeccy parity (2026-08-26)

**Status:** the Qt usability items 1-7 below are done (2026-11-04); the
PreferenceManager abstraction and the remaining parity list are open.

## Progress
- Full analysis set: [proposal.md](proposal.md) (phased plan),
  [features-parity.md](features-parity.md), [xpeccy-comparison.md](xpeccy-comparison.md),
  [ui-mockups.md](ui-mockups.md).
- Adjacent work landed since the proposal, covering some Phase 4 ground by
  other routes: ULA beam widget (`ulabeamwidget.*`), memory-page map widgets
  (`memorypageswidget.*`, `memorypagesviswidget.*`), FDC status widget.
- Expression evaluator / breakpoints split into their own folders
  (`../2026-08-26-expression-evaluator/`, `../2026-08-26-breakpoint-enhancements/`).

## Done (2026-11-04)
1. **Disassembly navigation** (`disassemblerwidget.*`): address history of
   every jump (go to, follow, PC, marks; 64 places) - Backspace / Alt+Left
   back, Alt+Right forward; Home shows PC; Enter follows the cursor row's
   target (JP / CALL / JR / DJNZ / RST, JP (HL) through the live register,
   RET to the stack's return address, else a 16-bit operand); marks 1-5
   (Ctrl+N sets / clears, Alt+N shows), drawn as a digit after the breakpoint
   column; the same in a right-click menu.
2. **Flags / interrupts** (`registerswidget.*`): S Z 5 H 3 P N C cells, a
   click toggles the bit; IM, clickable IFF1 / IFF2, the ISR address (0038,
   or the IM 2 vector at I*256+FF) - a click shows it in the disassembly.
   Writes go through `Emulator::SetRegisterFromTool` (a remote session refuses).
3. **Stack** (`stackwidget.*`): 9 words SP-2 ... SP+E, click: disassembly,
   double click: memory view, right click: menu; a word right after a
   CALL / CALL cc / RST is marked "<- CALL nnnn" / "<- RST nn".
4. **Port watch** (`portwatchdialog.*`, Debugger "Machine" menu): the
   decoder's port map (port, mask, device, gate, latch value decoded, tags),
   a changed value lit for 3 s, TR-DOS and mouse routing state.
5. **Signal indicators** (disassembly header): DOS (the TR-DOS latch), the ROM
   page in 0000-3FFF (or RAM) with its title, INT asserted / NMI in service.
6. **CMOS clock** (`rtcdialog.*`): `DeviceState::Rtc` decoded (time,
   registers A-D, alarm) and a hex grid of every cell, edited in place
   through `RtcAccess::Write` (C and D read only); the reason when the
   machine has no clock.
7. **TTD port events** (`porteventsdialog.*`): the `port-events` verb of
   `ttd::TTDControl` with every option (event + argument, window, limit,
   newest, trigger, port / value masks, a `.ttd` file); a double click on a
   hit seeks there (`seek`) and shows its PC; a refusal shows the verb's
   message.

8. **Window layout** (`debuggerwindow.ui`): toolbar, speed control + FDC
   status, a band of framed panels of one height (Registers, Stack, Memory
   pages), a Memory | Disassembly splitter; one frame style and one panel
   font; no fixed sizes, the window's minimum comes from its contents (the
   panels never overlap). Panes find their window with `DebuggerWindow::Of`
   (their parent is a frame now).
9. **Memory view follows PC**: every stop shows PC's 16 KB page read again
   (it was empty until a click, and stale after a step), the PC byte marked.

Tests: `unreal-qt-tests --gtest_filter='DebuggerPanels_Test.*'` (clock grid
read / write / refusal, port watch highlight, port-events refusal).

## Remaining
- PreferenceManager abstraction.
- The rest of the parity list in [proposal.md](proposal.md) (heatmap, ...).

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — row #46, debugger parity (T3).
- Reference: [emulator debugger survey](../2026-09-28-emulator-debugger-survey/README.md)
  (2026-09-28): the full Xpeccy+ debugger and 15 other emulators' debuggers.
