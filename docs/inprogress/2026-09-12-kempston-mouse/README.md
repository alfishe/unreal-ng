# Kempston Mouse Emulation

Directory status: **implemented, tested and committed to master** (`a8767455`, 2026-09-12) —
see [DONE.md](DONE.md). The emulated device,
per-model port decoding, desktop (Qt) mouse capture, TTD record/replay of mouse input and
the automation surface (CLI, WebAPI, Python, Lua, MCP) are all in master. See
[Open items](#open-items) for what is left. Tests exist for the core device, pointer
arithmetic, decoders, funnel, TTD journalling and every automation front end (the Qt capture
itself has no automated test); they were **not re-run** for this documentation update.

Primary sources are the project's own port map (`speccy-bootcamp`
`10_references/io_port_map.md` @ `e50577b1`) and Black_Cat's *BC Info Guide #4*
(`tslabs/zx-evo`, `pentevo/docs/ZX/zx-ports-full-table.txt`) — the per-model table. They
agree character-for-character on the mouse decode patterns, and between them they settle
the button order and establish that **decoding is per-model**.

- [hardware-reference.md](hardware-reference.md) — how the real hardware behaves, every
  claim cited to a primary source, disagreements recorded rather than resolved silently.
- [design.md](design.md) — the design: decoding, device object, host pointer mapping, TTD
  integration, test architecture. Each decision is marked with what was actually built.
- [integration.md](integration.md) — how the pieces are wired together in the code as built.
- [automation-interfaces.md](automation-interfaces.md) — how scripts and agents drive the
  mouse (CLI, WebAPI, Python, Lua, MCP), with the decisions taken.
- [walkthrough.md](walkthrough.md) — implementation log and verification record, including
  the mistakes found after the first "done".

## Glossary

| Term | Meaning here |
|---|---|
| **Counter** | The mouse's X or Y register: an 8-bit number that goes up or down as the mouse moves and wraps (255 + 1 = 0). The program on the machine only ever sees these counters, never a screen position. |
| **Active-low** | A pressed button reads as bit `0`. `0xFF` = nothing pressed. |
| **Decode** | The rule a port decoder uses to decide whether an `IN` from a given 16-bit address belongs to this device. |
| **Mirror** | Another address that the same decode also accepts (because some address bits are not checked). |
| **Fitted** | The machine has the device plugged in, according to its config. Not fitted = nothing answers on the mouse ports. |
| **Funnel** | `DebugMouseManager`: the single core class every input source goes through, so the replay guard and TTD journalling live in one place. |
| **TTD** | Time-travel debugging: record execution, seek back, replay. |
| **Input journal** | The TTD list of timestamped input changes that replay feeds back in. |
| **Floating bus** | What an `IN` returns when no device answers (on these machines, usually `0xFF` or the byte the video circuit is reading). |

## Goal

Add Kempston Mouse support across every supported machine, with pointer motion that is
independent of monitor DPI, window size and upscale factor, and with input that survives
TTD record/replay without divergence.

## What shipped

**Device** — `core/src/emulator/io/mouse/mouse.{h,cpp}`, one per emulator on every model.
Three read-only registers: buttons (+ wheel) at `#FADF`, X at `#FBDF`, Y at `#FFDF`.
Counters power on at X = 31, Y = 85 (two different values, because software treats equal axes
as "no mouse"). A machine RESET keeps them (MiSTer behaviour). Counters are atomic, so host and automation moves that arrive at the same
moment both land.

**Button register, worked example.** Bits 0–2 are the buttons (0 = pressed; D0 left,
D1 right, D2 middle), bit 3 is always 1.
- `[INPUT] Wheel=NONE` (shipped default): bits 4–7 read 1. Idle = `0xFF`, left pressed = `0xFE`.
- `[INPUT] Wheel=KEMPSTON`: bits 4–7 carry the wheel counter. Idle with wheel at 0 = `0x0F`,
  left pressed and wheel at 2 = `0x2E`.

**Decoding** — the standard rule (A9 = 1 and A5-A0 = `011111`, as MiSTer decodes; A8 and A10 pick the register) on every
model decoder in this branch: ZX Spectrum 48K, 128K, +3, Pentagon 128 (and 512, which
inherits it), Scorpion ZS 256 / ProfROM, Profi. The mouse answers only when fitted, while the
TR-DOS ports are off the bus, and on addresses no other device already owns.

**Configuration** — `[INPUT] Mouse=NONE|KEMPSTON` fits the device (`AY` is logged as not
emulated and means none); `[INPUT] Wheel=NONE|KEMPSTON` (`KEYBOARD` is logged as not
implemented and means none). Every shipped `data/configs/*/unreal.ini` now has
`Mouse=KEMPSTON` and `Wheel=NONE`. Runtime feature `kempstonmouse` (alias `kmouse`, on by
default) can take the mouse off the bus without editing the config.

**Desktop capture (Qt)** — click the emulator screen to capture the pointer, `Esc` or focus
loss releases it (the `Esc` press never reaches the ZX keyboard). On macOS capture uses the
native relative mode (the cursor is frozen and raw travel is read from the OS), which needs
no Accessibility permission; elsewhere the cursor is re-centred after every move. One host
pixel of travel across the displayed image equals one emulated pixel, measured in physical
pixels over the source rectangle actually drawn (so window size, DPI and overscan crop do
not change the feel).

**TTD** — the mouse state (X, Y, buttons, wheel) is saved in every checkpoint
(`PeripheralId::KempstonMouse` = 7). Every mouse change — from the desktop, from automation
or from a timed click release — is journalled before it is applied while TTD records, and
refused while TTD replays. The same fix was applied to the keyboard: timed key operations and
desktop keystrokes used to bypass the journal and are now journalled and replay-guarded too.

**Automation** — CLI `mouse …`, WebAPI `/api/v1/emulator/{id}/mouse/*` (with OpenAPI),
Python `emu.mouse_*`, Lua `mouse_*`, MCP tool `mouse_input`. The keyboard WebAPI now also
returns 400 for unknown key names, as its OpenAPI always promised. Reference:
[command-interface.md §11](../../emulator/design/control-interfaces/command-interface.md#11-mouse-input-injection).

**Verified against real software** — the Scorpion ProfROM detects the mouse at page 5 `#08FB`
(`IN #FADF`, `AND #38`, `CP #38`: bits 3–5 must all read 1) and its service-monitor pointer
follows injected motion in the expected directions
(`core/tests/emulator/ports/models/scorpion_kempston_mouse_test.cpp`).

## Decisions taken

| # | Decision | Status | Rationale |
|---|---|---|---|
| 0 | Per-model decode predicate, overridable per decoder, standard decode as the default | **Implemented** | The hardware varies per model and per fitted card — [design §3.1](design.md#31-decoding-is-per-model-by-construction) |
| 1 | One host physical pixel = one emulated pixel, via float accumulator with carried remainder | **Implemented** (`mousedeltaaccumulator.h`) | Scale-invariant — [design §5.1](design.md#51-the-rule) |
| 2 | Keep OS pointer ballistics, do not use raw deltas | **Implemented** (macOS reads cooked `NSEvent` deltas) | Kempston Mouse software is pointer-style — [design §5.4](design.md#54-os-ballistics-a-fork-in-the-road-taken-deliberately) |
| 3 | ~~Narrow the joystick to a full `0x1F` low byte~~ | **Changed.** The Scorpion joystick already matched exactly `#FF1F` and still does. Its decode arm simply runs before the mouse arm. | [design §3.3](design.md#33-the-kempston-joystick-collision) |
| 4 | Extend the TTD input journal with a discriminated union rather than a parallel journal | **Implemented** (`TTDInputKind`) | One ordered stream keeps the ascending-time rule trivially true — [design §6.2](design.md#62-input-journal-the-real-work) |
| 5a | Buttons: D0 = Left, D1 = Right, D2 = Middle, active low | **Implemented** | Stated directly by bootcamp and BC#4 — [hardware-reference §4](hardware-reference.md#4-button-register) |
| 5 | Reset X/Y to 31/85, not 0/0 | **Implemented** | Software infers absence from equal axes — [hardware-reference §7](hardware-reference.md#7-presence-detection) |
| 6 | Reuse the old `CONFIG` fields rather than adding new ones | **Implemented** for `mouse`, `mousewheel`, `mouseswap`, `mousescale` (plus a new `mouseConfigured` flag) | [design §7](design.md#7-config-and-feature-gating) |
| 7 | Wheel **off** by default (`Wheel=NONE`) | **Implemented** | With a wheel fitted, bits 4–7 carry the counter, so an idle mouse reads `0x0F` and the ProfROM's bits-3-to-5 test fails: the ROM switches the mouse off. A classic Kempston mouse has no wheel. [design §7.2](design.md#72-mousewheel--three-modes-not-a-boolean) |
| 8 | One funnel (`DebugMouseManager`) for desktop, automation and tests; direct call, not a queued message | **Implemented** | Synchronous answers, deterministic pause → inject → run_frames, one place for the replay guard and journal — [automation-interfaces §4.1](automation-interfaces.md#41-core-funnel-debugmousemanager) |

## Earlier open questions

- **D-1 — RESOLVED.** Button order is D0 = Left, D1 = Right, D2 = Middle, active low.
- **D-2 — RESOLVED, differently than proposed.** The journal extension was done inside this
  work, not as a separate task. It did not change a file format: the input journal lives in
  memory only (the `.ttd` dump has no input section, for keyboard or mouse). Frame-boundary
  batching of motion was **not** done — see open items.
- **D-3 — still an assumption.** The standard decode is used on every model; BC#4 documents
  the mouse on four machines only. Overridable per decoder if a counter-example appears.
- **D-4 — resolved by the common TR-DOS rule.** While TR-DOS is selected only Beta Disk
  operations happen: no other device answers on any address until the selection is released.
  The mouse is silent on every address while `CF_DOSPORTS` is set. On Scorpion (aligned with the
  MiSTer ScorpionZS256 core, 2026-09-12), a TR-DOS session or the armed DOS trigger widens the
  Beta decode to A2-A0 = `111`, which owns every mouse address. The Shadow Monitor latch
  (`#1FFD` bit 1) alone hides only `#xx1F`/`#xx5F`, never `#xxDF`. The ProfROM mouse driver
  runs in page 5 with `#1FFD` = `#10` and TR-DOS off, so it still sees the mouse. Tests:
  `KempstonMouseDecodePriority_Test.ScorpionTrDosSelectionHandsMouseAddressesToBeta`,
  `KempstonMouseDecodePriority_Test.ScorpionMonitorLatchKeepsMouse`,
  `KempstonMouseModelDecode_Test.TrDosPortsHideMouse`.

## Open items

| Item | State |
|---|---|
| `MouseScale=` sensitivity | Applied by the desktop `MouseManager` as 2^scale when capture starts (`char` cast to signed - unsigned on ARM). Automation moves are exact emulated pixels and are not scaled. |
| `SwapMouse=` | Applied by the desktop `MouseManager` (left/right swapped when capture starts). Automation names buttons explicitly and is not affected. |
| `Wheel=KEYBOARD` | Not implemented (logged, treated as `NONE`). |
| Frame batching of host motion in the journal | Not done: each host motion event that yields a whole pixel is journalled. A 1000 Hz mouse during a long recording grows the journal quickly. |
| End-to-end TTD divergence test | `debugmousemanager_test.cpp` covers journalling, journal injection and checkpoint restore separately; a single record → seek → replay → compare-hash test with live mouse motion was not found. |
| Test run | The suites were not re-run for this documentation update. |
| `ports` representation | Integers in WebAPI, Python and Lua. |
| Status does not say when the ports are hidden | While TR-DOS ports are on the bus (or a peripheral owns the address) the mouse is silent but status still shows `present: true` (automation Q4). |
| USSR decode variant, 2-button option, `joymouse`, `lockmouse` | Not implemented (deferred). |
| ATM 7.10 / ZX Evo decoders | Exist only on the `atm` branch; not covered here. ZX Evo needs its stricter full-low-byte decode. |
| SDL front end | Design only ([design §9](design.md#9-if-the-front-end-is-sdl-rather-than-qt)). |
| `dy` + = up on more software | Confirmed on the Scorpion ProfROM only. Check Art Studio or similar. |
