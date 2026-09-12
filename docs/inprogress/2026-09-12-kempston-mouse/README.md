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

## Notes

The per-model decode arms touch every port decoder. Note `portdecoder_atm710` and
`portdecoder_atm3` exist only on the `atm` branch, not on `master`, so the work spans
branches.
