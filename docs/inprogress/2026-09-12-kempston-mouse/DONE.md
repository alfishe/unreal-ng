# DONE — Kempston Mouse emulation (2026-09-12)

**Status:** complete — implemented, tested and committed to master
(`a8767455` "feat(input): add Kempston Mouse with host capture, automation and
TTD"). The README line "not yet committed" predates the commit and is
superseded by this marker.

## What landed
- Device `core/src/emulator/io/mouse/mouse.{h,cpp}` (buttons at `#FADF`, X `#FBDF`,
  Y `#FFDF`; power-on 31/85; RESET keeps counters; atomic counters).
- Per-model decode on every master-branch decoder (48K, 128K, +3, Pentagon
  128/512, Scorpion ZS 256 / ProfROM, Profi), gated by `[INPUT] Mouse=KEMPSTON`,
  wheel via `[INPUT] Wheel=NONE|KEMPSTON`, feature `kempstonmouse` (`kmouse`).
- Desktop capture in Qt (macOS native relative mode, no Accessibility
  permission; `Esc`/focus-loss release), DPI/scale-invariant 1-host-pixel =
  1-emulated-pixel mapping via `mousedeltaaccumulator.h`.
- TTD: `PeripheralId::KempstonMouse` = 7 in every checkpoint; discriminated-union
  input journal (`TTDInputKind`) covering desktop, automation and timed inputs,
  with replay guard; the same fix hardened keyboard journalling.
- Automation parity: CLI `mouse …`, WebAPI `/mouse/*` (+ OpenAPI), Python
  `emu.mouse_*`, Lua `mouse_*`, MCP `mouse_input`; single funnel
  `DebugMouseManager`.
- Later closed by the 2026-09-14 triage program (P2-1): `/mouse/status` now
  carries `routing {ports_decoded, note}` and `inspect_state` gained the `mouse`
  aspect — the README "automation Q4" open item is resolved.

## Evidence
- All design decisions in [README.md](README.md) "Decisions taken" marked
  **Implemented**; verification against real software: Scorpion ProfROM page-5
  detection + service-monitor pointer (`scorpion_kempston_mouse_test.cpp`).
- Test suites: core device, pointer arithmetic, per-model decoders, funnel,
  TTD journalling, every automation front end — all committed with `a8767455`.

## Follow-ups
- Small items from [README.md](README.md) "Open items", none blocking: frame
  batching of host motion in the journal (1000 Hz mice grow it), one end-to-end
  record→seek→replay divergence test, `Wheel=KEYBOARD` (unimplemented, logged),
  USSR decode variant / 2-button / `joymouse` / `lockmouse` (deferred), SDL
  front end (design only), `dy += up` confirmation on more software.
- ATM 7.10 / ZX-Evo decoders exist only on the `atm` branch (see
  `2026-09-10-atm-debugging` / atm-merge item in PLAN.md).
