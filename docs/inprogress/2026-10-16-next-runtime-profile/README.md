# ZX Next: what a frame costs, by feature (runtime profile, 2026-10-16)

Question (owner): is there still room in the hot paths of the Next (screen modes, 3 AYs, DACs, DivMMC, ...), or is it near what it can be?
Answer in one line: **the machine is cheap where it is idle and the video is the biggest block; the places with some room are the tilemap and
ULA line renderers and the silent AY chips; the per-instruction device polling, first thought the best target, was tested and is not (see the table).**

## How

`core/benchmarks/emulator/machines/nextruntime_benchmark.cpp`: 30 scenes, each a booted `NEXT` with a small Z80 program (or the CPU parked) and one
feature on; the number is the median thread-CPU microseconds of a whole emulated frame (`MainLoop::RunFrame`, 40 rounds of 10 frames, 30 frames
of warm-up), plus its share of the 20 ms a 50 Hz frame has. Release build, Apple silicon.
`core-benchmarks --benchmark_filter='BM_NextRuntime/'` (needs `-DBENCHMARKS=ON`; use a separate build directory). Profiles: `sample <pid> 6`
on the process running one scene with `--benchmark_repetitions=80`.

## Frame cost (us per frame; share of 20 ms)

| scene | us | % of 20 ms | note |
|:--|--:|--:|:--|
| parked CPU, standard ULA, 3 AYs silent | 1169 | 5.8 | the floor: the frame itself |
| busy Z80 at 3.5 / 7 / 14 / 28 MHz | 1533 / 2169 / 3444 / 5198 | 7.7 / 10.8 / 17.2 / 26.0 | +364 / +1000 / +2275 / +4029 us over the floor |
| ULA hi-res / hi-colour / ULANext / ULA+ | 1442 / 1166 / 1385 / 986 | 7.2 / 5.8 / 6.9 / 4.9 | ULA+ is cheaper than the standard mode |
| LoRes / Radastan / ULA scrolled | 1222 / 1236 / 1160 | 6.1 / 6.2 / 5.8 | |
| Layer 2 256 / 320 / 640 | 1488 / 1532 / 1654 | 7.4 / 7.7 / 8.3 | +320 .. +485 |
| tilemap 40 / 80 columns | 2158 / 2348 | 10.8 / 11.7 | **+990 / +1180: the dearest layer** |
| sprites 64 / 128 | 1664 / 1758 | 8.3 / 8.8 | +495 / +590 |
| all layers (L2 320, tilemap, 128 sprites) | 2599 | 13.0 | |
| 3 AYs playing, synthesis on / off | 1230 / 1244 | 6.2 | the sound is within the noise of the floor |
| a DAC stream at 28 MHz | 4979 | 24.9 | the CPU's cost, the DAC adds nothing visible |
| DivMMC automap off / on (busy at 3.5 MHz) | 1512 / 1563 | 7.6 / 7.8 | the hook costs 3 % of a busy frame |
| zxnDMA, 6 KB continuous, repeated | 2063 | 10.3 | |
| copper, 200 waits / moves a frame | 1219 | 6.1 | |
| a game-like mix at 28 MHz (L2, tilemap, 64 sprites, 3 AYs, DAC-less) | 6767 | 33.8 | the CPU is 4.0 of the 6.8 ms |

Per Z80 T-state: 5.1 ns at 3.5 MHz, 7.1 ns at 28 MHz (the bus at speed carries more work per instruction).

## Where the time goes (main-thread samples)

**Idle frame (parked CPU)**: `UlaLine` 42 % (inclusive 29 % self), `ComposeLine` 9 %, **`SoundChip_AY8910::updateState` 10 %** (three chips stepped every
tick even when silent), `NextAudio::Tick` 2 % more, `Screen::LatchFramebuffer` 3 %, memset / memmove 7 %, `NextUart::Quiet` 4 %.

**Tilemap 40**: `TilemapLine` 34 % self, `UlaLine` 16 %, `ComposeLine` 15 %.

**CPU busy at 28 MHz** (inclusive; the `sample` attribution of the poller path overstates it, see the verdict table): the Z80N core `ExecuteStep` 38 % (`MemRead` 15 %), the machine's per-instruction hook `OnCPUStep` 23 %, of which
**`IsIntAsserted` 19 %** (it calls the device poller on every instruction: the CTC `Advance` 3.4 %, **`NextUart::Quiet` 9.4 %** - the ESP is always
fitted, so the UART never counts as idle - and `PollInterrupts`), `NextBoard::AfterInstruction` 5 % (`StepDma`), video `RenderUpTo` 19 %,
`AY updateState` 3 %.

## Verdict, item by item

| area | room? | size, risk |
|:--|:--|:--|
| per-instruction device polling (UART `Quiet`, CTC `Advance`, interrupt latch, DMA step) | **no (corrected 2026-10-16)** | the sampling profile put the poller path at ~20 % of the 28 MHz CPU cost, but the direct test says otherwise: with the poller body skipped altogether the 28 MHz scene was only 1.3-5.7 % faster (median ~3.5 %, ~0.2 ms a frame, 0.7 % of the 20 ms; a loaded host, load ~145). The CTC and the UART already have their quiet gates; what is left is ~8 ns an instruction. Not worth the risk to the exact timing of the UART / CTC |
| tilemap line renderer | yes | up to ~600 us a frame (3 % of 20 ms) from per-cell decoding instead of per-pixel; needs the A/B benchmark rule |
| ULA line renderer (every frame, every machine mode) | some | ~150-200 us a frame from cell-wise palette lookups; the idle floor of every Next program |
| AY chips when silent | some | up to ~100 us a frame by not stepping a chip whose channels and envelope are off; `SoundChip_AY8910` is shared with all Spectrum models, so a wider blast radius |
| sprites, Layer 2, copper, DMA | no | 0.3-0.6 ms for a full set; no profile peak of its own |
| 3 AYs, DACs (sound as a whole) | no | within 1-4 % of the floor |
| DivMMC automap hook | no | 3 % of a busy frame, 0.1 ms |
| Z80N core itself | not now | 38 % of the 28 MHz cost, already direct function tables; only a different CPU design (block caching) would move it |
| layer capture record | no | +3.5 %, tried at 320 and it was worse ([design-layer-capture.md](../2026-10-07-zx-next/design-layer-capture.md) section 10) |

In absolute terms nothing here is a problem on this machine: the worst measured frame (a 28 MHz game mix) is 34 % of the 50 Hz budget, a
typical one 10-15 %. The candidates matter for weak hosts, several instances at once (the video wall), turbo / fast-forward and the time-travel
recorder. They are listed, not planned: a change to a hot path needs its own A/B benchmark (`docs/guidelines/performance-guidelines.md`).

## Not measured / caveats

- `divmmc_busy_conmem` (the CPU inside the mapped window) came out below the floor: the program does not run there as written; the number is not valid.
- One host, one build; the numbers are for ranking, not for absolute claims. Thread CPU time, load on the machine was not controlled (median of 40 rounds).
- No real NEX program was profiled (the scenes are synthetic); NextZXOS idle is closest to the parked rows.

## Lesson

A sampling profile's inclusive share is not the saving: skipping the whole poller measured 3.5 % where the samples said ~20 %. Measure the removal before building the optimisation.
