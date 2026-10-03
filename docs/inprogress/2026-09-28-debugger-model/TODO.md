# TODO — debugger model (main + card debuggers, device plugins, one protocol)

Draft (2026-09-28): [README](README.md), [widget-catalog](widget-catalog.md), [rules](rules.md), [protocol](protocol.md), [gui-main-debugger](gui-main-debugger.md), [gui-card-debugger](gui-card-debugger.md) (delta: GS, then NeoGS), [debug-plugins](debug-plugins.md). No code.

- [ ] Fold in the emulator-debugger survey (Mesen2, MAME, WinUAE, vAmiga, FCEUX, BizHawk, Spectaculator, ZXSpin …) and the NedoOS metadata ideas
- [ ] Review
- [ ] Hand gui-main-debugger.md (M01-M13), gui-card-debugger.md (C01-C11, N01-N08) and debug-plugins.md (P01-P08) to a design agent
- [ ] Engine phases 1-2 of [gs-debugger design](../2026-09-27-gs-debugger/design.md) §10 (targets gs + neogs, coordinator)
- [ ] Phase 3: `DebugService`, serializer, WebSocket events, `cpu` on every surface, surface-parity test
- [ ] Plugins phases 1-3 (registry, event breakpoints, wrap the existing `DeviceState` builders)
- [ ] The GUI skin (main, then card), then the classic Unreal skin; the TUI skin (#49) on the same protocol

Plan: [PLAN.md](../PLAN.md) #45 (T2), #46 (debugger parity), #49 (TUI).
