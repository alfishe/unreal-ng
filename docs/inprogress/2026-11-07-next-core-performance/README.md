# ZX Next core: where the time goes on x86, and the plan (2026-11-07)

Question (owner): on the Steam Deck the Next runs Vradark's Revenge at 24-31 fps with a crackling sound, while the other
machines run at 50. Why is the Next several times slower, and what is the plan to fix it?

Answer in one line: **a 28 MHz Next executes eight times the instructions of a 3.5 MHz machine per frame, and after every
one of them the core runs about ten device hooks; on x86 a few of those hooks were integer divisions and redundant
recomputations that Apple silicon hid. Six of them are fixed on the branch (24-31 to 37-49 fps on the Deck); the rest is
listed here, ordered, with the structural fix last.**

This work runs on its own branch (`next-perf`, worktree `scratch/wt-next-perf`), apart from the VRR / frame presenter work.
The earlier Apple-silicon profile is [2026-10-16-next-runtime-profile](../2026-10-16-next-runtime-profile/README.md); this one
supersedes its verdict on the device polling for x86 (see section 3.8).

## 1. Why the Next costs more than the other machines

| factor | Pentagon / 128K | Next at 28 MHz (Vradark's Revenge) |
|:--|:--|:--|
| CPU clocks per frame | ~70 000 | ~560 000 (`speed_multiplier` 8) |
| instructions per frame | ~15-20 k | ~100-150 k |
| work after each instruction | screen catch-up, beta disk, tape, sound | the same, plus the device poll (CTC, UART, ESP), the interrupt source (ULA, line, CTC, hardware IM2), the DivMMC automap test, the Multiface / DivMMC M1 chain, the 28 MHz SRAM wait, `OnInstructionDone`, `NextBoard::AfterInstruction` |
| video | one ULA layer | ULA, Layer 2, tilemap, 128 sprites with the engine's per-line timing, the copper |

Every per-instruction cost is paid eight times more often than on a 3.5 MHz machine. On the Deck's Zen 2 (2.4-3.5 GHz) a
32-bit `div` costs tens of cycles, where Apple silicon pays a few; the Apple-silicon profile therefore understated every
division on the hot path.

## 2. How it was measured

- **Deck, live**: `perf record -t <emulation thread>` for 10 s on the running game (`/usr/bin/perf`; no frame pointers, so a
  flat profile). The emulation thread shows as `DrogonIoLoop` in `top -H`. Game: Vradark's Revenge from the NextZXOS
  Browser (SD card folder `testdata/machines/zxnext/card` plus the game), fullscreen, sound on.
- **Deck, cost per frame**: the emulation thread's `utime + stime` from `/proc/<pid>/task/<tid>/stat` against the emulated
  frame counter (`GET /api/v1/emulator/{id}/state/screen/digest`, field `frame`) over 8 s. More stable than fps, because the
  game's scenes differ.
- **x86 benchmark**: `core-benchmarks --benchmark_filter='BM_NextRuntime/cpu28_busy'` built in the CI image
  (`ghcr.io/alfishe/unreal-ng:qt6.9.3-ubuntu24.04`, gcc 14, Release, `-DBENCHMARKS=ON -DBUILD_QT_APPS=OFF
  -DSDL_UNIX_CONSOLE_BUILD=ON`) on the x86 build host, A and B builds run interleaved, 5 rounds.

## 3. The inefficiencies

Status: **fixed** = on the `next-perf` branch, not landed; **open** = not started. Shares are of the emulation thread's
samples on the Deck.

### 3.1 `NextInterruptSource::LineStartT` recomputed on every instruction (fixed)

`core/src/emulator/io/z80n/nextinterrupts.cpp:236` (with `CounterOrigin`, line 227). While the line interrupt is enabled
(the game enables it), `IsIntAsserted` (line 281) asks for the line's start T on every instruction; the answer takes six
divisions and modulos over the frame geometry. **16.6 %** of the thread.
Fix: the answer is kept with the inputs it was made from (`_timing` fields, `_lineControl & 1`, `_lineValue`,
`_counterOffset`) and compared field by field, so direct writes by the state codec are covered. After: **1.3 %**.

### 3.2 Base-T conversion by division (fixed)

`NextInterruptSource::BaseT` (`nextinterrupts.cpp:217`) and `EmulatorState::CpuToBaseT` (`core/src/emulator/platform.h:1266`,
used by `Screen::GetCurrentTstate` after every instruction) divide the CPU T-state by the speed multiplier. Fix: a shift for
the power-of-two multipliers (1, 2, 4, 8, 16), the division otherwise. Part of `IsIntAsserted`'s **10.0 %**.

### 3.3 `PortDecoder_Next::Now28` in double arithmetic (fixed)

`core/src/emulator/ports/models/portdecoder_next.cpp:1576` computed `Now() * 8.0`, a double division and multiply, in the
per-instruction device poll. **7.2 %**. Fix: integers and a shift for the power-of-two multipliers; the same value
(`floor((T + t / m) * 8) = 8T + floor(8t / m)`). After: 4.1-4.6 % (the call remains; see 3.8).

### 3.4 `ScreenNext::UpdateScreen` divides on every instruction (fixed)

`core/src/emulator/video/next/screennext.cpp:656` computed the finished line count `(now - first) / perLine` after every
instruction, then `RenderUpTo` returned without work almost every time. **7.3 %**. Fix: return at once while the beam has
not finished the line being drawn (a multiply against `_nextLine`); exactly the cases where `RenderUpTo` returned. After: **1.0-1.5 %**.

### 3.5 The contention table rebuilt on every CPU speed write (fixed)

`UlaContention::SetGateArray` / `SetWindowedWait` (`core/src/emulator/video/ulacontention.h:127`, `:136`) rebuilt the delay
table unconditionally; `PortDecoder_Next::UpdateContention` (`portdecoder_next.cpp:1909`) calls both, from every NR `#07`
write among others, so a program that writes the speed register often rebuilt the table twice each time. **2.5 %**. Fix:
rebuild only when the rule changes. After: gone from the profile.

### 3.6 `NextSprites::FillClocks` decodes all 128 sprites per line (fixed, Deck measurement pending)

`core/src/emulator/io/z80n/nextsprites.cpp:454`, called from `ScreenNext::PassEnd` (`screennext.cpp:122`) once per line and
whenever the sprite epoch moves: it decoded or resolved all 128 sprites (anchors, relative sprites) and ran
`ProcessTicksOf`, a loop up to the sprite's width, for every sprite on the line. About 40 000 decodes per frame for
attributes that change a few times per frame. **20.4 %** in the game scene (plus `Decode` 3.8 % and `FillUntil` 2.7 %, part of
it from here). Fix: per visible sprite the start line, the height and the process clocks are kept until the sprite epoch
moves (every attribute write and the state codec's `Touch` move it); the per-line loop only compares.

### 3.7 `NextDivMmc::Classify` on every opcode fetch (fixed partly, regression to fix)

`core/src/emulator/io/z80n/nextdivmmc.cpp:124`: before looking at the address, it evaluated `rom3`, which calls the
`isBasicRomPaged` `std::function` and two memory queries. **5.1 %**. The branch's change answers the addresses no automap
rule names first (all are below `0x4000`): 2.7 % in the game. But in `cpu28_busy` (automap off) the address list is now
tested before the cheap `AutomapEnabled` gate: B2 is 2.4 % slower than B1 there. To do: test `address >= 0x4000` first
(one compare, most game code), then the existing gates, then the list.

### 3.8 The device poll after every instruction: UART and ESP never idle (open)

The poller lambda (`portdecoder_next.cpp:167`), called through a `std::function` from `IsIntAsserted` on every instruction:
`Now28`, `NextCtc::Advance` (already gated by `_quietUntil`), then the UART. The ESP module is always fitted on UART 0,
so `NextUart::Idle` (`core/src/emulator/io/z80n/nextuart.h:140`) is never true; every instruction then runs
`SyncPiLine`, `NextUart::Quiet` (`nextuart.cpp:382`: two channels, virtual `HasByte` / `BreakHeld` calls to the ESP and the
Pi peer) and `NextUart::Look` (`:402`, three stores), then `PollInterrupts` (an out-of-line call that returns at once).
On the Deck: lambda **10.7 %** + `Now28` 4.6 % + `PollInterrupts` 2.5 % + `EspModule::HasByte` 1.5 %. The Apple-silicon
measurement of 2026-10-16 found 3.5 %; on x86 the indirect and virtual calls cost more.
The branch inlines `PollInterrupts`' fast path (after: lambda 5.0 %, `HasByte` 1.0 %).
Plan: **UART sleep**. The UART falls asleep when `Quiet()` holds and every wired peer has `Pending() == 0` (no byte queued,
including one not ready yet, such as the ESP's reply turnaround) and no break. Asleep, the poll stores only `now` (the
time a look would have had). It wakes, first applying that stored look so `lastLook` is what per-instruction looks would
have left, on: any UART port access or setter (`Read`, `Write`, `Reset`, `SetPeer`, `SetJoystickPeer`,
`SetJoystickRoute`, `SetLineConnected` with a change), the frame boundary after the peers' `OnFrame` (network data and
ESP Wi-Fi events land there), and `ISerialPeer::onReceive` (journaled network arrivals). A game that never touches the
UART then pays one store per instruction; NextZXOS, the ESP tools and network programs behave as now. Verify with the
UART, ESP, Serial and TTD replay tests.

### 3.9 The rest of the per-instruction chain (open)

Each is small; together they are a large share at 28 MHz:

| hook | share (Deck) | note |
|:--|--:|:--|
| `Z80::StepInstructionWithWork` | 8.6-10.6 % | the step loop around the Z80N core |
| `Z80NEngine::MemRead` + `NextMemory::SlotReadContendedFast` + `Z80nContendRT` | 6 + 3-5 + 2 % | at 28 MHz every access goes through the contended interface for the SRAM wait (`NextMemory::ModelMemoryInterface`, `_sramWait28`); a per-slot wait table folded into the plain read would drop one indirection |
| `MainLoop::OnCPUStep` | 3-5 % | null checks, beta disk and tape `handleStep` on a machine without a beta disk |
| `PortDecoder_Next::OnInstructionDone` | 2.3-3.3 % | |
| `NextBoard::AfterInstruction` | 1.5-2 % | |
| `M1Chain::BeforeMachineM1` / `OnMachineM1` | 2.2 + 0.6-2 % | the Multiface / DivMMC M1 hooks, also on fetches that cannot trigger |
| `NextInterruptSource::IsIntAsserted` | 4-6 % after 3.1-3.2 | the `std::function` poller call and the pulse logic |
| `SoundChip_AY8910::updateState` | 1.5-2 % | three AYs, mostly silent (see the 2026-10-16 profile) |

Plan: one pass over the chain: answer the common case first in each hook (no automap address, no Multiface, no beta disk),
call the poller directly instead of through `std::function`, and fold the hooks that only test flags into one gate per
instruction.

### 3.10 Video (open)

| item | share (Deck) | note |
|:--|--:|:--|
| `NextVideoRenderer::RenderLine` + `DrawLayers` | 6-7 % | the layer compositor, per line segment |
| `NextSprites::Decode` from `RunSpritePass` | 3.8 % | the drawing pass decodes the sprites again; can share the 3.6 cache |
| `NextSprites::FillUntil` | 2.7 % | |
| `refresh` in `ScreenNext::RenderUpTo` (`screennext.cpp:587`) | | copies all 256 NextREGs into the inputs for every segment; copy only what the renderer reads, or on a register epoch |
| ScreenHQ / SoundHQ | ~3 fps on the Deck | per-instruction screen catch-up and the high-quality audio path |

### 3.11 Structural: polling instead of scheduling (open)

All of 3.1-3.9 come from one shape: after every instruction the core asks every device whether something happened. The
devices know when their next event is (the CTC's next zero count, the ULA and line interrupt positions, the next UART
frame or peer byte, the end of the screen line being drawn, the copper's next wait). A scheduler that keeps the nearest
event time lets the CPU run instructions with no device calls until then, and the hooks run only at events. That removes
most of the per-instruction cost instead of trimming it, at the price of a careful change to the Next's timing model; it
needs the TTD replay and the Next timing tests (`NextRtlTiming`, the co-simulation) as the safety net.

## 4. Measurements so far

**Deck, Vradark's Revenge** (intro and first scenes; fps varies with the scene):

| build | fps, SoundHQ / ScreenHQ on | fps, off | emulation thread |
|:--|--:|--:|:--|
| master | 24-27 | 31 | ~99 % of a core |
| fixes 3.1-3.5 | 37-43 | 40-49 | 90 % |
| fixes 3.1-3.5, 3.7, 3.8 (inline) | 37-40 | 42-44 | 90-93 %, **21-24 ms CPU per frame** (budget 20 ms) |
| + 3.6 (sprites) | pending | pending | pending |

**x86 build host, `BM_NextRuntime/cpu28_busy`** (busy Z80 loop at 28 MHz, no line interrupt, no sprites, so 3.1 and 3.6
do not show here), µs per frame, 5 interleaved rounds:

| build | median | change |
|:--|--:|--:|
| A: master | ~6000 | |
| B1: fixes 3.1-3.5 | ~5500 | -8.4 % |
| B2: B1 + 3.7, 3.8 (inline) | ~5630 | -6.2 % (3.7's ordering, to fix) |

The Deck profile before the fixes (10 s, 16 329 samples): `LineStartT` 16.6, `IsIntAsserted` 10.0, `UpdateScreen` 7.3,
`Now28` 7.2, `OnCPUStep` 5.2, `StepInstructionWithWork` 4.2, poller lambda 3.6, `MemRead` 3.5, `SlotReadContendedFast` 3.3,
`OnInstructionDone` 3.2, `ExecuteStep` 3.2, `DrawLayers` 3.1, `RenderLine` 3.1, `ScreenNext::Decoder` 3.0, `RenderUpTo` 2.9,
`SlowStep` 2.7, `RebuildDelayLut` 2.5, `Classify` 1.9, AY 1.7 %.

## 5. Plan

Each phase: target build, the Next / ULA / DivMMC / UART / ESP / Serial / sprite tests, an interleaved A/B on the x86 host,
the Deck's CPU ms per frame on the game; land a phase when it is measured.

| phase | content | expected |
|:--|:--|:--|
| P0 | land 3.1-3.6 with 3.7's order fixed; add a game-like benchmark scene (line interrupt on, 32+ sprites, automap on) so 3.1 and 3.6 show in `BM_NextRuntime` | Deck 50 fps with SoundHQ / ScreenHQ off |
| P1 | UART sleep (3.8); the per-instruction chain pass (3.9): direct poller call, common case first in each hook, one flag gate | -10-15 % more |
| P2 | video: share the sprite decode with the drawing pass, the NextREG snapshot per segment, the compositor (3.10) | 50 fps with SoundHQ / ScreenHQ on |
| P3 | memory path at 28 MHz: SRAM wait folded into the plain read (3.9) | a few % |
| P4 | the event scheduler (3.11), design note first, behind the full timing suite | the per-instruction cost mostly gone |

Acceptance for the branch: Vradark's Revenge on the Deck at a steady 50 fps with SoundHQ and ScreenHQ on, the emulation
thread under 15 ms per frame (25 % headroom), no change in the Next timing, TTD replay and co-simulation tests.
