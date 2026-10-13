# Heavy machines: where a Sprinter, ZX Next and TS-Conf frame goes, and what to optimise

**Task (owner, 2026-10-13):** one more performance cycle for the heaviest configurations, Sprinter, ZX Next and
TS-Conf: measure, profile, and propose what to optimise. This document is the analysis and the proposals; nothing
in the emulator was changed (two proposals were tried as throwaway prototypes to measure them, section 5).

Before this, the cards were taken out of the picture: idle sound cards now cost ~nothing
([2026-10-12-peripheral-standby](../2026-10-12-peripheral-standby/results.md)). What is left is the machines
themselves.

## Contents

- [1. Summary](#1-summary)
- [2. Method](#2-method)
- [3. Numbers](#3-numbers)
- [4. Where the time goes](#4-where-the-time-goes)
- [5. Proposals](#5-proposals)
- [6. Suggested order](#6-suggested-order)
- [7. Reproducing](#7-reproducing)

## 1. Summary

The real-time budget of a frame is 20 ms (Sprinter 20.48 ms); the numbers are host CPU per emulated frame on one
thread of the dev Mac (Apple Silicon, Release).

| Machine | Worst measured 1x frame | Turbo (max speed) | What dominates |
|:--|--:|--:|:--|
| TS-Conf | 8.3 ms (Wild Commander idle) | 2.8-3.0 ms | TSFM idle check per instruction (1x), per-instruction machinery, the screen per instruction, DRAM arbitration with the TSU |
| ZX Next | 8.3 ms (ScrollNutter), 5-8 ms with any demo | 3.4 ms (Spectron 2084) | the line compositor (1x), the interrupt poll per instruction (UART + CTC), the Z80N glue |
| Sprinter | 3.0 ms (ROTOZOOM with sound) | 1.2 ms | already optimised (2026-10-09); the AY per step at 1x, the text renderer |

The biggest single items:

1. **TSFM at 1x: the idle check runs every instruction** (TS-Conf 1x 27 % of the frame, every machine with a TSFM
   at 1x). Reordering two conditions, exactly the same result, measured: **TS-Conf 1x 8.4 -> 6.2 ms (-26 %),
   with sprites -21 %, Pentagon 1x 945 -> 777 µs (-18 %)** (P1).
2. **Next: the line compositor walks a string per pixel** (Next 1x idle 39 % of the frame, 12-18 % with demos) (P4).
3. **Next: the interrupt poll steps the UART on every instruction**, because the ESP model is always attached to
   UART 0, and walks the four CTC channels (11-25 % of the frame with a program running) (P3).
4. **Next: a fresh bug from today**, `e0ec30376`: a missing pair of braces in the interrupt poller makes the UART
   step unconditional. With the shipped configuration it costs nothing extra (UART 0 is never idle, item 3), but
   the code no longer does what its comment says (P2).

## 2. Method

### Rows

`BM_Prof_*` had TS-Conf turbo rows only. Added in this cycle
([frame_profile_benchmark.cpp](../../../core/benchmarks/emulator/profiles/frame_profile_benchmark.cpp)):

- `TSConf_ShipNormal`, `TSConf_BareTurbo`; `Sprinter_ShipNormal / ShipTurbo / BareTurbo`;
  `Next_ShipNormal / ShipTurbo / BareTurbo`.
- Rows with a program (`FRAME_PROFILE_WORKLOAD`: loaded after the 300 boot frames, then a few frames settle):
  `TSConf_Sprites_ShipNormal / ShipTurbo` (the TS-Conf SDK sprite example, `testdata/machines/tsconf/spg/sprites.spg`).
- Local rows, not committed (the files are a private collection, not in the repository): the Next demos
  `Spectron 2084`, `copper_test` (em00k copper wobble), `show512`, `tm` (tilemap), `ScrollNutter`, loaded as `.nex`.
  The snapshot path of a workload row may be absolute for this.

What the idle rows show (screenshots of the timed frames, checked by eye):

| Row | Screen |
|:--|:--|
| `TSConf_Ship*` | Wild Commander 1.11i on the shipped SD image, two panels |
| `TSConf_Sprites_*` | TSU sprites over the tile background |
| `Sprinter_Ship*` | the BIOS screen "Boot from HDD ... fail, PRESS ENTER" (no HDD in the shipped config): a CPU poll loop |
| `Next_Ship*` | the 128 menu (no SD card in the shipped config) |
| `Next_<Demo>_*` | the demo running |

The Sprinter programs come from `BM_SprinterDemo_*` (DNTBLINK, ROTOZOOM, PLASMA2, BADAPPLE from the system HDD,
`UNREAL_SPRINTER_HDD`).

### Profiles

xctrace Time Profiler ([tools/profiling/xctrace](../../../tools/profiling/xctrace/README.md)), 15 s per row.

**Pitfall found on the way.** Profiling a row by launching it puts its boot frames into the profile: every run of
a row boots 300 frames at 1x, and Google Benchmark runs a row 4-5 times before the timed run. For a turbo row of a
slow machine that is most of the samples (TS-Conf: 300 boot frames x 8 ms per run against ~1 s of timed turbo
frames), and they show work the turbo frame does not do. The first round of profiles here said "TSFM synthesis and
the Next renderer in turbo"; counters inside the device showed that the turbo frames defer every TSFM step and
render one frame in N. Moving the boot into a non-inlined function does not help: xctrace attributes its frames to
the neighbouring function. The profiles below are taken by attaching to the benchmark during its last run (raise
`MinTime` locally to 40 s, wait until the last run has started, `record.sh -p <pid> -t 15`). The recipe is in the
profiles README, section 5.

## 3. Numbers

`BM_Prof_*` and `BM_SprinterDemo_*`, minimum `cpu_time` over two runs, load average 14-24 (the machine is shared;
the shares in section 4 are more robust than the absolute numbers).

| Row | µs / frame | fps | Program |
|:--|--:|--:|:--|
| TSConf_ShipNormal | 8,297 | 121 | Wild Commander idle |
| TSConf_ShipTurbo | 3,011 | 332 | |
| TSConf_BareTurbo | 2,772 | 361 | |
| TSConf_MaxTurbo | 2,748 | 364 | |
| TSConf_Sprites_ShipNormal | 5,575 | 179 | TSU sprites |
| TSConf_Sprites_ShipTurbo | 2,828 | 354 | |
| Next_ShipNormal | 2,210 | 453 | 128 menu |
| Next_ShipTurbo | 535 | 1,870 | |
| Next_BareTurbo | 539 | 1,856 | |
| Next_Spectron_ShipNormal (local) | 5,104 | 196 | game title, sprites |
| Next_Spectron_ShipTurbo (local) | 3,397 | 294 | |
| Next_Copper_ShipNormal (local) | 7,698 | 130 | copper wobble over Layer 2 |
| Next_Show512_ShipNormal (local) | 6,221 | 161 | Layer 2 pictures |
| Next_Tilemap_ShipNormal (local) | 7,154 | 140 | tilemap |
| Next_Scroll_ShipNormal (local) | 8,255 | 121 | sprites scroller |
| Sprinter_ShipNormal | 1,773 | 564 | BIOS boot-fail screen |
| Sprinter_ShipTurbo | 1,193 | 839 | |
| SprinterDemo_DontBlink Bare / Shipped | 1,320 / 1,640 | | |
| SprinterDemo_Rotozoom Bare / Shipped | 2,445 / 3,023 | | |
| SprinterDemo_Plasma2 Bare / Shipped | 608 / 866 | | |
| SprinterDemo_BadApple Bare / Shipped | 930 / 1,184 | | |
| (Pentagon_ShipNormal / ShipTurbo) | 955 / 158 | | for scale |

Read across: a TS-Conf or a Next running a program costs 6-9x a Pentagon frame at 1x and 18-21x in turbo. In
turbo the Next with a 28 MHz program (Spectron) is only 1.5x faster than at 1x: the CPU and the per-instruction
work are the frame, not the output.

## 4. Where the time goes

Shares of the timed frame (xctrace, attached, inclusive unless "self").

### TS-Conf

| Item | 1x idle | 1x sprites | Turbo idle | Turbo sprites |
|:--|--:|--:|--:|--:|
| TSFM `syncTo` per instruction, of it the idle check `fmIdle()` | **38 % (27 %)** | **30 % (21 %)** | 5 % (step deferral only) | 3 % |
| Screen per instruction (`ScreenZX::UpdateScreen` -> `ScreenTSConf::DrawRange` / `DrawLineSpan`) | 25 % | 14 % | 11 % (one rendered frame in N) | 5 % |
| Memory reads with DRAM arbitration (`TsConfMemory::AfterRead` -> `DramWait`, `TsConfArbiter`) | 2 % | 20 % | 6 % | **37 %** |
| Interrupt raster catch-up (`TsConfInterrupts::CatchUp`, `OnWait` per wait tick) | 2 % | 5 % self | 5 % | **12 %** |
| Engine `OnMachineStep` -> `CatchUp`, `AccountBudget` | 4 % | 4 % | 11 % | 6 % |
| Halted CPU (`Z80::HaltedM1`, the engine around every 4 T) | 2 % | 1 % | 6 % | 2 % |
| Z80 step dispatch (`StepInstructionWithWork`, `RunInstructionStartHooks`, `OnCPUStep`) | 5 % | 4 % | 14 % | 8 % |

- At 1x the TSFM is the largest item, and almost all of it is the **check** whether the FM half is idle
  (`fm_operator::idle` 17 % self): `Ym2203Pair::advanceChip` calls `c.fm.fmIdle()` before it knows whether the
  instruction crosses an FM sample clock at all, so every instruction walks 2 chips x 3 channels x 4 operators.
  Most instructions do not reach a sample clock (12 x prescaler master clocks).
- The screen is drawn after every instruction (`MainLoop::OnCPUStep`): the per-call setup of `DrawLineSpan`
  (palette refresh, the line's window, memsets) is paid ~70,000 times a frame at 14 MHz for a few dots each.
- With the TSU active, every CPU memory access goes through the DRAM arbiter and its waits through
  `TsConfInterrupts::OnWait`, which catches the raster up per wait tick.

### ZX Next

| Item | 1x idle (128 menu) | 1x Spectron | 1x copper | 1x scroller | Turbo idle | Turbo Spectron |
|:--|--:|--:|--:|--:|--:|--:|
| Rendering (`ScreenNext::RenderUpTo`) | **67 %** | 32 % | 37 % | 27 % | 7 % | 11 % |
| of it `ComposeLine` (self) | 39 % | 14 % | 18 % | 12 % | 4 % | 5 % |
| of it `UlaLine` + `UlaFetchAt` (self) | 21 % | 8 % | 12 % | 6 % | 2 % | 2 % |
| Interrupt poll (`PortDecoder_Next` poller lambda: CTC + UART) | 5 % | 17 % | 11 % | 11 % | **20 %** | **25 %** |
| of it `NextUart::Step` / `Advance` | 3 % | 10 % | 7 % | 6 % | 12 % | 15 % |
| Z80N step (`Z80NEngine::ExecuteStep`, memory reads, contention) | 10 % | 16 % | 26 % | 27 % | 38 % | 23 % |
| DivMMC / M1 chain per opcode fetch (`BeforeMachineM1`, `OnMachineM1`, `Classify`) | 1 % | 3 % | 2 % | 2 % | 5 % | 5 % |
| Copper (`NextCopper::Poll` -> `RunTo`, mostly the render before a MOVE) | - | - | 20 % | - | - | - |
| Sprite fill (`NextSprites::FillClocks`, `UpdateAnchor`) | - | 2 % | - | 6 % | - | - |

- **The compositor.** `NextVideoRenderer::ComposeLine` handles 640 pixels a line (14 MHz pixels), and for every
  pixel it decides the layer order by walking the string `kOrders[order]` (`"SLU"`, `"LSU"`, ...) with three
  character compares (`nextvideorenderer.cpp`, the loop at the end of `ComposeLine`), plus the stencil and blend
  branches. The order and the modes are the same for the whole line. On the 128 menu that is 39 % of the frame for
  a screen that is plain ULA. The line is also composed from four full `Pixel` arrays that `DrawLayers` fills even
  for layers that are off.
- **Per segment.** `RenderUpTo` copies all 256 next registers (`in.nr[r] = board.Stored(r)`) for every segment;
  a copper demo renders many partial segments per line (one before every MOVE), so the copy and the stack arrays
  (`___chkstk_darwin` 1-2 %) repeat.
- **The interrupt poll.** `NextInterruptSource::IsIntAsserted` runs the poller lambda before every instruction:
  `NextCtc::Advance` walks the four channels, then the UART. `NextUart::Idle()` is meant to skip the UART, but
  UART 0 always has a peer, the ESP model `NetworkManager` attaches through `attachSerialPeer`, so `Live()` is set
  and the UART steps both channels every instruction (`NextUart::Step` 7-10 % self). In turbo, where nothing is
  drawn, the poll is the largest single item.

### Sprinter

Optimised in the 2026-10-09 cycle (halted CPU in one go, a kept "no" for the INT question, the screen on its
events, the span renderer, the fused bus, the accelerator's quiet fetches;
[sprinter-cpu-and-peripherals.md §8](../../emulator/design/core/sprinter-cpu-and-peripherals.md)). What is left:

| Item | 1x BIOS screen | Turbo BIOS screen | ROTOZOOM shipped | DNTBLINK shipped | BADAPPLE shipped |
|:--|--:|--:|--:|--:|--:|
| CPU (`Z84CpuStep`, `Z84C15Engine::MemRead`, the fused bus) | 33 % | 54 % | | | |
| AY per step (`SoundChip_TurboSound::handleStep` + `updateState`) | **12 %** | 3 % (suppressed; still computes `nowT`) | 12 % | 9 % | 15 % |
| Text renderer (`SymbolUnit`) | 13 % | 2 % | - | - | - |
| Graphics renderer (`GraphicsSegment`, `DrawSpanImpl`) | 3 % | 4 % | 8 % | 13 % | 18 % |
| Video RAM write path (`SprinterMemory::OnWrite`, accelerator `AfterWrite`) | - | - | 15 % | 23 % | 11 % |
| WD1793 per step | 6 % (boot-fail state) | 7 % | 1 % | | |

## 5. Proposals

Each proposal keeps the machine state exactly as it is now (the rule of every speed change here: the fast path
reaches the state the stepped path reaches, bit for bit; a comparison test runs both). "Gain" is measured where it
says so, otherwise it is the share of the profile the change removes, as an upper bound.

### P1. TSFM: test "does this step reach a sample clock" before the idle check (measured)

`Ym2203Pair::advanceChip` (`core/src/emulator/sound/chips/tsfm/ym2203pair.cpp`):

```cpp
if (_fmIdleFastPath && toTimer > 1 && c.fm.fmIdle())   // walks every operator
{
    const int32_t n = std::min(delta, toTimer - 1);
    if (n >= toClock) { ...idle chunk... }
}
```

Reorder to `... && std::min(delta, toTimer - 1) >= toClock && c.fm.fmIdle()`. The branch taken is the same for
every input (the idle chunk was entered only when both held), so the result is the same, and most instructions no
longer walk the operators.

- **Measured** (throwaway prototype, interleaved A B B A x 2, minimum): TS-Conf 1x 8,418 -> 6,203 µs (**-26 %**),
  TS-Conf sprites 1x 5,570 -> 4,416 µs (**-21 %**), Pentagon 1x 945 -> 777 µs (**-18 %**). Every machine with a TSFM
  at 1x gains.
- Effort: one line; the existing `Ym2203Pair_Test.IdleFmFastPathMatchesTheClockedPair` and the TSFM goldens cover
  it.

### P1b. TSFM: remember that the FM half is idle

After P1 the check still runs once per FM sample clock (~49 kHz at the default prescaler, x 2 chips). Idle stays idle until something changes
it: an FM register write, a CSM key-on from timer A, a state load or a reset. Cache the result per chip and drop
it on those events. The engine-level part of `idle()` that depends on the envelope counter's sub-count must stay
live (or be folded into the cached state).

- Gain: the rest of `fmIdle` at 1x; a few per cent on TS-Conf / Pentagon after P1.
- Effort: small, in the ymfm patch 6 area (`PATCHES.md` §6 gets a line).

### P2. Next: restore the braces in the interrupt poller (correctness)

`e0ec30376` (2026-10-13) made `portdecoder_next.cpp`:

```cpp
if (!_uart.Idle())
    SyncPiLine();
    _uart.Advance(now);          // indented as if conditional, runs always
```

`NextUart::Idle()` says "the machine's per-instruction poll skips Advance then". Whether `SyncPiLine()` must run
while idle (a Pi line connected by NR #A0 while nothing else moves has to make the UART non-idle) is for the
author to decide. The cleaner fix is to call `SyncPiLine()` where NR #A0 is written and keep the poll as it was
before. **Measured** with `SyncPiLine(); if (!Idle()) Advance();`: no change in the shipped configuration, because
UART 0 is never idle (P3). It still matters for a correct reading of the code and for configurations without the
network.

### P3. Next: an interrupt poll that does nothing until something is due

Today, before every instruction: `NextCtc::Advance(now)` walks the four channels, and the UART steps both channels
and polls its interrupts. That is 11-25 % of a Next frame with a program (turbo: the largest item).

- **CTC:** keep `nextEventAt` = the earliest `NextZero` of the running timer channels, recomputed on a CTC write,
  a channel's zero count and a reset. The poll returns while `now < nextEventAt`. A counter-mode channel moves on
  its trigger, not on the clock.
- **UART:** a channel with a quiet peer is idle too. Nothing moves until the peer has a byte (the peers already
  call `onReceive`, which runs `Advance` and `PollInterrupts`), a byte is in the shifter or the FIFO, or the line
  changes. `Idle()` would then test `peer->HasByte()` / `BreakHeld()` instead of `Live()`, or keep a "next due"
  time like the CTC. `PollInterrupts` only needs to run when the FIFO state or the enables changed (a version
  counter, as the Sprinter's `ChainSource` keeps its "no").
- The pattern is the Sprinter's `cfc87408c` (the INT question answered from a kept "no").
- Gain: up to 15-20 % of a Next frame. Effort: medium. Exactness: the CTC and UART states at every port read and at
  every interrupt must be the same: a lockstep test that polls every instruction against the gated poll, with
  random CTC programming and a scripted peer.

### P4. Next: compose a line with the order resolved once

`ComposeLine` decides per pixel what is fixed per line: the layer order (`NR #15` bits 4:2), stencil, blend, and
which layers are on. Proposals, in order of size:

1. Resolve the order once per line: a template parameter or a table of three layer indices, instead of walking
   `"SLU"` with character compares for each of 640 pixels.
2. Fast paths per line by the layers that are on: only the ULA (the 128 menu, BASIC, many games): the output is
   the ULA pixel or the fallback, no merge. ULA + sprites, Layer 2 + sprites. Keep the general loop for blend
   modes 6 / 7 and the stencil.
3. Do not fill the arrays of layers that are off (`DrawLayers`), and compose at 320 grid pixels when no layer on
   the line is hi-res (each pixel twice), which halves the loop for most screens.

**Done 2026-10-13, steps 1 and 2** (`ComposeOrder<kRecord, kOrder>`: one instance per layer order with the order
folded in; a table for the 9-bit colour to RGBA; the layer functions report whether they may have painted, and a
line with nothing above the ULA takes `ComposeUlaOnly`). Exactness: `NextVideoRenderer_Test.
RandomStatesDigestIsUnchanged`, a digest of 864 random lines (every order, blend, stencil, with and without the
layer capture) recorded on the old compositor. Measured (interleaved A B B A x 2, minimum µs per frame):

| Row | before | after | x |
|:--|--:|--:|--:|
| Next_ShipNormal (128 menu) | 2,192 | 1,414 | 0.65 |
| Next_ShipTurbo | 538 | 505 | 0.94 |
| Next_Spectron_ShipNormal | 5,121 | 4,493 | 0.88 |
| Next_Copper_ShipNormal | 7,538 | 6,853 | 0.91 |
| Next_Show512_ShipNormal | 6,138 | 5,690 | 0.93 |
| Next_Tilemap_ShipNormal | 6,913 | 6,473 | 0.94 |
| Next_Scroll_ShipNormal | 8,147 | 7,449 | 0.91 |

Step 3 (compose at 320 grid pixels, skip filling rows of layers that are off) is not done.

- Gain: `ComposeLine` is 39 % of a 1x frame on the 128 menu and 12-18 % with demos; a 3-5x faster compositor takes
  most of that. The layer capture (`ComposeLine<true>`, on by default) must keep its ids and info for each pixel:
  the fast paths fill them too (or the record keeps the general loop).
- Effort: medium. Exactness: the existing Next render goldens plus a randomised test that composes every
  combination of NR #15 / #68 / #6B and layer contents with the old and the new loop.

### P5. Next: cheaper segments

`ScreenNext::RenderUpTo` copies the 256 next registers into `NextVideoInputs` for every segment and keeps five
640-wide scratch arrays on the stack (`___chkstk_darwin`). A copper demo renders a segment before every MOVE.

- Copy the registers only when they changed since the last copy (a write counter in `NextBoard`; the copper's
  MOVEs and the CPU's writes both go through it), and move the scratch to members.
- Gain: a few per cent on copper demos (the copper row: `NextCopper::Poll` 20 %, mostly this render). Effort:
  small.

### P6. TS-Conf: the screen on its events

The TS-Conf screen is drawn after every instruction. The Sprinter screen catches up only on events
(`ScreenSprinter::CatchesUpOnEvents`, `5640784ea`): a write that changes the picture, a port that changes the
video mode or the palette, and the frame tail. TS-Conf needs the same: a write watch on the displayed pages
(graphics, tiles and sprites go through the TSU at line starts already), the video registers, CRAM, the border.

- Gain: up to 25 % of a 1x idle frame and 14 % with sprites (`ScreenZX::UpdateScreen` share); in turbo 5-11 % (the
  one rendered frame in N). Already in the TS-Conf backlog
  ([implementation-plan.md](../2026-09-27-tsconf/implementation-plan.md), speed backlog).
- Effort: medium-large (the write watch). Exactness: the frame buffer of each frame equals the per-instruction
  render's, checked by the TS-Conf render goldens and a lockstep test.

### P7. TS-Conf: DRAM arbitration and the raster per wait tick

With the TSU active, every CPU memory read goes through `TsConfMemory::DramWait` -> `TsConfArbiter::CpuAccess`
-> `Step` (20-37 %), and each wait tick calls `TsConfInterrupts::OnWait` -> `CatchUp` (5-12 %).

- `TsConfInterrupts`: keep the raster of its next event (line interrupt, frame interrupt, DMA end) and return from
  `CatchUp` while the raster is before it, the same "next event" gate as P3.
- The arbiter: the free DRAM slots of a line are fixed once the line's TSU and DMA load is known (the engine
  computes the TSU cost at line start). A per-line slot map answers "the next free slot after t" with a lookup
  instead of stepping the arbiter.
- Gain: up to a third of a TS-Conf frame with sprites in turbo. Effort: medium. Exactness: the CPU wait states and
  the DMA / TSU budget per line must be identical: the arbiter's own tests plus a lockstep run of a TSU demo.

### P8. TS-Conf: a halted CPU in one go

`Z80::HaltedM1` re-executes the halted M1 every 4 T with the engine around it (2-6 %; Wild Commander waits in
HALT). The Sprinter runs these cycles in one go (`Z84C15Engine::RunIdleCycles`, `39417bffc`, DNTBLINK 2.3x). For
TS-Conf the run ends at the next event: the engine's line start / `ts_start`, an interrupt, a DMA step.

- Gain: 2-6 % idle, more for programs that HALT a lot. Effort: medium (the engine events of TSU-6 must be visited in
  order).

### P9. TSFM / AY: no per-step call when the step does nothing

- **TSFM with its sync deferred** (turbo, sound off): `handleStep` only notes `nowT()`, 3-5 % of a TS-Conf turbo
  frame (`SoundChip_TurboSoundFM::handleStep` self + `AudioTstate`). The noted position is the CPU's position at
  the end of the last instruction, which `catchUp()` can read from the CPU itself; a catch-up inside an
  instruction (a port access) is followed by `syncTo(nowT())` at once, and the TSFM sync is split-invariant (the
  property the deferral already relies on). So `SoundManager` can skip the call while the device defers (one bool
  per frame).
- **The legacy AY with synthesis suppressed** (the Sprinter's two AYs in turbo): `handleStep` still computes
  `nowT()` for `_lastSeenT` (3 % of a Sprinter turbo frame). `_lastSeenT` is read at the frame rebase only; it can
  be taken there.
- Gain: 3-5 % in turbo on all three machines (and the Pentagon). Effort: small.

### P10. AY at 1x: render between register writes

The AY renders per CPU step at 1x (`SoundChip_TurboSound::handleStep` -> `updateState`): 12 % of a Sprinter 1x
frame, 9-15 % of the Sprinter demos with sound, and the same on every machine with an AY. The output between two
register writes depends only on the registers and the generator state, so rendering in one go at each register
write and at the frame end gives the same samples. This is the AY counterpart of the TSFM "lazy sync" in the
standby results ([results.md §5](../2026-10-12-peripheral-standby/results.md#5-not-done)).

- Gain: up to 12 % at 1x on the Sprinter, similar on any AY machine. Effort: medium (the HQ decimator path and the
  native tap must see the same sample stream). Exactness: the audio goldens (`SoundChip_*Golden*`) and a
  byte-for-byte comparison of the rendered buffers.

### P11. Sprinter: the text renderer

`SymbolUnit` is 13 % of a 1x frame on a text screen (the BIOS, Flex Navigator, the DSS shell). The graphics modes
got the segment renderer (`1c8c7e404`, 2.1x); text mode still draws one character cell at a time. A span version
(one font row of a run of cells, the attribute decode hoisted) is the same kind of change.

- Gain: up to ~10 % at 1x on text screens. Effort: small-medium. Exactness: the Sprinter render goldens.

### P12. Cross-cutting: the per-instruction dispatch

On every machine `Z80::StepInstructionWithWork` -> `RunInstructionStartHooks` -> machine step hook ->
`MainLoop::OnCPUStep` -> screen, BetaDisk, tape, sound is 5-14 % of a frame in itself (calls, null checks, the
analyzer gate). After P3, P6, P9 most of these calls do nothing. A per-frame list of the step clients that are
actually active (built at frame start, as the `stepWork` gate already does for the whole set) would make the empty
ones cost nothing. Gain: 3-8 %, measured after the others. Effort: medium.

### Not proposed

- The Next CPU memory model: the P2 PoC of the Next work found a paged bus would save 26 % of the CPU part, about
  0.5 % of a 28 MHz frame, and it was deferred. That stands.
- Dropping the Sprinter's second AY: judged not worth the change in the output (§8.11 of the Sprinter design).
- SIMD in the TS-Conf gathers: after P6 the render is a smaller share; measure again first.

## 6. Suggested order

| # | Proposal | Machines | Gain (measured / bound) | Effort |
|--:|:--|:--|:--|:--|
| 1 | P1 TSFM check order | TS-Conf, Pentagon, every TSFM at 1x | **-26 % / -21 % / -18 % (measured)** | one line |
| 2 | P2 Next poller braces | Next | correctness | one line |
| 3 | P4 Next compositor | Next | up to 39 % (idle), 12-18 % (demos) | medium |
| 4 | P3 Next interrupt poll gate | Next | up to 15-20 %, the largest in turbo | medium |
| 5 | P9 no per-step call while deferred / suppressed | all | 3-5 % turbo | small |
| 6 | P1b TSFM idle cache | TSFM machines | a few % after P1 | small |
| 7 | P7 TS-Conf arbiter + raster gate | TS-Conf with TSU | up to ~30 % turbo sprites | medium |
| 8 | P6 TS-Conf screen on events | TS-Conf | up to 25 % at 1x | medium-large |
| 9 | P10 AY between writes | Sprinter, all AY machines at 1x | up to 12 % at 1x | medium |
| 10 | P5, P8, P11, P12 | | a few % each | small-medium |

Every change goes through the A/B procedure
([performance-guidelines.md](../../guidelines/performance-guidelines.md)) and a lockstep exactness test, as in the
standby work.

## 7. Reproducing

```bash
tools/build/build.sh core-benchmarks
cd cmake-build-agent-release/bin
./core-benchmarks --benchmark_filter='BM_Prof_(TSConf|Sprinter|Next)_' 2>/dev/null
UNREAL_SPRINTER_HDD=.../sp_hdd_sys.img ./core-benchmarks --benchmark_filter=BM_SprinterDemo 2>/dev/null
```

The Next demo rows are local: add a `FRAME_PROFILE_WORKLOAD` row with the absolute path of the `.nex`. Profiling
a row without its boot frames: the profiles README, section 5, "Turbo rows: the boot frames are in the profile".
