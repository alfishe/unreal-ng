# Spec256 — 256-Colour ZX Spectrum Extension: Research & Integration Analysis

**Created:** 2026-09-27
**Status:** research complete, implementation not started (see [TODO.md](TODO.md))

## What this folder is

A deep investigation of **Spec256** — the 256-colour ZX Spectrum graphics
extension created by the 1999 DOS emulator of the same name — covering its
execution model, file formats, palette, software catalog, every known
implementation (emulators and FPGA cores), and a concrete analysis of how
unreal-ng could support it.

## The one fact that changes everything

Spec256 is **not a port-driven video mode** and there is **no Spec256
hardware**. It is an *emulator-level co-processing model*: a fictitious
64-bit-wide "Z80_GFX" processor executes in lockstep with the real Z80,
mirroring every instruction with 8-byte-wide operands. Every Z80 address has
8 shadow bytes (one colour plane per bit of the colour index). A game's
unmodified bitmap-drawing code — the same `LD (HL),A` loops that move bitmap
bytes — simultaneously moves 8 bytes of pre-authored colour data per byte.
The 256-colour screen is rendered from the shadow ("GFX") memory instead of
the standard ULA screen; activation is a user/emulator toggle (F2/F3), fully
invisible to the running program.

This means unreal-ng integration is **not** another ATM/Profi-style
port-decoder + VRAM-banking job. The hard parts are:

1. the **lockstep shadow-execution engine** (8 colour planes reacting to every
   memory-affecting instruction, incl. RMW opcodes such as `XOR (HL)`),
2. the **512 KB GFX shadow RAM** that no Z80 instruction directly addresses,
3. **per-game correction profiles** (register alignment, leveled logicals)
   stored in each game's `.cfg`,
4. TTD capture of a machine whose interesting state lives outside both Z80
   memory and any I/O latch.

## Document index

| File | Contents |
|:--|:--|
| [spec256-format-and-mechanics.md](spec256-format-and-mechanics.md) | Technical reference: Z80_GFX execution model, GFX memory layout, on-disk `.gfx` byte order, palette, backgrounds, `.cfg` semantics, activation, all container file formats |
| [spec256-resources.md](spec256-resources.md) | Catalog: emulators, FPGA cores, games, tools, community links (all with URLs), plus what exists locally under the emulators collection |
| [spec256-emulator-survey.md](spec256-emulator-survey.md) | Implementation deep-dive of the reference sources, primarily the local **zxpoly** Java tree (container handling, plane codec, 8-core scheduler, renderer compositing, per-game DB) |
| [unreal-ng-integration-analysis.md](unreal-ng-integration-analysis.md) | How unreal-ng could support Spec256: design decisions (model vs flag, execution engine options, GFX RAM placement, TTD strategy), phased plan, extension-point checklist with file/line references, risks, open questions |

## Relationship to other work

- **[2026-09-27-zxpoly](../2026-09-27-zxpoly/)** — the ZX-Poly port analysis
  lists Spec256 as its deferred **Phase 4** ("8 gfx cores + alignRegs + leveled
  logicals + archive loader"). This folder is the deep-dive that phase
  consumes. The two tracks share machinery: zxpoly's Spec256 board mode is one
  ZX module + 8 satellite GFX cores, i.e. exactly the multi-CPU scheduler
  ZX-Poly Phases 1–2 would build (PLAN row #43).
- **TTD v2 migration (PLAN #40)** — the 512 KB GFX RAM is a textbook "device
  RAM region" for V1; doing #40-V1 first is the same sequencing rule the plan
  already states for TSConf/ZX-Poly.
- **Video debug translation (PLAN #42)** — Spec256's pixel→memory mapping
  (each pixel maps to *two* addresses: a bitmap byte and 8 plane bytes) should
  plug into the planned `IVideoMapper` interface rather than bypass it.
- **Pentagon 1024 16-colour mode (retired #21)** — the closest landed
  precedent (`M_P16`: decoder + mode enum + state reporting + TTD + tests),
  minus Spec256's execution-model twist.

## Provenance

Compiled from: the original 1999 emulator's readme (archived Emulatronia
zips), EmuZWin's `256_color_games.htm`/`EZXFormat_Eng.htm` documentation, the
local `github/zxpoly` source tree (verified line-by-line, incl. two test
archives usable as golden fixtures), GZX/oozx/FPGA-core sources and docs on
GitHub, and community threads (zx-pk.ru, Reddit, Wikipedia). Every claim in
the technical reference is tagged with its source; unresolved contradictions
are flagged explicitly (see bit-order note in the format doc).
