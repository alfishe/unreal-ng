# Phase 3 results: everything a replay needs

Part of the [TTD v1 → v2 migration](README.md). Phase 3 design: [phase-3-replay-inputs-tdd.md](phase-3-replay-inputs-tdd.md). Quality bar: [engine decision 33](engine-decisions.md#g-quality-bar-after-every-phase). Phase 2: [phase-2-results.md](phase-2-results.md).

Measured 2026-10-04 on branch `ttd-engine` (the commit that adds this document), Release, on the shared development host.

## Summary

| Check | Result |
|---|---|
| A seek from the engine's own data lands on v1's machine | Yes. Restore, input, bus data, sector reads and interrupt vectors come from the engine; CPU, all RAM and every device are equal on a live recording with keys, the Dizzy X and Green Beret fixtures, and on Pentagon + NeoGS, TS-Conf, Sprinter, Scorpion, Profi and ZX-Evo (`TimeTravelManager_EngineSeek_Test`, `TimeTravelManager_EngineSeekModels_Test`) |
| Every checkpoint of the corpus restores as v1's | Yes (`TTDV1Feeder_Test`, 9 sessions), and every write journal of the corpus imports into the engine |
| Recorded bytes not larger than v1 | Yes, on all 46 configurations and workloads |
| Memory not larger than v1 | Yes, on all 46. Both sides now count their bus journals and the write journal |
| Counted capture work not larger than v1 | Yes, on all 46 |
| Capture p99 ≤ 1 ms (PR-3), memory restore p99 ≤ 5 ms (PR-5) | Yes, on all 46, at load 13–16: capture p99 at most 325 µs (TS-Conf), restore p99 at most 0.9 ms (Pentagon, game) |

The check is `tools/verification/ttd-bench/ttd_engine_d33.py`. The baseline is `testdata/ttd/bench/engine-phase3-full.json` (`UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_ENGINE=all`, 600 frames, with seeks).

## What Phase 3 changed

| Step | What | Where it shows |
|---|---|---|
| 1. One event stream | The engine's event log and payload store take v1's input, network records and markers. `IN` / `OUT` bus journals have a cursor per checkpoint. Debugger edits carry their bytes, and a replay crosses them | `TTDEventLog`, `FeedV1Events`, `SetReplaySource(engine)` |
| 1. Media reads | Every sector read from an image is journaled at the read (`MediaReadTap`), CD data reads too; a replay needs no image file | `TTDMediaJournal` |
| 1. Vectors, port journals everywhere | Interrupt vectors are recorded on machines with their own INT logic. The port journals are recorded on every machine | `BusVectors`, `_portJournalRecorded` |
| 2. RZX playback | TTD records while an RZX plays: the player's position is in every checkpoint (`RzxPlayback`, id 47), and RZX frame ends are facts; a seek to RZX frame N equals the RZX player's own seek | `TimeTravelEngine::RzxFrameTime` |
| 3. Machine time | From the frame table: every frame at its measured length (Sprinter 320 / 312 lines) | `FrameLengthChange` facts |
| 4. Configuration and media | A named-field fingerprint (model, RAM, timing, audio and render settings, board options, every ROM); media slot versions per checkpoint | `LastEngineCheck`, `IMediaHistory` |
| 5. Real-time clocks | One session time base for every DS12887 user | `PortDecoder::SessionWallMicros` |
| 6. Nothing written outside the session during a replay | Write-through media are held in memory until the replay ends | `HostWriteHold` |
| 7. Write journal on demand (D40) | Off by default; switched at any moment, in segments; built for any span by replay; in the engine as compressed blocks; on every surface and in the Qt panel | [write-journal-e7.md](write-journal-e7.md), `TTDWriteIndex` |

Fixed on the way: the tape's restore (Green Beret's tape load: 780–3,444 differing `IN` values in a replay, now 0); find-last hits after the query time; a seek back inside an RZX playback desynced the player.

## Results

Bytes per frame (memory, references, devices), memory per frame, counted work per frame; 600 frames. "Phase 2" is `engine-phase2-full.json`.

| Configuration / workload | Bytes v1 → Phase 2 → Phase 3 | Memory v1 → Phase 2 → Phase 3 | Work v1 → Phase 3 |
|---|---|---|---|
| 128K/idle | 723 → 271 → 258 | 2,451 → 753 → 1,177 | 180,549 → 58,121 |
| 48K/idle | 690 → 294 → 283 | 23,161 → 778 → 1,375 | 171,292 → 78,647 |
| ATM3+ay/idle | 7,076 → 608 → 559 | 14,264 → 1,287 → 2,618 | 4,338,518 → 152,559 |
| ATM3+beta/idle | 7,076 → 608 → 559 | 14,264 → 1,287 → 2,618 | 4,338,518 → 152,559 |
| ATM3+covox/idle | 7,076 → 608 → 559 | 14,264 → 1,287 → 2,618 | 4,338,518 → 152,559 |
| ATM3+gs128/idle | 7,176 → 573 → 533 | 14,364 → 1,287 → 2,607 | 4,448,149 → 129,135 |
| ATM3+gs512/idle | 7,202 → 576 → 534 | 14,391 → 1,290 → 2,608 | 4,841,365 → 129,659 |
| ATM3+moon/idle | 7,076 → 608 → 559 | 14,264 → 1,287 → 2,618 | 4,338,518 → 152,559 |
| ATM3+mouse/idle | 7,076 → 608 → 559 | 14,264 → 1,287 → 2,618 | 4,338,518 → 152,559 |
| ATM3+noay/idle | 6,398 → 244 → 210 | 11,180 → 757 → 1,950 | 4,287,541 → 84,040 |
| ATM3+tsfm/idle | 7,115 → 636 → 586 | 14,303 → 1,314 → 2,645 | 4,339,545 → 154,722 |
| ATM3/idle | 7,070 → 608 → 546 | 13,207 → 1,287 → 2,081 | 4,338,518 → 148,110 |
| ATM450/idle | 1,683 → 360 → 327 | 30,015 → 822 → 1,402 | 585,269 → 73,609 |
| ATM710-turbo/idle | 2,642 → 552 → 702 | 9,790 → 1,281 → 2,231 | 1,141,962 → 110,392 |
| ATM710/idle | 2,675 → 539 → 722 | 9,873 → 1,269 → 2,262 | 1,155,282 → 119,152 |
| PENTAGON+ay/idle | 1,100 → 320 → 302 | 3,353 → 780 → 1,189 | 188,151 → 71,756 |
| PENTAGON+beta/disk-loading | 2,293 → 584 → 410 | 5,130 → 1,282 → 1,669 | 206,870 → 89,344 |
| PENTAGON+beta/idle | 1,138 → 342 → 324 | 3,390 → 802 → 1,212 | 189,178 → 73,900 |
| PENTAGON+covox/idle | 1,138 → 342 → 324 | 3,390 → 802 → 1,212 | 189,178 → 73,900 |
| PENTAGON+covox/music-covox | 2,045 → 481 → 395 | 4,530 → 1,157 → 1,352 | 216,294 → 92,891 |
| PENTAGON+gs128/idle | 1,236 → 320 → 308 | 3,488 → 811 → 1,208 | 298,809 → 50,485 |
| PENTAGON+gs512/gs-upload | 2,369 → 1,146 → 1,208 | 27,023 → 1,875 → 5,850 | 771,451 → 122,804 |
| PENTAGON+gs512/idle | 1,261 → 322 → 309 | 3,514 → 814 → 1,210 | 692,025 → 51,009 |
| PENTAGON+moon/idle | 1,138 → 342 → 324 | 3,390 → 802 → 1,212 | 189,178 → 73,900 |
| PENTAGON+moon/moon-upload | 2,056 → 673 → 1,099 | 26,753 → 1,364 → 5,736 | 269,424 → 144,171 |
| PENTAGON+mouse/idle | 1,138 → 342 → 324 | 3,390 → 802 → 1,212 | 189,178 → 73,900 |
| PENTAGON+noay/idle | 444 → 60 → 58 | 2,697 → 397 → 837 | 161,259 → 25,485 |
| PENTAGON+tsfm/idle | 1,138 → 342 → 324 | 3,390 → 802 → 1,212 | 189,178 → 73,900 |
| PENTAGON+tsfm/music-tsfm | 2,871 → 864 → 798 | 9,060 → 1,540 → 2,397 | 216,526 → 102,516 |
| PENTAGON/demo | 3,299 → 879 → 939 | 14,359 → 1,557 → 4,043 | 234,595 → 111,245 |
| PENTAGON/demo-eyeache | 3,075 → 586 → 956 | 37,415 → 1,263 → 12,044 | 254,497 → 136,476 |
| PENTAGON/demo2 | 3,336 → 1,058 → 1,177 | 23,539 → 1,730 → 6,922 | 285,707 → 163,130 |
| PENTAGON/game | 2,896 → 1,726 → 1,385 | 31,887 → 2,404 → 9,674 | 257,801 → 142,757 |
| PENTAGON/idle | 1,141 → 342 → 319 | 2,869 → 802 → 1,205 | 189,178 → 72,170 |
| PENTAGON1024/idle | 2,037 → 349 → 322 | 3,765 → 812 → 1,208 | 1,106,682 → 72,782 |
| PENTAGON512/idle | 1,525 → 345 → 321 | 3,253 → 807 → 1,206 | 582,394 → 72,433 |
| PLUS2/idle | 724 → 272 → 260 | 2,452 → 755 → 1,179 | 180,549 → 58,121 |
| PLUS2A/idle | 745 → 268 → 255 | 2,640 → 751 → 1,298 | 183,748 → 58,154 |
| PLUS3/idle | 816 → 271 → 257 | 2,711 → 753 → 1,299 | 184,189 → 58,652 |
| PROFI/idle | 1,882 → 336 → 283 | 3,376 → 813 → 1,105 | 1,103,109 → 63,444 |
| PROFSCORP/idle | 1,437 → 394 → 359 | 3,017 → 856 → 1,264 | 317,124 → 70,323 |
| SCORPION-3.5MHz/idle | 1,397 → 385 → 354 | 3,125 → 847 → 1,241 | 317,033 → 70,243 |
| SCORPION/idle | 1,398 → 389 → 358 | 3,126 → 851 → 1,245 | 317,033 → 70,240 |
| SPRINTER/idle | 18,791 → 1,097 → 425 | 21,387 → 2,182 → 1,790 | 4,617,553 → 427,734 |
| TSCONF/idle | 6,011 → 465 → 424 | 25,376 → 1,197 → 2,244 | 4,260,682 → 89,093 |
| TSL-VDAC2/idle | 6,334 → 532 → 481 | 25,699 → 1,256 → 2,282 | 4,266,651 → 100,219 |

**Memory went up from Phase 2, and it is still below v1 everywhere.** The engine now keeps what a replay from its own data needs: the `IN` / `OUT` bus journals (on every machine), the event log, sector reads and the write journal. v1's figure counts its own port journals and write journal the same way. The largest engine figures are the workloads with many port reads: 12,044 bytes per frame on the eyeache demo against v1's 37,415.

**Bytes per checkpoint changed by a few percent** (128K idle 271 → 258, ATM3 608 → 559). Phase 3 did not change how memory and device states are stored; the bus data and events are counted under memory, not here.

## Open after Phase 3

- Several CPUs: the CPU table, the clock map and positions on a card CPU, deferred to the GS debugger, their user (Step 3).
- Media heads that go back, with the storage manager's change layer (Step 4).
- `rzx/seek` through the engine and the removal of `RzxKeyframeStore`, with the switch-over (Phase 5).
- The session file: events, bus journals, write-journal segments, the media and the RZX recording in the session (Phase 4).
