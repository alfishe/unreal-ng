# Phase 4 results: the session file

Part of the [TTD v1 → v2 migration](README.md). Phase 4 design: [phase-4-session-file-tdd.md](phase-4-session-file-tdd.md). Quality bar: [engine decision 33](engine-decisions.md#g-quality-bar-after-every-phase). Phase 3: [phase-3-results.md](phase-3-results.md).

Measured 2026-10-04 on branch `ttd-engine`, Release, on the shared development host.

## Summary

| Check | Result |
|---|---|
| Every recorded session survives the file | Yes. Every corpus session, saved and loaded, restores checkpoint for checkpoint (every region, every device, events, journals, configuration); saving the loaded session gives the same bytes (`TTDSessionFile_Test`) |
| Written as it records, crash-safe | Yes. The writer thread never holds capture; a forked child that aborts mid-recording leaves a file whose complete parts load exactly; a write error or a disk that cannot keep up stops the writer with its reason, the file valid to its last part |
| Damage | Open with holes (owner decision): CRC32C on every record, header and index; 4,120 damaged files never crash the loader and never load another session (`TTDSessionFileFuzz_Test`) |
| Session file not larger than v1's | Yes, on all 46 configurations: **2.3 to 62 times smaller** |
| Recorded bytes, memory, counted work not larger than v1 | Yes, on all 46 |
| Memory report is the real total (FR-16) | Within 1-2% of what the engine's allocations hold (`TimeTravelEngineHeap_Test`) |
| Capture p99 ≤ 1 ms, memory restore p99 ≤ 5 ms | Capture yes on all 46 (at most 781 µs). Restore: 44 of 46; the two others (Pentagon *game* 8.6 ms, *Eye Ache* 7.6 ms) were measured at a load of 60-93 and are pending a rerun on a quiet host. Phase 4 did not change the restore path; Phase 3 measured them at p99 0.9 ms (*game*) and 0.5 ms (*Eye Ache*), at a load of 13-16 |

The check is `tools/verification/ttd-bench/ttd_engine_d33.py` (now also comparing the session file, `bm7_file_bpf`). The baseline is `testdata/ttd/bench/engine-phase4-full.json` (`UNREAL_TTD_BENCH_SET=full UNREAL_TTD_BENCH_ENGINE=all`, 600 frames, with seeks).

## What Phase 4 built

| Step | What | Where |
|---|---|---|
| 1 | Integrity decided: CRC32C per record, header and index; open with holes | phase-4 TDD §5.1 |
| 2 | The container: records in parts, part ends with dependencies, index and trailer; scan recovery | `engine/ttdcontainer.*`, `platform/fileio.h` |
| 2 | The engine's session in it: versions as stored, reference tables rebuilt on load, events, journals in columns | `engine/ttdsessionfile.*` |
| 2 | Written as it records: a writer thread, lag limits, crash safety | `TTDSessionWriter` |
| 2 | The recording on disk: `~/.unreal-ng/ttd/<date-time>-<model>/`, an owner file, a startup cleanup of crashed recordings after 7 days | `FileHelper`, `CleanupManager`, `ttdrecordingfolders.h` |
| 3 | History in segments (D41): a ring of the last 5 minutes by default, or a list that grows; a baseline per segment; a file per segment; the last window on load; save as joins the files | `TTDHistoryPolicy`, `TTDRecordingWriter`, `LoadRecording`, `JoinSessionFiles` |
| 3 | The memory report covers everything (FR-16) | `HeapBreakdown().bookkeeping` |
| 4 | Frame-boundary streams in the file, the screenshot first | `AddFrameStreamCopy`, `ReadFrameStream` |
| 5 | v1 sessions converted (verification tools only) | `bench::ConvertV1Session` |
| 6 | The format described and read outside the emulator | `engine/ttdsession.ksy`, `ttd-analyzer/src/ttdcontainer.py` |

Found and fixed on the way:
- A file that did not start at the session's start loaded its bus journals at absolute positions: memory restored right, a replay would have read the wrong records.
- A writer that fell behind dropped the parts already queued (a race with its thread).
- The memory report left out 22% on a small session.
- A journal-build test raced its own threads on a loaded host.

## Results

| Configuration / workload | Session file v1 → engine (bytes per frame) | Smaller by | Memory v1 → engine | Restore p99, µs |
|---|---|---|---|---|
| 128K/idle | 997 → 153 | 7× | 2,451 → 1,177 | 10 |
| 48K/idle | 1,255 → 236 | 5× | 23,161 → 1,375 | 24 |
| ATM3+ay/idle | 7,822 → 455 | 17× | 14,264 → 2,618 | 37 |
| ATM3+beta/idle | 7,822 → 455 | 17× | 14,264 → 2,618 | 57 |
| ATM3+covox/idle | 7,822 → 455 | 17× | 14,264 → 2,618 | 36 |
| ATM3+gs128/idle | 7,947 → 424 | 19× | 14,364 → 2,607 | 36 |
| ATM3+gs512/idle | 7,973 → 425 | 19× | 14,391 → 2,608 | 34 |
| ATM3+moon/idle | 7,822 → 455 | 17× | 14,264 → 2,618 | 36 |
| ATM3+mouse/idle | 7,822 → 455 | 17× | 14,264 → 2,618 | 37 |
| ATM3+noay/idle | 6,955 → 219 | 32× | 11,180 → 1,950 | 23 |
| ATM3+tsfm/idle | 7,861 → 482 | 16× | 14,303 → 2,645 | 1658 |
| ATM3/idle | 7,811 → 436 | 18× | 13,207 → 2,081 | 64 |
| ATM450/idle | 2,342 → 244 | 10× | 30,015 → 1,402 | 28 |
| ATM710-turbo/idle | 3,791 → 790 | 5× | 9,790 → 2,231 | 183 |
| ATM710/idle | 3,866 → 829 | 5× | 9,873 → 2,262 | 129 |
| PENTAGON+ay/idle | 1,396 → 166 | 8× | 3,353 → 1,189 | 11 |
| PENTAGON+beta/disk-loading | 2,788 → 473 | 6× | 5,130 → 1,669 | 91 |
| PENTAGON+beta/idle | 1,434 → 185 | 8× | 3,390 → 1,212 | 10 |
| PENTAGON+covox/idle | 1,434 → 185 | 8× | 3,390 → 1,212 | 11 |
| PENTAGON+covox/music-covox | 2,375 → 239 | 10× | 4,530 → 1,352 | 19 |
| PENTAGON+gs128/idle | 1,551 → 156 | 10× | 3,488 → 1,208 | 84 |
| PENTAGON+gs512/gs-upload | 6,041 → 2,441 | 2× | 27,023 → 5,850 | 404 |
| PENTAGON+gs512/idle | 1,576 → 156 | 10× | 3,514 → 1,210 | 11 |
| PENTAGON+moon/idle | 1,434 → 185 | 8× | 3,390 → 1,212 | 11 |
| PENTAGON+moon/moon-upload | 5,715 → 2,438 | 2× | 26,753 → 5,736 | 366 |
| PENTAGON+mouse/idle | 1,434 → 185 | 8× | 3,390 → 1,212 | 33 |
| PENTAGON+noay/idle | 739 → 59 | 13× | 2,697 → 837 | 5 |
| PENTAGON+tsfm/idle | 1,434 → 185 | 8× | 3,390 → 1,212 | 10 |
| PENTAGON+tsfm/music-tsfm | 3,889 → 930 | 4× | 9,060 → 2,397 | 89 |
| PENTAGON/demo | 5,921 → 1,348 | 4× | 14,359 → 4,043 | 386 |
| PENTAGON/demo-eyeache | 11,769 → 1,511 | 8× | 37,415 → 12,044 | 7598 (load 60–93: rerun pending) |
| PENTAGON/demo2 | 8,346 → 1,815 | 5× | 23,539 → 6,922 | 1252 |
| PENTAGON/game | 10,948 → 3,024 | 4× | 31,887 → 9,674 | 8603 (load 60–93: rerun pending) |
| PENTAGON/idle | 1,436 → 171 | 8× | 2,869 → 1,205 | 11 |
| PENTAGON1024/idle | 2,334 → 171 | 14× | 3,765 → 1,208 | 26 |
| PENTAGON512/idle | 1,821 → 171 | 11× | 3,253 → 1,206 | 12 |
| PLUS2/idle | 998 → 155 | 6× | 2,452 → 1,179 | 231 |
| PLUS2A/idle | 1,022 → 141 | 7× | 2,640 → 1,298 | 10 |
| PLUS3/idle | 1,099 → 143 | 8× | 2,711 → 1,299 | 13 |
| PROFI/idle | 2,213 → 188 | 12× | 3,376 → 1,105 | 183 |
| PROFSCORP/idle | 1,760 → 214 | 8× | 3,017 → 1,264 | 16 |
| SCORPION-3.5MHz/idle | 1,701 → 192 | 9× | 3,125 → 1,241 | 13 |
| SCORPION/idle | 1,702 → 196 | 9× | 3,126 → 1,245 | 18 |
| SPRINTER/idle | 19,299 → 312 | 62× | 21,387 → 1,790 | 774 |
| TSCONF/idle | 6,853 → 544 | 13× | 25,376 → 2,244 | 21 |
| TSL-VDAC2/idle | 7,181 → 561 | 13× | 25,699 → 2,282 | 43 |

The file is smaller than v1's for three reasons. Every piece version is stored once, with its encoding as captured; reference tables are rebuilt on load, not stored; and the bus journals and the write journal are columns. The largest factor is the Sprinter (19,299 → 312 bytes per frame): v1 stores its large device states whole in every checkpoint.

## Open after Phase 4

- Rerun the two restore timings on a quiet host (load < 12).
- A Kaitai-generated parser run (no compiler on the build host).
- The screenshot's size and frame-time cost on the matrix (`bm3_stream_screenshot_bpf`, `bm1_overhead_screenshot_pct`).
- Not dropped by the ring yet: the sector-read journal (rare) and the write journal (off by default).
- Phase 5: the emulator, every surface and the UI on the engine: the user path to the recording folder, the black-box setting, the frame-stream controls, `rzx/seek` through the engine.
