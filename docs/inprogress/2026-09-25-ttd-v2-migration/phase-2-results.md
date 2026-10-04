# Phase 2 results: device state that costs only what changes

Part of the [TTD v1 → v2 migration](README.md). Phase 2 design: [phase-2-device-state-tdd.md](phase-2-device-state-tdd.md). Quality bar: [engine decision 33](engine-decisions.md#g-quality-bar-after-every-phase). Phase 1: [phase-1-results.md](phase-1-results.md).

Measured 2026-10-03 on branch `ttd-engine` (the commit that adds this document), Release, on the shared development host.

## Summary

| Check | Result |
|---|---|
| Every checkpoint restores as v1's | Yes. The v1 → engine oracle (`TTDV1Feeder_Test`) feeds the 9 v1 sessions of the corpus into the engine and compares every checkpoint, device state included. The shadow-mode tests do the same live on 10 machine models, and with every card that runs its own clock fitted (TSFM, MoonSound, GS or NeoGS) |
| The engine restores every device as v1 does | Yes. On the 10 models v1 restores a checkpoint, the machine runs on, the engine restores the same checkpoint, and every device saves the same bytes (`EngineRestoresEveryDeviceAsV1`) |
| Recorded bytes not larger than v1 | Yes, on all 46 configurations and workloads |
| Memory not larger than v1 | Yes, on all 46 |
| Counted capture work not larger than v1 | Yes, on all 46 |
| Capture p99 ≤ 1 ms (PR-3), memory restore p99 ≤ 5 ms (PR-5) | Yes, on all 46, at load 7–15: capture p99 at most 434 µs (Profi), restore p99 at most 1.8 ms (Sprinter) |

The check is `tools/verification/ttd-bench/ttd_engine_d33.py`. The baseline is `testdata/ttd/bench/engine-phase2-full.json` (600 frames, both engines, no seeks: bytes, memory and counted work).

## What Phase 2 changed

| Step | What | Where it shows |
|---|---|---|
| 1. Device table | Each device describes itself (type, instance, layout version, size, firmware fingerprint, dependencies, time fields); the engine builds one table per session and restores in its order | `TTDDeviceDescriptor`, `TTDDeviceTable` |
| 2. Device history | A device's state is an engine region: stored only when it changes, as a difference from the previous version | device bytes 3–10× below v1 |
| 2. Time fields | Clocks declared by the device are stored as residuals from a line; restored exactly at any frame, forward or back | MoonSound, NeoGS, TSFM, ATM2 keyboard controller |
| 2. Changed ranges | A few changed bytes stored as (offset, length, bytes), uncompressed; memory pieces benefit as well | compressions per frame 2–8× fewer |
| 3. Restore result | Every device not restored exactly is an issue naming it: missing state, device not present, size, firmware, damage with its frames, sync after restore | `TTDRestoreResult`, `CheckSession` |
| 4. Contract and sync | Registration builds the engine's device table and refuses recording on a mismatch, naming the device; the cards that run their own clock are checked at every frame boundary | `CheckDeviceTable`, `TTDSyncedTime` |

Decided on the way: the device set is fixed for a session (D38): no device-set events; a device without state keeps its live state and is reported (no power-on reset per device; a machine reset stops the recording).

## Results

Bytes per frame (memory + references + devices), device bytes per frame, memory and counted work per frame; 600 frames. "Phase 1" is the engine before device history (`engine-phase1-full.json`).

| Configuration / workload | Bytes v1 → Phase 1 → Phase 2 | Device bytes v1 → Phase 1 → Phase 2 | Memory v1 → Phase 2 | Work v1 → Phase 2 |
|---|---|---|---|---|
| 128K/idle | 724 → 631 → 271 | 543 → 542 → 129 | 1,081 → 753 | 180,549 → 62,851 |
| 48K/idle | 690 → 653 → 294 | 528 → 527 → 137 | 1,115 → 778 | 171,292 → 83,249 |
| ATM3+ay/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM3+beta/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM3+covox/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM3+gs128/idle | 7,196 → 2,665 → 573 | 2,470 → 2,266 → 135 | 7,758 → 1,287 | 4,448,149 → 139,525 |
| ATM3+gs512/idle | 7,222 → 2,668 → 576 | 2,496 → 2,267 → 135 | 7,784 → 1,290 | 4,841,365 → 140,836 |
| ATM3+moon/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM3+mouse/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM3+noay/idle | 6,427 → 2,016 → 244 | 1,726 → 1,723 → 3 | 6,989 → 757 | 4,287,541 → 92,609 |
| ATM3+tsfm/idle | 7,135 → 2,819 → 636 | 2,408 → 2,405 → 182 | 7,696 → 1,314 | 4,339,545 → 168,069 |
| ATM3/idle | 7,096 → 2,781 → 608 | 2,370 → 2,366 → 154 | 7,658 → 1,287 | 4,338,518 → 165,922 |
| ATM450/idle | 1,678 → 1,114 → 360 | 991 → 989 → 141 | 2,035 → 822 | 585,269 → 81,530 |
| ATM710-turbo/idle | 2,458 → 1,467 → 552 | 1,240 → 1,238 → 222 | 2,883 → 1,281 | 1,131,954 → 106,739 |
| ATM710/idle | 2,452 → 1,458 → 539 | 1,240 → 1,238 → 214 | 2,877 → 1,269 | 1,129,524 → 104,329 |
| PENTAGON+ay/idle | 1,093 → 1,008 → 320 | 911 → 910 → 127 | 1,449 → 780 | 188,151 → 76,904 |
| PENTAGON+beta/disk-loading | 1,898 → 1,227 → 584 | 978 → 977 → 192 | 2,323 → 1,282 | 201,944 → 98,261 |
| PENTAGON+beta/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON+covox/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON+covox/music-covox | 2,094 → 1,182 → 481 | 934 → 932 → 150 | 2,519 → 1,157 | 216,294 → 100,431 |
| PENTAGON+gs128/idle | 1,228 → 927 → 320 | 1,047 → 845 → 143 | 1,585 → 811 | 298,809 → 52,708 |
| PENTAGON+gs512/gs-upload | 2,517 → 1,785 → 1,146 | 1,991 → 901 → 182 | 3,078 → 1,875 | 733,961 → 99,655 |
| PENTAGON+gs512/idle | 1,254 → 929 → 322 | 1,073 → 844 → 143 | 1,611 → 814 | 692,025 → 54,019 |
| PENTAGON+moon/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON+moon/moon-upload | 1,559 → 1,411 → 673 | 1,006 → 1,005 → 193 | 2,121 → 1,364 | 234,903 → 121,260 |
| PENTAGON+mouse/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON+noay/idle | 446 → 336 → 60 | 264 → 264 → 0 | 802 → 397 | 161,259 → 25,804 |
| PENTAGON+tsfm/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON+tsfm/music-tsfm | 2,851 → 1,721 → 864 | 1,292 → 1,290 → 340 | 3,276 → 1,540 | 216,498 → 110,480 |
| PENTAGON/demo | 3,246 → 1,595 → 879 | 972 → 970 → 181 | 3,807 → 1,557 | 226,185 → 109,934 |
| PENTAGON/demo-eyeache | 2,572 → 1,293 → 586 | 977 → 975 → 185 | 2,997 → 1,263 | 217,359 → 102,452 |
| PENTAGON/demo2 | 3,176 → 1,807 → 1,058 | 969 → 968 → 181 | 4,010 → 1,730 | 283,372 → 166,350 |
| PENTAGON/game | 3,191 → 2,438 → 1,726 | 979 → 978 → 187 | 3,753 → 2,404 | 250,714 → 147,010 |
| PENTAGON/idle | 1,131 → 1,045 → 342 | 949 → 948 → 149 | 1,487 → 802 | 189,178 → 79,078 |
| PENTAGON1024/idle | 2,028 → 1,052 → 349 | 949 → 948 → 149 | 2,385 → 812 | 1,106,682 → 82,136 |
| PENTAGON512/idle | 1,515 → 1,048 → 345 | 949 → 948 → 149 | 1,872 → 807 | 582,394 → 80,388 |
| PLUS2/idle | 725 → 633 → 272 | 543 → 542 → 129 | 1,082 → 755 | 180,549 → 62,851 |
| PLUS2A/idle | 747 → 645 → 268 | 558 → 557 → 127 | 1,103 → 751 | 183,748 → 62,998 |
| PLUS3/idle | 819 → 716 → 271 | 626 → 625 → 128 | 1,176 → 753 | 184,746 → 63,955 |
| PROFI/idle | 1,893 → 861 → 336 | 756 → 755 → 147 | 2,318 → 813 | 1,100,215 → 70,917 |
| PROFSCORP/idle | 1,431 → 1,153 → 394 | 1,048 → 1,047 → 183 | 1,788 → 856 | 317,484 → 78,271 |
| SCORPION-3.5MHz/idle | 1,387 → 1,148 → 385 | 1,048 → 1,046 → 179 | 1,744 → 847 | 317,033 → 77,868 |
| SCORPION/idle | 1,388 → 1,149 → 389 | 1,049 → 1,047 → 183 | 1,745 → 851 | 317,033 → 77,854 |
| SPRINTER/idle | 18,244 → 1,534 → 1,097 | 12,312 → 1,093 → 116 | 19,625 → 2,182 | 4,724,016 → 559,914 |
| TSCONF/idle | 6,008 → 1,964 → 465 | 1,839 → 1,836 → 197 | 6,433 → 1,197 | 4,260,758 → 106,929 |
| TSL-VDAC2/idle | 6,331 → 2,138 → 532 | 2,162 → 1,990 → 210 | 6,756 → 1,256 | 4,266,727 → 123,916 |

**Device bytes** fall from v1's whole blobs every frame to what changes: 48K 528 → 137, Pentagon idle 949 → 149, ZX-Evo 2,370 → 154, TS-Conf 1,839 → 198, Sprinter 12,312 → 116.

**An idle frame (PR-10, Q1).** With no device that runs its own code (`PENTAGON+noay/idle`) a frame costs 60 bytes, within the 64 B of PR-10. With cards fitted, what still changes every frame is state the devices really change, not clocks: the AY's and TSFM's noise generator (a pseudo-random shift register), TSFM's decimator phases (floating point), the NeoGS and keyboard-controller CPUs' RAM and registers. That is Q1's reading of PR-10: 64 B plus what devices running their own code change, reported per configuration above.

## Timings

Full matrix with seeks (`UNREAL_TTD_BENCH_ENGINE=all UNREAL_TTD_BENCH_SET=full`, 600 frames), load average 7–15 during the run. Capture p99 leaves the first frame out (PR-3). The engine's capture now includes the device work (laying out every device's state, time fields, differences); in Phase 1 the engine's capture had no device work at all, so its Phase 1 capture times are not comparable. Against v1, which always did that work, the engine is 3–20× faster at the median.

| Configuration / workload | Capture p50 µs v1 → engine | Capture p99 µs (engine) | Restore p50 µs v1 → engine | Restore p99 µs (engine) |
|---|---|---|---|---|
| 128K/idle | 52.5 → 9.8 | 24.6 | 57 → 6 | 16 |
| 48K/idle | 42.6 → 12.5 | 30.9 | 76 → 10 | 14 |
| ATM3+ay/idle | 424.2 → 22.6 | 77.5 | 772 → 28 | 34 |
| ATM3+beta/idle | 320.5 → 23.0 | 76.1 | 781 → 27 | 32 |
| ATM3+covox/idle | 555.0 → 22.5 | 75.5 | 781 → 27 | 31 |
| ATM3+gs128/idle | 312.8 → 20.9 | 72.2 | 756 → 27 | 34 |
| ATM3+gs512/idle | 597.5 → 20.8 | 72.8 | 744 → 26 | 31 |
| ATM3+moon/idle | 405.6 → 22.2 | 79.1 | 792 → 27 | 32 |
| ATM3+mouse/idle | 574.0 → 23.0 | 79.8 | 754 → 27 | 32 |
| ATM3+noay/idle | 210.3 → 11.8 | 41.6 | 708 → 19 | 22 |
| ATM3+tsfm/idle | 461.0 → 24.2 | 81.4 | 775 → 27 | 34 |
| ATM3/idle | 354.5 → 23.5 | 80.6 | 744 → 28 | 39 |
| ATM450/idle | 77.2 → 13.0 | 33.4 | 136 → 13 | 20 |
| ATM710-turbo/idle | 191.8 → 19.4 | 48.8 | 230 → 20 | 32 |
| ATM710/idle | 109.4 → 19.9 | 51.2 | 211 → 20 | 31 |
| PENTAGON+ay/idle | 272.0 → 12.5 | 30.8 | 57 → 7 | 9 |
| PENTAGON+beta/disk-loading | 83.7 → 17.4 | 107.5 | 103 → 31 | 78 |
| PENTAGON+beta/idle | 148.1 → 13.4 | 35.5 | 60 → 7 | 10 |
| PENTAGON+covox/idle | 127.3 → 13.0 | 33.4 | 60 → 7 | 14 |
| PENTAGON+covox/music-covox | 295.4 → 16.1 | 48.3 | 187 → 13 | 15 |
| PENTAGON+gs128/idle | 126.6 → 11.6 | 33.3 | 57 → 5 | 9 |
| PENTAGON+gs512/gs-upload | 723.2 → 27.8 | 74.3 | 122 → 95 | 256 |
| PENTAGON+gs512/idle | 324.8 → 11.2 | 31.2 | 57 → 5 | 7 |
| PENTAGON+moon/idle | 145.4 → 13.2 | 33.2 | 61 → 7 | 9 |
| PENTAGON+moon/moon-upload | 206.2 → 30.1 | 329.7 | 125 → 79 | 307 |
| PENTAGON+mouse/idle | 361.2 → 14.4 | 35.0 | 60 → 7 | 9 |
| PENTAGON+noay/idle | 27.8 → 3.9 | 10.2 | 57 → 4 | 5 |
| PENTAGON+tsfm/idle | 290.3 → 14.1 | 35.9 | 61 → 7 | 10 |
| PENTAGON+tsfm/music-tsfm | 106.6 → 36.8 | 63.3 | 216 → 41 | 84 |
| PENTAGON/demo | 210.2 → 21.4 | 64.7 | 281 → 142 | 259 |
| PENTAGON/demo-eyeache | 223.5 → 23.7 | 55.9 | 221 → 15 | 37 |
| PENTAGON/demo2 | 372.6 → 51.0 | 111.5 | 332 → 95 | 185 |
| PENTAGON/game | 404.9 → 82.4 | 105.8 | 332 → 280 | 488 |
| PENTAGON/idle | 49.5 → 17.8 | 39.2 | 57 → 7 | 12 |
| PENTAGON1024/idle | 162.5 → 17.8 | 39.1 | 179 → 8 | 10 |
| PENTAGON512/idle | 65.9 → 17.5 | 43.4 | 108 → 7 | 9 |
| PLUS2/idle | 44.0 → 14.2 | 31.2 | 57 → 6 | 9 |
| PLUS2A/idle | 49.8 → 14.5 | 30.9 | 78 → 6 | 9 |
| PLUS3/idle | 38.2 → 13.4 | 29.8 | 76 → 6 | 12 |
| PROFI/idle | 100.4 → 13.0 | 433.5 | 205 → 8 | 181 |
| PROFSCORP/idle | 139.4 → 14.8 | 37.2 | 90 → 8 | 22 |
| SCORPION-3.5MHz/idle | 56.2 → 14.9 | 35.5 | 84 → 8 | 10 |
| SCORPION/idle | 151.3 → 17.5 | 39.8 | 84 → 7 | 10 |
| SPRINTER/idle | 510.6 → 25.8 | 378.6 | 822 → 196 | 1766 |
| TSCONF/idle | 187.1 → 18.5 | 48.4 | 596 → 11 | 15 |
| TSL-VDAC2/idle | 1028.1 → 21.2 | 51.4 | 618 → 32 | 42 |

**A slow scan found and fixed while measuring.** The first timing run gave a capture p99 of 675 µs on the Sprinter (limit 1 ms) and a median of 28 µs on an idle Pentagon. The slowest Sprinter frames had many small changes and only three compressions: the changed-ranges encoding scanned each changed piece's difference byte by byte, twice, which cost more than compressing it (with ranges switched off as an experiment: 383 µs and 14 µs). The scan now skips zero bytes 8 at a time: Sprinter p99 379 µs, Pentagon median 18 µs (13 µs on a quieter host), the bytes unchanged. The loop is tagged `SIMD-CANDIDATE(ttd-ranges-scan)`.

A restore p99 of 4.4 ms on the Sprinter in a run at load 36–47 was the host: rerun at load 6, 1.7 ms.

## Migration v1 → engine

`TTDV1Feeder_Test.EngineRestoresEveryCheckpointAsV1` reads each v1 session of the corpus, builds the device table from the file's blobs, feeds it into the engine and compares every checkpoint, device states included. All 9 pass.

## Open after Phase 2

- Machine ROMs (the Sprinter BIOS among them) in the configuration fingerprint (Phase 3, Step 4).
- The delta base of mostly-zero regions (TODO, from Phase 1).
- The slot guards on load stay with v1 until Phase 5, where the generic device-set rule replaces them.
- Media versions (registry gaps 11–13; Phase 3).
