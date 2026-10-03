# Phase 1 results: memory that costs only what changes

Part of the [TTD v1 → v2 migration](README.md). Phase 1 design: [phase-1-memory-regions-tdd.md](phase-1-memory-regions-tdd.md). Quality bar: [engine decision 33](engine-decisions.md#g-quality-bar-after-every-phase).

Measured 2026-10-02 on branch `ttd-engine`, commit `d7c609051`, Release, on the shared development host.

## Summary

| Check | Result |
|---|---|
| Every checkpoint restores as v1's | Yes. The v1 → engine oracle (`TTDV1Feeder_Test`) feeds all 9 recorded v1 sessions of the corpus into the engine and compares every checkpoint: position, CPU, chipset, device state, RAM byte for byte, General Sound RAM. The shadow-mode tests do the same live on 10 machine models (Pentagon, Scorpion, ProfScorpion, Profi, ATM710, ATM450, ZX-Evo, TS-Conf, TS-Conf with VDAC2, Sprinter) and on NeoGS, MoonSound, General Sound, Sprinter, VDAC2 and the EEPROMs |
| Recorded bytes not larger than v1 | Yes, on all 46 configurations and workloads |
| Memory not larger than v1 | Yes, on all 46, for the parts both engines have |
| Counted capture work not larger than v1 | Yes, on all 46: 2× to 50× less |
| Seek time within PR-5 (p99 ≤ 5 ms) | Yes, even under heavy load: the worst memory restore p99 is 1.8 ms (Sprinter), measured at load 17–139 |
| Capture time p99 ≤ 3 × p50 (PR-3) | Not as a ratio on 25 of 46 configurations (v1: 39): the slow captures are frames with more changed content, the time follows the work (see [Timings](#timings)); p99 at most 280 µs after the first frame |

The check is a tool: `tools/verification/ttd-bench/ttd_engine_d33.py` (exit status 1 when a condition fails). The baseline is `testdata/ttd/bench/engine-phase1-full.json`.

## What is compared

All values are per recorded frame, over 600 frames, from the benchmark matrix run with both engines (`UNREAL_TTD_BENCH_ENGINE=all UNREAL_TTD_BENCH_SET=full`). The engine runs in shadow mode: it records the same frames as v1, from the same machine.

- **Bytes**: what a recording stores. v1: RAM pages, page references, device blobs. Engine: RAM pieces, reference records and tables, device blobs, device regions.
- **Memory**: the heap of the parts both engines have. v1 also holds the write journal, the port journals and the coverage index; the engine gets those in Phase 3, so they are compared then.
- **Counted work**: bytes walked, copied and compressed by one capture. v1 copies all of RAM into its delta base every frame (4 MB on ZX-Evo and TS-Conf); the engine copies only the pieces that changed.

### Exceptions applied (decision 33)

- **Memory v1 does not record at all**: NeoGS RAM and flash, MoonSound wave RAM, the ZX-Evo AVR and SMUC EEPROMs. Their bytes, version records and capture work are left out of the comparison. The shipped configurations fit NeoGS and MoonSound, so this applies on most machines (about 99 bytes per frame; 29–59% of the engine's version records).
- **Memory moving between streams**: v1 keeps the General Sound RAM, the Sprinter video and fast RAM and the VDAC2 memory inside device blobs, the engine as regions. Both sides are compared as sums.
- **Fixed amounts, printed apart**: the engine's delta base (the latest contents of every region, to compute differences: 0.1–10 MB, most of it NeoGS's 4.5 MB) and its arena slack (at most one chunk). Neither grows with the history. Dropping the delta base for mostly-zero regions is open in the [TODO](TODO.md).

## Results

Bytes, memory and work per frame; delta base and arena slack in bytes per frame of this 600-frame run (fixed amounts divided by 600).

| Configuration / workload | Bytes v1 → engine | Memory v1 → engine | Work v1 → engine | Delta base | Arena slack |
|---|---|---|---|---|---|
| 128K/idle | 724 → 631 | 1,081 → 999 | 180,549 → 54,338 | 4,588 | 191 |
| 48K/idle | 690 → 653 | 1,115 → 1,028 | 171,292 → 78,743 | 4,533 | 163 |
| ATM3+ay/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM3+beta/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM3+covox/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM3+gs128/idle | 7,196 → 2,665 | 7,758 → 3,167 | 4,448,149 → 136,583 | 8,963 | 25 |
| ATM3+gs512/idle | 7,222 → 2,668 | 7,784 → 3,171 | 4,841,365 → 137,894 | 9,619 | 25 |
| ATM3+moon/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM3+mouse/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM3+noay/idle | 6,427 → 2,016 | 6,989 → 2,539 | 4,287,541 → 104,385 | 6,997 | 113 |
| ATM3+tsfm/idle | 7,135 → 2,819 | 7,696 → 3,503 | 4,339,545 → 163,918 | 13,114 | 363 |
| ATM3/idle | 7,096 → 2,781 | 7,658 → 3,465 | 4,338,518 → 162,891 | 13,114 | 363 |
| ATM450/idle | 1,678 → 1,114 | 2,035 → 1,479 | 585,269 → 67,644 | 6,991 | 167 |
| ATM710-turbo/idle | 2,458 → 1,467 | 2,883 → 1,931 | 1,131,954 → 92,348 | 7,864 | 79 |
| ATM710/idle | 2,452 → 1,458 | 2,877 → 1,919 | 1,129,524 → 89,850 | 7,864 | 86 |
| PENTAGON+ay/idle | 1,093 → 1,008 | 1,449 → 1,367 | 188,151 → 63,687 | 6,335 | 191 |
| PENTAGON+beta/disk-loading | 1,898 → 1,227 | 2,323 → 1,591 | 201,944 → 78,272 | 6,335 | 3 |
| PENTAGON+beta/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON+covox/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON+covox/music-covox | 2,094 → 1,182 | 2,519 → 1,632 | 216,294 → 89,393 | 6,335 | 47 |
| PENTAGON+gs128/idle | 1,228 → 927 | 1,585 → 1,267 | 298,809 → 38,406 | 2,185 | 71 |
| PENTAGON+gs512/gs-upload | 2,517 → 1,785 | 3,078 → 2,279 | 733,961 → 90,691 | 2,840 | 832 |
| PENTAGON+gs512/idle | 1,254 → 929 | 1,611 → 1,271 | 692,025 → 39,717 | 2,840 | 71 |
| PENTAGON+moon/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON+moon/moon-upload | 1,559 → 1,411 | 2,121 → 1,873 | 234,903 → 112,597 | 6,335 | 308 |
| PENTAGON+mouse/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON+noay/idle | 446 → 336 | 802 → 689 | 161,259 → 29,716 | 218 | 72 |
| PENTAGON+tsfm/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON+tsfm/music-tsfm | 2,851 → 1,721 | 3,276 → 2,171 | 216,498 → 91,058 | 6,335 | 303 |
| PENTAGON/demo | 3,246 → 1,595 | 3,807 → 2,056 | 226,185 → 100,855 | 6,335 | 118 |
| PENTAGON/demo-eyeache | 2,572 → 1,293 | 2,997 → 1,743 | 217,359 → 89,714 | 6,335 | 417 |
| PENTAGON/demo2 | 3,176 → 1,807 | 4,010 → 2,476 | 283,372 → 163,817 | 6,335 | 803 |
| PENTAGON/game | 3,191 → 2,438 | 3,753 → 3,104 | 250,714 → 135,944 | 6,335 | 175 |
| PENTAGON/idle | 1,131 → 1,045 | 1,487 → 1,405 | 189,178 → 64,714 | 6,335 | 191 |
| PENTAGON1024/idle | 2,028 → 1,052 | 2,385 → 1,419 | 1,106,682 → 67,773 | 7,864 | 191 |
| PENTAGON512/idle | 1,515 → 1,048 | 1,872 → 1,411 | 582,394 → 66,025 | 6,991 | 191 |
| PLUS2/idle | 725 → 633 | 1,082 → 1,001 | 180,549 → 54,338 | 4,588 | 190 |
| PLUS2A/idle | 747 → 645 | 1,103 → 1,013 | 183,748 → 54,471 | 4,588 | 192 |
| PLUS3/idle | 819 → 716 | 1,176 → 1,084 | 184,746 → 55,504 | 4,588 | 190 |
| PROFI/idle | 1,893 → 861 | 2,318 → 1,236 | 1,100,215 → 58,602 | 6,117 | 176 |
| PROFSCORP/idle | 1,431 → 1,153 | 1,788 → 1,516 | 317,484 → 61,696 | 6,560 | 184 |
| SCORPION-3.5MHz/idle | 1,387 → 1,148 | 1,744 → 1,510 | 317,033 → 61,313 | 6,560 | 189 |
| SCORPION/idle | 1,388 → 1,149 | 1,745 → 1,511 | 317,033 → 61,313 | 6,560 | 189 |
| SPRINTER/idle | 13,312 → 1,534 | 13,873 → 2,023 | 4,612,904 → 435,940 | 11,906 | 336 |
| TSCONF/idle | 6,008 → 1,964 | 6,433 → 2,438 | 4,260,758 → 86,770 | 13,114 | 188 |
| TSL-VDAC2/idle | 6,331 → 2,138 | 6,756 → 2,627 | 4,266,727 → 99,572 | 16,766 | 188 |

## Fixed during the check

The first run failed the memory condition on 17 configurations. Two engine costs:

- checkpoint records were kept in a `std::vector`, whose growth reserve held up to twice the records (490 against v1's 288 bytes per frame). They are a `std::deque` now;
- the piece arena allocated 1 MB chunks from the start. Chunks now grow from 64 KB, doubling, to 1 MB.

The benchmark's accounting was also made equal on both sides: v1's device-state serialization (the Sprinter's 320 KB of video and fast RAM read every frame) was not counted, and the engine's work on memory v1 does not record was not separated. New metrics: `bm2_work_device_state_bpf`, `bm2_work_scanned_v1_lacks_bpf`, `bm2_work_scanned_ram_bpf`, `bm3_device_regions_v1_lacks_bpf`, `bm3_versions_v1_lacks_share`.

## Timings

Timings depend on the host's load and are not judged by the tool. The run above was made at load 17–139 (other sessions building on the same host). Even there:

- engine capture p50 is 3.5–62 µs against v1's 47–846 µs;
- engine memory restore p99 is at most 1.8 ms (Sprinter), within PR-5's 5 ms;
- capture p99 is up to 26× p50 on some configurations, from the load spikes; PR-3 (p99 ≤ 3 × p50) is checked on an idle host, below.

### Idle host

Run 2026-10-02 at load 5.7–12.4 (one-minute average at the start and the end), 600 frames, 200 seeks per configuration. `*`: p99 above 3 × p50.

| Configuration / workload | Capture p50 v1 → engine, µs | Engine capture p99, µs | p99 / p50 | Memory restore p50 v1 → engine, µs | Engine restore p99, µs |
|---|---|---|---|---|---|
| 128K/idle | 63.5 → 3.5 | 11.5 | 3.2 * | 57 → 30 | 59 |
| 48K/idle | 48.5 → 6.4 | 16.8 | 2.6 | 74 → 55 | 110 |
| ATM3+ay/idle | 356.8 → 13.2 | 53.0 | 4.0 * | 780 → 131 | 230 |
| ATM3+beta/idle | 263.6 → 13.8 | 56.8 | 4.1 * | 758 → 131 | 232 |
| ATM3+covox/idle | 585.3 → 14.2 | 54.0 | 3.8 * | 777 → 131 | 230 |
| ATM3+gs128/idle | 580.7 → 13.5 | 53.0 | 3.9 * | 784 → 130 | 231 |
| ATM3+gs512/idle | 758.0 → 14.1 | 56.2 | 4.0 * | 784 → 130 | 228 |
| ATM3+moon/idle | 608.3 → 13.9 | 53.9 | 3.9 * | 780 → 130 | 229 |
| ATM3+mouse/idle | 402.8 → 13.6 | 52.8 | 3.9 * | 758 → 127 | 226 |
| ATM3+noay/idle | 218.5 → 10.4 | 41.3 | 4.0 * | 739 → 93 | 169 |
| ATM3+tsfm/idle | 550.7 → 13.8 | 56.2 | 4.1 * | 776 → 124 | 223 |
| ATM3/idle | 319.1 → 13.8 | 53.0 | 3.8 * | 745 → 127 | 235 |
| ATM450/idle | 63.2 → 4.2 | 13.3 | 3.1 * | 137 → 57 | 111 |
| ATM710-turbo/idle | 162.2 → 7.0 | 22.5 | 3.2 * | 227 → 83 | 248 |
| ATM710/idle | 194.5 → 6.9 | 25.2 | 3.6 * | 212 → 78 | 233 |
| PENTAGON+ay/idle | 149.7 → 3.8 | 9.5 | 2.5 | 59 → 30 | 58 |
| PENTAGON+beta/disk-loading | 102.0 → 5.6 | 77.3 | 13.9 * | 102 → 98 | 329 |
| PENTAGON+beta/idle | 147.6 → 4.0 | 11.9 | 3.0 | 59 → 30 | 58 |
| PENTAGON+covox/idle | 326.0 → 3.9 | 12.8 | 3.3 * | 60 → 29 | 56 |
| PENTAGON+covox/music-covox | 106.0 → 6.8 | 20.2 | 3.0 | 204 → 57 | 112 |
| PENTAGON+gs128/idle | 176.3 → 3.9 | 9.8 | 2.5 | 61 → 29 | 57 |
| PENTAGON+gs512/gs-upload | 424.3 → 18.6 | 64.5 | 3.5 * | 122 → 208 | 554 |
| PENTAGON+gs512/idle | 448.2 → 3.5 | 9.2 | 2.7 | 59 → 29 | 64 |
| PENTAGON+moon/idle | 317.0 → 4.0 | 11.4 | 2.8 | 60 → 30 | 66 |
| PENTAGON+moon/moon-upload | 176.7 → 17.1 | 78.5 | 4.6 * | 126 → 237 | 527 |
| PENTAGON+mouse/idle | 63.3 → 4.0 | 10.0 | 2.5 | 58 → 30 | 58 |
| PENTAGON+noay/idle | 33.5 → 3.2 | 9.3 | 2.9 | 56 → 28 | 56 |
| PENTAGON+tsfm/idle | 197.5 → 3.8 | 9.5 | 2.5 | 60 → 29 | 57 |
| PENTAGON+tsfm/music-tsfm | 144.2 → 8.3 | 44.1 | 5.3 * | 219 → 76 | 147 |
| PENTAGON/demo | 205.5 → 7.5 | 62.5 | 8.3 * | 271 → 246 | 386 |
| PENTAGON/demo-eyeache | 141.1 → 6.6 | 26.5 | 4.0 * | 223 → 67 | 142 |
| PENTAGON/demo2 | 381.0 → 38.4 | 85.2 | 2.2 | 317 → 186 | 433 |
| PENTAGON/game | 424.2 → 60.2 | 74.2 | 1.2 | 321 → 335 | 573 |
| PENTAGON/idle | 95.6 → 3.8 | 10.2 | 2.7 | 56 → 30 | 58 |
| PENTAGON1024/idle | 113.4 → 4.2 | 11.0 | 2.6 | 180 → 30 | 58 |
| PENTAGON512/idle | 80.2 → 4.2 | 17.5 | 4.2 * | 107 → 30 | 58 |
| PLUS2/idle | 67.1 → 4.0 | 11.2 | 2.8 | 56 → 30 | 57 |
| PLUS2A/idle | 75.1 → 3.8 | 10.0 | 2.6 | 78 → 28 | 55 |
| PLUS3/idle | 68.4 → 3.5 | 10.0 | 2.8 | 80 → 34 | 68 |
| PROFI/idle | 199.4 → 3.9 | 31.4 | 8.0 * | 194 → 39 | 246 |
| PROFSCORP/idle | 151.6 → 4.6 | 13.6 | 2.9 | 90 → 34 | 59 |
| SCORPION-3.5MHz/idle | 111.6 → 5.0 | 13.7 | 2.7 | 85 → 34 | 59 |
| SCORPION/idle | 209.7 → 4.3 | 12.7 | 3.0 | 85 → 34 | 59 |
| SPRINTER/idle | 574.5 → 16.9 | 271.6 | 16.1 * | 750 → 65 | 1775 |
| TSCONF/idle | 201.5 → 5.2 | 20.2 | 3.9 * | 589 → 36 | 87 |
| TSL-VDAC2/idle | 728.8 → 5.6 | 16.6 | 2.9 | 593 → 54 | 82 |

**PR-5 holds**: the engine's memory restore p99 is at most 1.8 ms (Sprinter), within 5 ms on every configuration.

**PR-3, read as a ratio, does not hold on 25 of 46 configurations** (v1: 39 of 46). The slowest captures were looked at one by one (`UNREAL_TTD_BENCH_SLOW_FRAMES=N` prints them with their work): the time follows the work of the frame, not the engine:

- frame 0 is the first full snapshot of every region (up to 7.8 MB scanned, 150-170 compressions, 1.3-3 ms), one of the 600 frames and so inside p99;
- on the Sprinter the BIOS fills memory in frames 1-3 and 22-24 (500 KB changed, 130 compressions, 280-900 µs);
- on the ZX-Evo every 50th frame changes 64 KB (the clock in RAM);
- demos and disk loading have frames with 8-11 compressions against 1-2 in a quiet frame;
- one capture was host noise: Profi frame 447, 2.4 ms for a single compression.

With full reference tables switched off (snapshot interval 1,000,000) the p99 stays the same, so the periodic full table is not the cause. The engine's capture p99 is at most 280 µs after the first frame (1.4% of a 20 ms frame). Whether PR-3 should be judged per unit of work or against an absolute budget instead of the ratio is open.

## Migration v1 → engine

`TTDV1Feeder_Test.EngineRestoresEveryCheckpointAsV1` reads each v1 session of the corpus (`testdata/ttd/`, `testdata/machines/*/ttd/`: Pentagon idle, demo, TSFM music, Dizzy X and Green Beret loading with port journals, TS-Conf sprites, Sprinter boot), feeds it into the engine and compares every checkpoint. All 9 pass. Writing a v1 session as an engine file (decision 31) comes with the engine's file format, Phase 4.

## Open after Phase 1

- The delta base of mostly-zero regions (TODO).
- Media versions: written sectors, write-protect toggles, queued swaps (registry gaps 11–13; decision 25, Phase 3).
- PR-3: decide whether it is judged per unit of work or against an absolute budget (the ratio follows the workload).
