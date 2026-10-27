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
- **Done 2026-10-13.** Measured again with the landed change (same procedure): TS-Conf 1x 8,498 -> 6,236 µs,
  TS-Conf sprites 1x 5,515 -> 4,370 µs, Pentagon 1x 949 -> 780 µs.

### P1b. TSFM: remember that the FM half is idle

After P1 the check still runs once per FM sample clock (~49 kHz at the default prescaler, x 2 chips). Idle stays idle until something changes
it: an FM register write, a CSM key-on from timer A, a state load or a reset. Cache the result per chip and drop
it on those events. The engine-level part of `idle()` that depends on the envelope counter's sub-count must stay
live (or be folded into the cached state).

- **Dropped after P1 (2026-10-13):** the check now runs ~2,000 times a frame instead of ~70,000 (TS-Conf); what is left
  is below the noise.
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

**Done by its author:** the braces are in `master` since the merge of 2026-10-13 (`SyncPiLine()` and `Advance`
both inside `if (!_uart.Idle())`).

### P3. Next: an interrupt poll that does nothing until something is due

**Done 2026-10-13** (the CTC and the UART step; `PollInterrupts` is unchanged):

- `NextCtc` keeps `_quietUntil`, the earliest zero count or ZC/TO pulse of its channels, worked out at the end of
  an `Advance` and cleared by everything that changes a channel (`Write`, `Trigger`, `Reset`, a state load).
  `Advance` returns at once below it.
- `NextUart::Quiet()`: no frame in a shifter, no byte queued, no reset, the lines steady, RTS as the peer was told,
  and each peer silent (`HasByte()` / `BreakHeld()` asked on every poll). The poll then calls `Look(now)`, which does
  what `Advance` does to such a UART: the clock and the channels' last look move to `now`. The state is the same
  byte for byte, so nothing is deferred and a TTD capture needs no flush.
- Tests: `NextCtc_Test.EventGateMatchesTheChannelWalkOnEveryAdvance` (60,000 steps of random programming against a
  CTC with the gate off: counts, zero counts, the order of interrupts and pulses) and
  `NextUart_Test.QuietLookMatchesAdvanceOnEveryPoll` (40,000 polls of random traffic, frame changes, flow control,
  breaks and a cut line against a UART that always advances).

| Row (P1 and P3 together; the Next rows are P3) | before | after | x |
|:--|--:|--:|--:|
| Next_ShipNormal | 1,425 | 1,357 | 0.95 |
| Next_ShipTurbo | 513 | 447 | 0.87 |
| Next_Spectron_ShipNormal | 4,545 | 4,175 | 0.92 |
| Next_Spectron_ShipTurbo | 3,227 | 2,916 | 0.90 |
| Next_Copper_ShipNormal | 6,910 | 6,481 | 0.94 |
| Next_Show512_ShipNormal | 5,770 | 5,297 | 0.92 |
| Next_Tilemap_ShipNormal | 6,538 | 6,022 | 0.92 |
| Next_Scroll_ShipNormal | 7,454 | 6,873 | 0.92 |

The original proposal:

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

**Done 2026-10-13** (`ScreenTSConf::CatchesUpOnEvents`, on by default; `SetCatchUpOnEvents(false)` gives the
per-instruction draw back for comparison). It turned out smaller than a page-level write watch, because the frame is
kept the same pixel for pixel instead of made more exact:

- The moments the picture already modelled exactly stay as they were: a video register, CRAM, the border and a DMA
  CRAM write flush the screen to the write's own T-state (`FlushVideo`, `SetVideoFlush`).
- Everything else took effect "from the end of the instruction before", simply because the screen was drawn after
  every instruction. Those inputs now draw up to exactly that place first: a CPU write to memory (a new
  `HostBusOverlay::onBeforeWrite`, called before the byte is stored when the overlay asks for it) and a DMA
  transfer (`TsConfEngine::SetVideoCatchUp`, before the DMA moves anything in a catch-up). The engine notes the
  raster tact each instruction ended at (`StepEndRaster`); it runs every step anyway.
- The frame's last step and a pause draw the rest (the existing `CatchesUpOnEvents` rule of `MainLoop`).
- Line latches and TSU rows are the engine's and are final for a line before its first dot.

Exactness: `TsConfScreenEvents_Test`, two machines per program, one in each mode, every frame compared byte for
byte: the sprite example, the slide show, the firmware boot, and a DMA copy into the shown page started at a moving
place in the frame (300 frames each by hand, 12-70 in the suite). Each of the two new catch-ups was switched off
once to see the test fail (the boot catches the memory one, the DMA program the DMA one). The event machine draws
~28x fewer spans.

| Row | before | after | x |
|:--|--:|--:|--:|
| TSConf_ShipNormal (Wild Commander) | 5,934 | 4,045 | 0.68 |
| TSConf_Sprites_ShipNormal | 4,041 | 3,374 | 0.84 |
| TSConf_ShipTurbo | 2,924 | 2,757 | 0.94 |
| TSConf_Sprites_ShipTurbo | 2,454 | 2,414 | 0.98 |

With P1, P7 and this, a TS-Conf 1x frame went 8.4 -> 4.0 ms in this cycle.

The original proposal:

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
- **Done 2026-10-13, the two cheap parts.** `TsConfInterrupts::OnWait`: when no frame pulse is latched and the frame
  event is not in the rasters the stall evaluates, one catch-up to the stall's end replaces the two (they latch the
  same line end; checked by running both and comparing on every call through the TS-Conf tests and the sprite
  demo). `TsConfArbiter::FetchAt`: the line's fetch schedule is kept while the line, V_CONFIG and G_X_OFFS stay the
  same. Measured: TS-Conf sprites turbo 2,768 -> 2,430 µs (-12 %), sprites 1x 4,334 -> 4,062 µs (-6 %), idle rows
  unchanged. The per-line slot map is not done: the arbiter's refused-cycle model is validated against the RTL
  simulation fclk by fclk, and what is left of it in the profile does not pay for a second model.
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
- **Checked 2026-10-13: not as small as written.** Both devices use the noted position across the frame boundary,
  after the CPU's `t` is rebased: the AY's frame start takes `_lastSeenT - now` as the rebase delta and its TTD
  capture uses `_lastSeenT`; the TSFM's frame end catches up to `_deferredT`, "the frame end the per-step sync
  reached". Reading the CPU at that moment gives the rebased value. It needs a hook before the frame rebase that
  notes the position once. Left for after the larger items.

### P10. AY at 1x: render between register writes

The AY renders per CPU step at 1x (`SoundChip_TurboSound::handleStep` -> `updateState`): 12 % of a Sprinter 1x
frame, 9-15 % of the Sprinter demos with sound, and the same on every machine with an AY. The output between two
register writes depends only on the registers and the generator state, so rendering in one go at each register
write and at the frame end gives the same samples. This is the AY counterpart of the TSFM "lazy sync" in the
standby results ([results.md §5](../2026-10-12-peripheral-standby/results.md#5-not-done)).

- **Checked 2026-10-13: dropped as written.** By source line, the AY's time on the Sprinter is the synthesis itself
  (the generator ticks and the decimator, inlined into `handleStep`); the per-step overhead around it is ~2 % (`nowT`,
  `AudioTstate`). Rendering later would move the work, not remove it. What would remove it is a closed-form advance of
  a silent chip (tone counters, the noise LFSR, the envelope), as the standby work did for the cards.
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

### Second round (2026-10-13, from profiles taken after P1-P7)

**R1. Next: the ULA layer line.** `UlaLine` + `UlaFetchAt` were 9-15 % of a Next frame: the fetch was worked out for
each of the 512 sub-pixels of a row from the registers, the palette entry looked up for each, and the border pixel
computed 640 times a line. Now the row's part of the fetch is worked out once (`UlaRowFetch`, which `UlaFetchAt` is
made of), the palette is asked when the fetch changes, and the border pixel is computed once and filled. The
compositor digest (`RandomStatesDigestIsUnchanged`) is unchanged.

**R2. Next: the UART interrupt levels.** `NextUart::PollInterrupts` returns at once when nothing it reads changed
since its last look (`_pollClean`, cleared by `Step`, `Read`, `Write`, the resets and a state load) and it is asked
with the same enables.

| Row (R1 + R2) | before | after | x |
|:--|--:|--:|--:|
| Next_ShipNormal | 1,345 | 1,084 | 0.81 |
| Next_Spectron_ShipNormal | 4,129 | 3,806 | 0.92 |
| Next_Spectron_ShipTurbo | 2,894 | 2,759 | 0.95 |
| Next_Copper_ShipNormal | 6,379 | 5,820 | 0.91 |
| Next_Show512_ShipNormal | 5,263 | 4,860 | 0.92 |
| Next_Tilemap_ShipNormal | 6,005 | 5,450 | 0.91 |
| Next_Scroll_ShipNormal | 6,823 | 6,473 | 0.95 |

**R3. TSFM with audio on: the sync waits for a sample.** After P1 and P6 the TSFM's per-instruction `syncTo` was the
largest item of a TS-Conf 1x frame again (25 %: `advanceChip`, the timer interface, the call itself). A step that
completes no output sample renders nothing, so it now only moves the sample phase and notes the position; the chips
catch up when a sample is due (882 times a frame instead of ~70,000), at a port access, at the frame end, at a
pause and before a TTD capture (`TTDSaveState` catches up first). This is the "lazy sync" left open by the standby
work. Two things it needed:

- `Ym2203Pair::syncTo` takes the host position without advancing on its first call after a reset. A deferred sync
  must not be that first call, or the chips start from a later moment (found by `TsfmPlayer_Test.
  FullCoreHashSameInSoundOffUnderTtd`: the timers stood 1087 clocks apart). No sync is deferred while
  `adoptsCpuClock()`; the turbo deferral of the standby work had the same hole and has the same guard now.
- `setLazySync(false)` gives the per-instruction sync back.

Exactness: the TSFM goldens (digests of the audio and of the core's trajectory) and the full-core-hash comparison
between a normal session and a per-step one under TTD, unchanged.

| Row (R3) | before | after | x |
|:--|--:|--:|--:|
| TSConf_ShipNormal | 4,021 | 3,347 | 0.83 |
| TSConf_Sprites_ShipNormal | 3,393 | 3,048 | 0.90 |
| Pentagon_ShipNormal | 778 | 723 | 0.93 |
| Pentagon_WorstNormal (TTD recording) | 890 | 844 | 0.95 |

### Third round (2026-10-13)

**R4. Sprinter: the text unit.** `SymbolUnit` drew each pixel of a character cell through the font-latch test and
a palette lookup. A unit the latch does not touch now takes its two colours once and picks by the font bit.
`BM_SprinterRender_Logo` 273 -> 205 µs (-25 %), Sprinter_ShipNormal (a text screen) 1,855 -> 1,672 µs (-10 %); the
Sprinter render goldens are unchanged.

**Tried and taken back (no measurable gain, interleaved A/B):**

- Next: `NextUart::Quiet()` with its own part kept between changes, the CTC's quiet test inline, an early exit of
  the DivMMC automap test for fetches at #3E00 and above. `Quiet` is 5-8 % of a Next frame by the profile, but that
  is its two virtual calls into the peer (`HasByte`, `BreakHeld`) per instruction, which the cache does not remove.
  Removing them needs the peers to tell the UART when they have something, and the ESP model's answer depends on
  time (`Now() >= _outReadyAt`), so it is not a small change.
- Next: the tilemap line working out a 40-column pixel once for its two sub-pixels.
- TSFM: reading the clock once in `handleStep`.

**Measured, not switched on: link-time optimisation.** The build has no LTO, and the per-instruction paths cross
translation units (the Z80N / Z84 library calls of `Enter` / `Leave`, the device hooks). A build with
`-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` (clang, `-flto`), same sources, interleaved:

| Row | regular | LTO | x |
|:--|--:|--:|--:|
| Next_ShipTurbo | 424 | 379 | 0.89 |
| Next_Spectron_ShipTurbo | 2,822 | 2,503 | 0.89 |
| Next_Spectron_ShipNormal | 3,813 | 3,649 | 0.96 |
| Sprinter_ShipTurbo | 1,175 | 1,060 | 0.90 |
| Sprinter_ShipNormal | 1,672 | 1,589 | 0.95 |
| TSConf_ShipNormal | 3,345 | 3,144 | 0.94 |
| TSConf_Sprites_ShipTurbo | 2,438 | 2,317 | 0.95 |
| Pentagon_ShipTurbo | 154 | 147 | 0.95 |
| Pentagon_ShipNormal | 731 | 738 | 1.01 |

**Switched on 2026-10-14** (`cmake/Lto.cmake`, `ENABLE_LTO`, Release only, not in sanitizer builds, off by default on
MinGW), after a check on every toolchain:

| Toolchain | Build | Warnings | core-tests |
|:--|:--|:--|:--|
| clang, macOS (the whole tree) | yes | none | all pass (9,407), asm and Qt tests too, MCP live suite 17 / 17; frames equal the regular build's |
| gcc 13.3, Linux (Docker, the whole tree) | yes | none | all pass (9,371) |
| MSVC 2019 (`/GL`, `/LTCG`) | yes | none | all pass (9,385) |
| MinGW gcc 15.2 with LTO | yes | none | all pass (9,384), but the link takes over 40 minutes: off by default there |
| MinGW gcc 15.2 without LTO (the default) | yes | none | all pass (9,384) |

What the check found and what was fixed on the way (gcc reports some of these only across units):

- `SoundManager` had two layouts (`-Wodr`): `_moonsound` sat under `UNREALNG_HAVE_OPL4`, which recording and the
  automation libraries do not define; they saw every later member 8 bytes off. The field is unconditional now.
- `unrealasm::ir::Op` had two sets of names (`-Wodr`, MinGW): winnt.h defines `RotateLeft16` / `RotateRight16` as
  macros, so files that had windows.h before `ir.h` saw `_rotl16`. The enumerators are `CyclicLeft16` /
  `CyclicRight16` now.
- MinGW with LTO rejected the implicit destructors of `Ne2000Board` and `EvoAvr` (two bases each: "multiple
  definition of non-virtual thunk"). They are defined in their .cpp files now.
- gcc repeats flow-based warnings over the whole program at link time and reports false ones there
  (`-Wstringop-overflow` in sam2695's state archive and inside `std::string`, `-Wfree-nonheap-object` in the tests'
  counting `operator delete`, `-Wattributes` for visibility on PE): switched off for the LTO link only.
- Not LTO, but found while checking MinGW: the Release `core-tests.exe` was 2.6 GB with full `-g` and Windows
  refused to start it; MinGW Release now has line tables only (`-g1`, 626 MB). The tests' static link forced
  `libiconv` with `--whole-archive` next to `libwinpthread`, two VERSION resources ("`.rsrc` merge failure"); it is
  linked plainly now. gcc 15's `-Wsubobject-linkage` for every `TEST_P` with a parameter type in an anonymous
  namespace (a googletest pattern) is off for the test binary.

**What the profiles show now** (after three rounds, the per-instruction cost is spread thin; no single item above
~8 % on TS-Conf or the Next):

- Pentagon and the other classic machines at 1x: `ScreenZX::DrawRangeZXImpl` is 16 % of the frame. The classic
  screen is still drawn after every instruction; the event rule that TS-Conf got (P6) fits it, with the same
  catch-up before a write to memory. It touches every classic machine and their screen variants.
- TS-Conf: a halted CPU step by step (5 % in Wild Commander, P8), the arbiter with the TSU active.
- Next: the peer calls of the UART poll (above), the sprite engine's fill (`FillClocks`, `UpdateAnchor`: 7 % in a
  sprite scroller).
- Everywhere: a silent AY / SSG still ticks its generators (the closed-form advance, P10's note).

### Regression check against the build before the cycle (2026-10-13)

Asked by the owner: no lost lines or pixels. Besides the per-change tests, the tree of this cycle was compared with
the commit before it (`4ad1b6d05`, built in its own worktree) frame by frame: a throwaway test ran whole machines
with the picture drawn along the beam (ScreenHQ on), RAM zeroed at power-on, and wrote for every frame a hash of the
frame buffer and the CPU's PC, SP, AF and T.

| Scenario | Frames | Distinct pictures | Picture differs | CPU state differs |
|:--|--:|--:|--:|--:|
| TS-Conf sprites (two starts) | 150 + 150 | 129 / 132 | 0 | 0 |
| TS-Conf slide show (two starts) | 200 + 400 | 5 / 8 | 0 | 0 |
| TS-Conf VDAC2 SDK tests 1 and 5 | 80 + 80 | 1 / 1 | 0 | 0 |
| Next Spectron 2084 (two starts) | 250 + 300 | 141 / 191 | 0 | 0 |
| Next copper wobble | 120 | 120 | 0 | 0 |
| Next ScrollNutter | 250 | 28 | 0 | 0 |
| Next tilemap, show512, Daybreak | 200 + 200 + 100 | 4 / 4 / 2 | 0 | 0 |
| Next boot (128 menu) | 100 | 14 | 0 | 0 |
| Pentagon boot | 400 | 13 | 0 | 0 |
| 48K, Scorpion, Profi boot | 120 each | 14-15 | 0 | 0 |
| ATM3 boot | 120 | 14 | 0 | 0 |
| TS-Conf boot | 150 | 29 | 0 up to frame 80 | 0 |
| Sprinter BIOS screen | 450 | 135 | 0 up to frame 337 | 0 |

The two rows with a limit show a clock: from that frame on a build differs from its own second run in the same
frames (the firmware prints the time of the host). For the Sprinter the frame buffers of both builds were compared
pixel by pixel instead: frame 330 (the text is on the screen, 4,454 lit pixels) has no differing pixel; frame 449
(11,184 lit pixels) differs in 31 pixels inside a 14 x 7 box, the seconds of the clock.

The LTO build was put through the same: all of `core-tests` passes in it (12 shards), and its frames equal the
regular build's in every scenario above that was run on it (the same Sprinter clock aside).

### The cycle on every platform (2026-10-14)

`4ad1b6d05` (before the cycle) against `09500dde7` (after it, LTO in its default per toolchain), the same
`frame_profile_benchmark.cpp` in both trees, interleaved A B B A x 2, minimum µs per frame. The old tree needed two
gcc `-Werror` fixes that master got later (the braces in the Next poller, parentheses in `unreal-asm`'s
`layout.cpp`) and, on Windows, the benchmark build fixes below; none of them is on a measured path.

| Row | macOS clang (LTO) | Linux gcc 14 (LTO) | Windows MSVC 2019 (LTO) | Windows MinGW gcc 15 (no LTO) |
|:--|--:|--:|--:|--:|
| TSConf_ShipNormal | 8,281 -> 3,113 (0.38) | 8,389 -> 3,692 (0.44) | 13,750 -> 5,522 (0.40) | 11,579 -> 5,397 (0.47) |
| TSConf_Sprites_ShipNormal | 5,384 -> 2,868 (0.53) | 5,985 -> 3,356 (0.56) | 10,000 -> 5,227 (0.52) | 8,044 -> 4,855 (0.60) |
| TSConf_ShipTurbo | 2,870 -> 2,516 (0.88) | 2,892 -> 2,802 (0.97) | 5,515 -> 4,121 (0.75) | 4,330 -> 4,283 (0.99) |
| TSConf_Sprites_ShipTurbo | 2,758 -> 2,287 (0.83) | 3,050 -> 2,455 (0.81) | 5,445 -> 4,094 (0.75) | 4,167 -> 3,797 (0.91) |
| Next_ShipNormal | 2,124 -> 1,024 (0.48) | 2,887 -> 1,604 (0.56) | 4,599 -> 2,064 (0.45) | 3,293 -> 1,820 (0.55) |
| Next_ShipTurbo | 526 -> 372 (0.71) | 600 -> 410 (0.68) | 876 -> 599 (0.68) | 745 -> 587 (0.79) |
| Sprinter_ShipNormal | 1,709 -> 1,522 (0.89) | 1,843 -> 1,604 (0.87) | 3,812 -> 2,726 (0.72) | 2,616 -> 2,546 (0.97) |
| Sprinter_ShipTurbo | 1,153 -> 1,048 (0.91) | 1,114 -> 973 (0.87) | 2,744 -> 1,676 (0.61) | 1,660 -> 1,636 (0.99) |
| Pentagon_ShipNormal | 920 -> 716 (0.78) | 1,045 -> 854 (0.82) | 2,277 -> 1,200 (0.53) | 1,412 -> 1,111 (0.79) |
| Pentagon_ShipTurbo | 154 -> 143 (0.93) | 156 -> 160 (1.03) | 1,013 -> 273 (0.27) | 243 -> 232 (0.95) |

macOS only (the files are local or need the Sprinter HDD image):

| Row | before | after | x |
|:--|--:|--:|--:|
| Next_Spectron_ShipNormal / ShipTurbo | 4,940 / 3,290 | 3,533 / 2,496 | 0.72 / 0.76 |
| Next_Copper / Show512 / Tilemap / Scroll (1x) | 7,441 / 5,985 / 6,870 / 7,902 | 5,463 / 4,548 / 5,234 / 5,945 | 0.73-0.76 |
| SprinterDemo DontBlink / Rotozoom / Plasma2 / BadApple (shipped) | 1,406 / 2,635 / 774 / 1,105 | 1,196 / 2,390 / 732 / 1,024 | 0.85-0.95 |

Machines: macOS on the Apple Silicon dev Mac; Linux in the CI image under Docker on the same Mac (arm64); Windows on
the i7-8700K host (12 threads). Read across: the code changes give the same picture everywhere (TS-Conf and the Next
at 1x about twice as fast, the Next in turbo -20..-32 %); the toolchain adds on top. MSVC gains the most from LTO -
without `/GL` it inlines nothing across files, so its Pentagon turbo frame was 4x slower than clang's and is now
within 2x. MinGW, with LTO off by default, shows the code changes alone: the Sprinter and the turbo rows barely move
there, which is about what LTO is worth on them elsewhere.

Fixes the Windows benchmark build needed (it did not build on master): `memory_interface_microbenchmark.cpp` used
`std::aligned_alloc`, which the Windows C runtime does not have (`_aligned_malloc` there); `unreal-asm`'s deferred
benchmark target called `add_subdirectory` when the build had no Google Benchmark target (MinGW compiles it into
`core-benchmarks`), which CMake refuses during deferred execution.

### Where the cycle stands

| Row | start of the cycle | now | x |
|:--|--:|--:|--:|
| TSConf_ShipNormal | 8,297 | 3,347 | 0.40 |
| TSConf_Sprites_ShipNormal | 5,575 | 3,048 | 0.55 |
| TSConf_Sprites_ShipTurbo | 2,828 | 2,414 | 0.85 |
| Next_ShipNormal | 2,210 | 1,084 | 0.49 |
| Next_Spectron_ShipNormal | 5,104 | 3,806 | 0.75 |
| Next_Spectron_ShipTurbo | 3,397 | 2,759 | 0.81 |
| Next_Copper_ShipNormal | 7,698 | 5,820 | 0.76 |
| Next_Scroll_ShipNormal | 8,255 | 6,473 | 0.78 |
| Pentagon_ShipNormal | 955 | 723 | 0.76 |

(Different runs on a shared machine: the ratios are within a few per cent.)

### Fourth round (2026-10-16)

**R5. The classic screen on its events.** `ScreenZX` (every machine whose picture is the ZX renderer family: Pentagon,
48K, 128K, +2/+3, Scorpion, the ZX modes of Profi and ATM) is no longer drawn after every instruction. The rule
P6 gave TS-Conf, kept pixel for pixel:

- `MainLoop` tells the screen where each step ended (`Screen::NoteStepEnd`, one store) and draws only the frame's
  last step.
- Before a change lands in the middle of a step, the screen draws up to where that step began, as the per-step draw
  had it (`ScreenZX::CatchUpToStepStart`): a CPU write into RAM page 5 or 7 (`Memory::MemoryWriteFast` /
  `MemoryWriteDebug`, one pointer test where nothing watches), every port write and the 128K's read-clocked
  `#7FFD` latch (`Z80::out` / `inFromBus`).
- The border write draws to its own T as before, and its step is drawn at its end too: with a CPU multiplier the
  step's end can fall on the Pentagon's 1T border compensation, where the per-step draw redraws that T.
- A mode change in the middle of a frame (Pentagon 1024, ATM, Profi switch the family by a port) drops the rest of
  that frame to the per-step draw; the decision is taken again at the next frame start. Not on a ZX-Poly member.
  `SetCatchUpOnEvents(false)` gives the per-step draw back at once (comparisons, code that draws by hand).

Exactness: `ZxScreenEvents_Test`, two machines per scenario, one in each mode, every frame compared byte for byte -
the boots of six models, five border / multicolor demos, a beam racer (random bytes into pages 5 and 7, the border,
`#7FFD` screen flips) at x1, x2 and x4, the read-clocked latch at eight places in the frame, ULA snow, an ATM
flipping between its ZX and 16-colour modes mid-frame. Seven mutations (no write watch, no port catch-up, no latch
catch-up, no draw at the border step's end, no mode fallback, no step end at the frame start, no catch-up at all)
were each caught by at least one of them.

| Row | before | after | x |
|:--|--:|--:|--:|
| Pentagon_ShipNormal | 739 | 615 | 0.83 |
| Pentagon_WorstNormal | 832 | 721 | 0.87 |
| Pentagon_ShipTurbo / 128K_ShipTurbo | 147 / 177 | 147 / 175 | 1.00 / 0.99 |
| TSConf / Next / Sprinter (1x and turbo) | | | 0.99-1.01 |

The remaining classic-screen cost is the per-T loop itself (`DrawRangeZXImpl`); a cell-at-a-time loop for long spans
is what is left there.

**R6. The ZX range renderer a cell at a time** (`f5ac00cd3`): a range that covers a character cell's four T draws
its eight pixels at once; the T-state LUT carries each T's run, so a blank run is one step and a border run one fill
once the border latch holds the range's colour. Pentagon 1x 611 -> 573 (-6 %). `ZxScreenCells_Test`: a frame in one
range equals the frame drawn one T at a time, picture and plane B.

**R7. The TSFM's quiet ticks.** With R5 and R6 the classic screen was ~7 % of a Pentagon 1x frame and the TurboSound
FM ~53 % - in the BASIC idle, two silent chips rendered tick by tick (218.75 kHz: two SSG mixers and output filters,
four FM half-ticks with their coupling filters, six decimator feeds). `Ym2203Pair::quietTick` replaces `renderTick`
while the pair is quiet at rest: no SSG write due, both SSGs at amplitude 0 (volume 0, no envelope; the DAC gives 0
for amplitudes 0 and 1 on both chips) with their output filters at rest on 0, each FM output stage's coupling at rest
on the gated hold, every FM word due the level held, all six decimator histories zeros (HQ only; LQ is untouched).
Then the full tick would change no filter state and feed zeros everywhere: the quiet tick advances the generators
(their phase is heard when the sound comes back), consumes the words and moves the decimator positions. The output
filters settle to exactly 0 some seconds after a sound stops (the SSG's ~2 s, the FM coupling's ~8 s), so a playing
tune never takes the path.

| Row | before | after | x |
|:--|--:|--:|--:|
| Pentagon_ShipNormal | 570 | 476 | 0.84 |
| Pentagon_WorstNormal | 678 | 579 | 0.85 |
| TSConf_ShipNormal | 3,183 | 3,094 | 0.97 |
| turbo rows, Sprinter | | | 1.00 |

Exactness: `TsfmQuietTicks_Test` - two machines (CPU parked, the test writes the chips), one with the quiet ticks off,
the host sound compared sample for sample through silence, an SSG tone, noise and an envelope on chip 1, an FM note,
each followed by silence until the quiet ticks take over again, and a tone after that; HQ and LQ. Freezing the tone
counters in the quiet tick fails it at once; not advancing the decimator phase never reaches an output. The audio of
eleven snapshots (tech_support on the TSFM, 7th Reality, Eye Ache, Vibrations, Earshaver, Binary Love II, Crazy Love,
others), HQ and LQ, 300-500 frames each, hashed in a throwaway harness: the same bytes before and after. The TSFM
golden digests are unchanged. (An AY mixer cache - the sums kept while the three amplitudes stay - was exact too but
measured nothing and was dropped.)

**R8. Quiet runs, and the plain AY.** The quiet tick still tested its conditions and stepped every generator 218,750
times a second. The conditions cannot change between two queue events (an SSG write, an FM word), so they are now
tested once for the whole run up to the next output sample (`Ym2203Pair::renderQuiet`, `SoundChip_TurboSound::
renderQuiet`), and the generators move the run in one step (`SoundChip_AY8910::advanceSilent`: a tone or the noise
whose counter does not reach its period moves at once; the envelope - in the reset state period 1, a step every tick -
in closed form: the steps the run contains, applied a segment at a time). The decimators take the run's zeros in one
call, their phase by the same additions as one feed at a time. The plain AY device (the 128K, Scorpion, Sprinter, any
machine set to the two-AY TurboSound) got the same quiet runs.

| Row | before | after | x |
|:--|--:|--:|--:|
| Pentagon_ShipNormal | 479 | 440 | 0.92 |
| 128K_ShipNormal | 481 | 444 | 0.92 |
| Scorpion_ShipNormal | 767 | 731 | 0.95 |
| Sprinter_ShipNormal | 1,583 | 1,486 | 0.94 |
| TSConf_ShipNormal | 3,118 | 3,075 | 0.99 |

(128K_ShipNormal and Scorpion_ShipNormal are new rows; the shipped 128K has the TurboSound FM as well.)

Exactness: `SoundChip_AY8910_Test.SilentAdvanceMovesTheGeneratorsAsTickByTick` - every envelope shape, envelope
periods 0-300, tones (period 0 too) and noise, 400 runs of 1-40 ticks each, the whole generator state compared after
every run (moving the envelope's turn by one step fails it); `AyQuietTicks_Test` - the lockstep of R7 for one AY and
the two-AY TurboSound, ending on a channel switched to the envelope that ran through the silence unwritten (no R13
write), so the phases the generators kept are heard; freezing the tones or moving the envelope's test by one tick
fails it. The eleven snapshots' audio: the same bytes.

**R9. The rest of the silent TurboSound FM** (branch `perf/sound-idle`):

- The pair remembers that an FM half is idle (`Ym2203Chip::fmIdleKnown`): the idle test walks every operator and ran at
  every sync; `clockFmIdle` keeps an idle engine idle, so only a register write, a reset, a restore, a timer expiry
  (CSM) or a clock through `clockFmOnce` clears it. Pentagon / 128K 1x -7 %.
- The silent tones and the noise count their turns in closed form too (in the reset state they turn every tick: tone
  period 1, noise period 0), and the TSFM step reads the clock once. Pentagon / 128K 1x -6 %.
- The step gate: the lazy deferral of a step that completes no sample holds until a T the step works out
  (`_stepDeadline`); the steps before it note their T only and `settleSteps` books them as the deferral would, at the
  next full step or any other entry into the device (each starts with it). Pentagon / 128K -1.5 %, Scorpion -3 %,
  TSConf -3.6 %.

| Row | before R9 | after | x |
|:--|--:|--:|--:|
| Pentagon_ShipNormal | 448 | 379 | 0.85 |
| 128K_ShipNormal | 445 | 383 | 0.86 |
| Scorpion_ShipNormal | 737 | 659 | 0.89 |

Exactness: the TSFM golden digests and the MultiSound TFM trace (dropping the cache clear on a data write fails them),
`SilentAdvanceMovesTheGeneratorsAsTickByTick` with the noise at periods 0-31 (moving the first shift or a tone's parity
fails it), `TsfmStepGate_Test` (two machines, the gate closed in one, a program writing random registers of both chips
at random places, dense and with long silences, HQ / LQ / 2x: the host sound and the end state equal; not booking the
sample phase fails it); the eleven snapshots' audio is the same bytes after each step.

**R10. The Next's three AYs in quiet runs.** `NextAudio::Tick` (three `updateState`, the mix, the device's DC
filter, a pushed level per tick at 218.75 kHz) was ~15 % of an idle Next 1x frame. `NextAudio::QuietTicks` takes
`AdvanceTo` to its end in one run while the three chips are silentAtRest and the DC filter sits at its fixed point for
their silence (the next step would leave it bit for bit where it is): every tick then has one level - the DC filter's,
the DAC's and the Pi's - worked out by Tick's own arithmetic once and pushed for the run, and the generators advance
(`advanceSilent`). Every input that moves the level (the ports, the DAC, the configuration) advances the device first,
so nothing changes inside a run. Asked at the start of a run and every 256 ticks.

| Row | before | after | x |
|:--|--:|--:|--:|
| Next_ShipNormal | 1,048 | 899 | 0.86 |
| Next_ShipTurbo / Pentagon_ShipNormal | | | 1.00 |

Exactness: `NextAudioQuietTicks_Test` - two Next machines (CPU parked), the quiet runs off in one, through silences
long enough for them to take over and out again on each of the three chips (tone, noise, envelope, a pan change, the
DAC held off its middle), ending on an envelope that ran through the silences unwritten; dropping the DAC from the
quiet level or freezing the tones fails it. The audio of Spectron 2084, ScrollNutter (music), the copper demo and the
idle machine, with the eleven classic snapshots: the same bytes.

**R11. The cards at 1x, and the MultiSound's quiet runs.** New rows: one card in the ZX bus at 1x, the AY socket a
plain AY (`Pentagon_Only*_Normal`, `Pentagon_NoCards_Normal`). Idle Pentagon, median us: no card 300, GS 303, NeoGS 326,
MoonSound 334, MultiSound 626. The MultiSound's YM2203 pair renders per channel (`renderChannels`: six SSG and two FM
decimators, no coupling stage) and ticked every generator through silence. `Ym2203Pair::renderQuietChannels` takes the
ticks up to each output in one run while nothing sounds: no SSG write due, both SSGs silentAtRest, all eight decimator
histories zeros, each FM part fed 0 (muted, or holding 0 with every word due 0), no native tap.

| Row | before | after | x |
|:--|--:|--:|--:|
| Pentagon_OnlyMultiSound_Normal | 559 | 422 | 0.75 |
| Pentagon_NoCards_Normal / ShipNormal | | | 1.00 |

Exactness: `MultiSoundQuietTicks_Test` - two Pentagons with the card (CPU parked), the quiet runs off in one, through
silences and out (SSG tone, noise and envelope on both chips, an FM note alone after a silence), ending on an envelope
that ran through them unwritten, each stretch checked audible; freezing the tones or not testing the FM words due fails
it. The card's TFM / player traces (MultiSoundTrace_Test digests) unchanged; the card with allsources.sna, tech_support
and idle, with the classic and Next snapshots: the same audio bytes. What the card costs on top now: the mixer
(`MultiSoundMixer::Mix`), the SAM2695 resampler and the GS on the card.

**R12. The AY and TSFM devices sleep through silence.** Quiet at rest (both SSGs silentAtRest, the decimator
histories zeros, nothing queued, HQ, x1, no native tap; the TSFM also both FM halves idle, holding 0, the output
coupling at rest), every output is +0.0 until something enters the device. The step gate then holds to the next entry
(a port access, the frame start / end, a buffer or sample-phase accessor, TTD), and settleSteps takes the step at the
last noted T as handleStep would (`stepAt`); the outputs due go in one quiet run (`renderQuietOutputs`), up to the first
tick with an SSG write due (or, TSFM, the first half-tick with an FM word off the hold: a CSM key-on from timer A
sounds with no CPU write). The device is open again after the entry, so nothing is late. Asleep again once the filters
are back at exactly 0: about 2 s after an SSG sound, about 8 s after an FM one (the 1.3 Hz output coupling).

| Row | before | after | x |
|:--|--:|--:|--:|
| Pentagon_NoCards_Normal (plain AY) | 288 | 269 | 0.93 |
| Pentagon_OnlyMoonSound / NeoGS / MultiSound_Normal | | | 0.92-0.93 |
| Pentagon_ShipNormal (TSFM) | 436 | 326 | 0.75 |
| 128K_ShipNormal (TSFM) | 440 | 332 | 0.75 |
| Scorpion_ShipNormal (TSFM) | 754 | 621 | 0.82 |
| Sprinter_ShipNormal | | | 0.99-1.00 |

Exactness: `AyStepGate_Test` / `TsfmStepGate_Test.BeepsAfterSilencesMatchEveryStepTakenTheFullWay` - a CPU program
sounding after long silences (the TSFM one also a CSM expiry with FM muted and one audible, no write), against a
machine with no gate and no quiet runs: the host sound frame by frame and the end state equal, and the device slept.
Settling without the step, losing the decimator phases, or a run blind to FM words each fail them. The 29 snapshots'
audio: the same bytes.

**Tried and dropped: the Next UART's peer promise.** A peer (the ESP module, a wired device) told the UART until when
it stays silent (`QuietUntil` plus an activity counter), so the poll skipped the two virtual peer calls per
instruction. Exact (lockstep twins with and without the promise, every Next frame / CPU / RAM hash equal at 3.5 and
28 MHz), but the A/B showed nothing (Next rows 1.00-1.03): with LTO those calls had become cheap already. Not landed.

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
