# TODO — Debugger enhancements / xpeccy parity (2026-08-26)

**Status:** partially done — the analysis/proposal phase is complete; almost
none of the parity feature list itself is implemented (verified 2026-09-16: no
address history, marked addresses, port watch or heatmap code in
`unreal-qt/src/debugger/`).

## Progress
- Full analysis set: [proposal.md](proposal.md) (phased plan),
  [features-parity.md](features-parity.md), [xpeccy-comparison.md](xpeccy-comparison.md),
  [ui-mockups.md](ui-mockups.md).
- Adjacent work landed since the proposal, covering some Phase 4 ground by
  other routes: ULA beam widget (`ulabeamwidget.*`), memory-page map widgets
  (`memorypageswidget.*`, `memorypagesviswidget.*`), FDC status widget.
- Expression evaluator / breakpoints split into their own folders
  (`../2026-08-26-expression-evaluator/`, `../2026-08-26-breakpoint-enhancements/`).

## Remaining (value order)
1. **Phase 1 disassembly navigation** — address history back/forward, go-to-PC
   hotkey, follow-operand on Enter, marked addresses (highest daily-use value).
2. **Phase 3 flags/interrupts** — clickable flag checkboxes, IFF1/IFF2 + ISR
   address display (feeds IRQ breakpoints later).
3. **Phase 2 stack widget** — 9 entries, single-click jump, return-address
   hints.
4. **Phase 5 port watch** — PortRegistry list with value-change highlighting
   (note: `/ports` automation surface with tags already exists — build on it).
5. Signal indicators (DOS/ROM/INT), PreferenceManager abstraction.
6. **CMOS / RTC panel** (from PLAN #60(c), 2026-09-29) — the Qt view of the
   machine's clock chip: time, registers A-D and alarms decoded, a hex grid of
   every cell with in-place edit. Render the core report `DeviceState::Rtc` and
   write through `RtcAccess::Write`, the same calls CLI `rtc`, WebAPI
   `/state/rtc` + `/rtc/cells`, MCP `rtc`, Lua / Python `rtc_*` use - no Qt-only
   logic. Machines: ATM3 (the AVR's registers noted), Profi, Scorpion with SMUC.
7. **TTD port-events search panel** (2026-09-29, branch `ttd-o1-journals`) -
   the Qt view of "when did the program ...": an event picker (`key` with a key
   name, `ear`, `ay-read` / `ay-write` / `ay-select` with a register, `border`,
   `beeper`, raw `in` / `out` with port and value masks), options (time window,
   limit, newest first, trigger), and a hit list (frame, T-state, PC, port,
   value, AY register) where a double click seeks there and opens the
   disassembly at the PC. Search the current session or a `.ttd` file on disk
   without loading it. Call `TimeTravelManager::SearchPortEvents` /
   `ttd::BuildPortEventQuery` / `ttd::ApplyPortQueryOption` - the same calls CLI
   `ttd port-events`, WebAPI `/ttd/port-events`, MCP `port_events`, Lua /
   Python `ttd_port_events` use - no Qt-only logic. Show why a search is
   refused (no port journals on TSConf / ZX Next / NeoGS, recording running).
   Design: [ttd-port-read-journal.md](../../emulator/design/debugger/time-travel-debug/ttd-port-read-journal.md) §10.

## Pointers
- Cumulative plan: [`../PLAN.md`](../PLAN.md) — debugger parity (T4).
