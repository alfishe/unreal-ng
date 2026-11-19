# TODO — debugger model (main + card debuggers, device plugins, one protocol)

Draft (2026-09-28): [README](README.md), [widget-catalog](widget-catalog.md), [rules](rules.md), [protocol](protocol.md), [gui-main-debugger](gui-main-debugger.md), [gui-card-debugger](gui-card-debugger.md) (delta: GS, then NeoGS), [debug-plugins](debug-plugins.md). No code.

- [ ] Fold in the emulator-debugger survey (Mesen2, MAME, WinUAE, vAmiga, FCEUX, BizHawk, Spectaculator, ZXSpin …) and the NedoOS metadata ideas
- [ ] Review
- [ ] Hand gui-main-debugger.md (M01-M13), gui-card-debugger.md (C01-C11, N01-N08) and debug-plugins.md (P01-P08) to a design agent
- [ ] Engine phases 1-2 of [gs-debugger design](../2026-09-27-gs-debugger/design.md) §10 (targets gs + neogs, coordinator)
- [x] Ahead of Phase 3 (2026-10-03): WebSocket events for the main CPU (`debug` topic: `paused`, `resumed`, `step_done`, `breakpoints_changed`, `seq` per emulator) and the step's stop reason on WebAPI / CLI / MCP / Lua / Python; a direct step never parks on a breakpoint (`Emulator::OnBreakpointHit`)
- [ ] Phase 3, as built (StateNode builders, `*control` namespaces, a per-call `cpu`; no `DebugService`, `debugjson`, target object, `timeline` / `cmdlog` topics, `positions`):
  - [x] Stage 1 (2026-11-12): `debug` events `edited` (every tool edit, from `Emulator::EditMemoryFromTool`), `run_control`, `labels_changed` (with the instance), `targets_changed` (on a change of the card CPUs)
  - [ ] Stage 2: one error `code` field on every surface (added beside the existing fields)
  - [ ] Stage 3: `cpu` where it is missing (CPU list, snapshot, page and block I/O, scripts' breakpoint notes, MCP disassemble / memory banks, find / fill / assemble, stack / calls, run-to on a card, breakpoint import / export); the snapshot's `cpu: "z80"` gets a field beside it, not a rename
  - [ ] Stage 4: surface-parity test
- [ ] Plugins phases 1-3 (registry, event breakpoints, wrap the existing `DeviceState` builders)
- [ ] The GUI skin (main, then card), then the classic Unreal skin; the TUI skin (#49) on the same protocol

Plan: [PLAN.md](../PLAN.md) #45 (T2), #46 (debugger parity), #49 (TUI).
