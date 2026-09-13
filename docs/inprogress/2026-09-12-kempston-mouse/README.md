# Kempston Mouse Emulation

Directory status: **design only — nothing implemented.** No code has been written; this
directory is a design proposal with two open questions (below) that want answers before
implementation starts.

Primary sources are the project's own port map (`speccy-bootcamp`
`10_references/io_port_map.md` @ `e50577b1`) and Black_Cat's *BC Info Guide #4*
(`tslabs/zx-evo`, `pentevo/docs/ZX/zx-ports-full-table.txt`) — the per-model table. They
agree character-for-character on the mouse decode patterns, and between them they settle
the button order and establish that **decoding is per-model**.

- [hardware-reference.md](hardware-reference.md) — normative behaviour, every claim cited
  to a primary source, disagreements recorded rather than resolved silently.
- [design.md](design.md) — the TDD: decoding, device object, host pointer mapping, TTD
  integration, test architecture.
- [polling-detection-and-grab-control.md](polling-detection-and-grab-control.md) —
  enhancement: automatic grab based on polling detection + manual toolbar override.

## Goal

Add Kempston Mouse support across every supported machine, with pointer motion that is
independent of monitor DPI, window size and upscale factor, and with input that survives
TTD record/replay without divergence.

## What the research established

Two things were expected to be simple and turned out not to be.

**The registers are small; the decoding is not uniform.** Three read-only registers.
The standard decode qualifies on **A9 = 1, A5 = 0**, then A8 selects buttons vs axis and
A10 selects X vs Y — four address lines, everything else mirrored. But **each model
decoder has its own flavour and bit sensitivity**: BC#4 shows the Kempston *joystick*
decoded four different ways across four machines (KAY-1024SL answers on A0 alone), and
ZX Evo decodes the mouse's entire low byte where original hardware decodes only A5. A
"low byte == `#DF`" test is ZX Evo's behaviour, not the general rule, and using it
everywhere would silently break mirrored addresses that real hardware answers.

There is also a **USSR variant** with a different five-line decode (drops A9, adds A7 and
A0 = 1, so it answers only on odd addresses).

Axes are 8-bit wrapping counters; buttons are active-low with **D0 = Left, D1 = Right,
D2 = Middle**; the wheel lives in the upper nibble of the button register; bit 3 is a
constant 1. No "Kempston mouse turbo" exists in any source.

**The surrounding integration is where the work is.** Three findings drive the design:

1. **The mouse sits inside the Kempston joystick's decode window, on every model that
   has both** — and how badly depends on the model. `Kjoy(7)` decodes A0 alone, so it
   answers every odd port including all three mouse ports. Neither BC#4 nor bootcamp
   calls the overlap out; the emulators resolve it two different ways.
2. **TTD cannot currently replay mouse input at all.** `TTDInputEvent` is keyboard-only
   and replay injects into `pKeyboard` exclusively — there is no joystick or mouse
   journalling. Mouse deltas are host-asynchronous, so replay *will* diverge until the
   journal record is extended. This is the single largest piece of work in the feature
   and it changes a serialised format.
3. **Half the scaffolding already exists and is misleading.** `CONFIG` carries
   `input.mouse`, `mouseswap`, `mousescale`, `mousewheel`, `joymouse`, `lockmouse` and
   a `MOUSE_WHEEL_MODE` enum (three values, not a boolean) — all inherited from Unreal
   Speccy, none parsed by `config.cpp`. A logger submodule id is reserved. And
   `PortDecoder_Scorpion256` already claims the port space with a stub that returns
   `0x00` for both axes — which is precisely the equal-axes pattern software reads as
   "no mouse fitted".

## Decisions taken

| # | Decision | Rationale |
|---|---|---|
| 0 | Per-model decode predicate, overridable per decoder, standard decode as the default | The hardware varies per model and per fitted card — [design §3.1](design.md#31-decoding-is-per-model-by-construction) |
| 1 | One host physical pixel = one emulated pixel, via float accumulator with carried remainder | Scale-invariant; every reference emulator instead uses a fixed divisor and is therefore window/DPI dependent — [design §5.1](design.md#51-the-rule), [§5.3](design.md#53-why-not-the-reference-approach) |
| 2 | Keep OS pointer ballistics (Qt's default), do not use raw deltas | Kempston Mouse software is pointer-style, not mouse-look — [design §5.4](design.md#54-os-ballistics-a-fork-in-the-road-taken-deliberately) |
| 3 | Resolve the joystick collision by narrowing the joystick to a full `0x1F` low byte | Matches the ZX Evo FPGA and Xpeccy; needs no cross-device knowledge — [design §3.3](design.md#33-the-kempston-joystick-collision) |
| 4 | Extend the TTD input journal with a discriminated union rather than a parallel journal | One ordered stream keeps the ascending-time invariant trivially true — [design §6.2](design.md#62-input-journal-the-real-work) |
| 5a | Buttons: D0 = Left, D1 = Right, D2 = Middle, active low | bootcamp and BC#4 state the bit map directly; ZXMAK2 and zxsp's header disagree and are wrong — [hardware-reference §4](hardware-reference.md#4-button-register) |
| 5 | Reset X/Y to 31/85, not 0/0 | Software infers absence from equal axes — [hardware-reference §7](hardware-reference.md#7-presence-detection) |
| 6 | Reuse the dead `CONFIG` fields rather than adding new ones | They already carry the right names and a shipped ini documents them; `mousescale` is a power-of-two integer `[-3;3]`, not a float — [design §7](design.md#7-config-and-feature-gating) |
| 7 | Track polling via T-state timestamp, not a boolean | Allows window-based detection ("polled within last N T-states") and survives focus loss — [polling §3.1](polling-detection-and-grab-control.md#31-the-metric-port-read-timestamp) |
| 8 | Deferred grab: arm on focus, engage on first poll | Avoids grabbing cursor for software that does not use the mouse — [polling §3.6](polling-detection-and-grab-control.md#36-deferred-grab--wait-for-first-poll) |
| 9 | Manual disable via always-visible toolbar icon suppresses ALL mouse input | Discoverable; allows pre-emptive disable; crossed icon = no grab AND no events to emulator — [polling §4.6](polling-detection-and-grab-control.md#46-input-suppression-when-disabled) |
| 10 | Escape key releases grab before reaching emulator | User intent to escape grab takes priority; matches original Unreal behaviour — [polling §5.3](polling-detection-and-grab-control.md#53-escape-key-handling) |
| 11 | Grab logic lives inline in DeviceScreen (no separate controller) | DeviceScreen already owns mouse events; state is simple enough for inline fields — [design §5.6a](design.md#56a-cursor-grab-implementation-qt) |
| 12 | Warp-event discard via position matching (±1px), not flag | Flag races with queued events; position matching is deterministic — [design §5.6a](design.md#56a-cursor-grab-implementation-qt) |
| 13 | Delta computed from warp target, not from previous event position | Avoids accumulated drift from timing jitter — [design §5.6a](design.md#56a-cursor-grab-implementation-qt) |
| 14 | Polling telemetry in core, grab state machine in UI | Layer separation: core has no concept of cursor grab — [polling §3.0](polling-detection-and-grab-control.md#30-layer-separation-important) |
| 15 | `_lastPollTState` is `std::atomic<uint64_t>` | Cross-thread safety: emulation thread writes, UI thread reads — [polling §3.1](polling-detection-and-grab-control.md#31-the-metric-port-read-timestamp) |
| 16 | Escape releases grab; requires click to re-engage | Prevents infinite re-grab loop when software is actively polling — [polling §5.3](polling-detection-and-grab-control.md#53-escape-key-handling) |
| 17 | Sustained polling (2 frames in 100ms) required to engage grab | Filters out transient presence probes at boot — [polling §3.6](polling-detection-and-grab-control.md#36-deferred-grab--wait-for-sustained-polling) |
| 18 | No `grabMouse()` — rely on `BlankCursor` + event filtering | `grabMouse()` blocks menu/toolbar clicks — [polling §6.3b](polling-detection-and-grab-control.md#63b-no-grabmouse--menu-accessibility) |
| 19 | Focus-gaining click consumed, not injected | Prevents accidental in-game actions — [polling §5.4](polling-detection-and-grab-control.md#54-click-to-focus-handling) |
| 20 | Menu item + shortcut (Ctrl+G), not toolbar only | Toolbar can be hidden; menu ensures accessibility — [polling §4.2](polling-detection-and-grab-control.md#42-placement) |

## Open questions

These are judgement calls on contradictory or absent evidence. They are flagged in the
design at the point they bite.

- **D-1 — RESOLVED.** Button order is D0 = Left, D1 = Right, D2 = Middle, active low,
  stated directly by bootcamp and BC#4. The emulator disagreement was noise.
- **D-2 — TTD journal extension scope. Proposed: yes, split it out.** It changes a
  serialised format and its test fixtures, and it simultaneously unblocks joystick
  journalling, which is missing for the same reason — so it stands on its own merits
  rather than riding in on the mouse. Includes frame-boundary batching, without which a
  1000 Hz mouse inflates recordings by orders of magnitude.
  [design §6.2](design.md#62-input-journal-the-real-work)
- **D-3 (revised) — Default decode for machines that never documented a mouse.** Ten of
  the thirteen rows in the decode matrix are "add-on" or "unknown"; BC#4 documents the
  mouse on four machines only (ZX Spectrum, KAY-1024SL, Pentagon 128, Profi-1). Claims of
  verified hardware parity across 128K / +3 / Scorpion / ATM are not supported by any
  source examined. The
  original question assumed a per-model mask table exists; BC#4 shows the four machines
  that document the mouse all use the same standard decode and the rest never documented
  it at all. Narrower question: for an add-on on an undocumented machine, is the standard
  decode the right default? Proposed yes, overridable per decoder.
  [design §3.2](design.md#32-per-model-decode-matrix)
- **D-4 — TR-DOS gating. Mechanism found, not yet proof.** The low five bits of `#DF`
  are `11111`, the same qualifier the Beta 128 FDC uses, so a mouse read presents the
  Beta's address qualifier to the bus — which would collide on any Beta that decodes its
  system register loosely (A7 = 1 rather than a full A7:A5 match). That explains *why*
  ZXMAK2 and Xpeccy gate. It stops short of proof: on a fully decoded Beta, `#DF` is
  A7:A5 = `110`, not a defined register, so there is no collision at all — and our own
  `IsBeta128Port` matches the five ports exactly, so nothing collides here today.
  [design §3.5](design.md#35-tr-dos-gating)
- **D-5 — Polling timeout configurability.** 1 second works for typical presence-detection
  loops, but some software may poll less frequently. Proposed: fixed at 1s initially,
  configurable via `Mouse=KEMPSTON:timeout=2` syntax if user feedback demands it.
  [polling §9](polling-detection-and-grab-control.md#9-open-questions)
- **D-6 — RESOLVED: No.** If mouse emulation is disabled or the mouse reports absent,
  grab must never engage. Grabbing the cursor for a non-existent device is pointless.
  [polling §9](polling-detection-and-grab-control.md#9-open-questions)

## Notes

The per-model decode arms touch every port decoder. Note `portdecoder_atm710` and
`portdecoder_atm3` exist only on the `atm` branch, not on `master`, so the work spans
branches.
