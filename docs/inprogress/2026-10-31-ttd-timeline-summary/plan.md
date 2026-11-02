# TD-5 timeline summary: plan

Requirements: [README.md](README.md). Design: [design.md](design.md). Worktree `scratch/wt-ttd-timeline`, branch
`ttd-timeline`.

## Steps

| # | Step | Done when |
|---|------|-----------|
| 0 | Baseline: the benchmarks of B1-B3 built and run on the tree before the change (`TTD_TIMELINE_BASELINE`) | numbers in this file |
| 1 | `TTDTimelineResult` and `TimeTravelController::QueryTimeline`, tier 1 facts | S1 tests green |
| 2 | Tier 2: the port classification | S1 tests green, B1 measured, the `ports` default fixed |
| 3 | Phases (tags, runs, merge) | S1 phases match the script; S2 checks |
| G | **Gate:** quality and size on the ULA machines (48K, 128K, Pentagon): S1 phases match, UMT23X depacker found, reply bytes per 1,000 frames and per phase measured and judged | numbers in this file, then go on at once |
| 4 | Every other machine and peripheral (design §5, TL-11): the video families (ATM, Profi, Pentagon 16-colour and multicolour, ZX-Evo text, TS-Conf, Next, Sprinter), the devices on every model's port map, the media slots, the cards with their own CPU / DMA (each device's time fields checked) | S3 green on every corpus model, no `missing` entry for a fact the model has |
| 5 | The verb and the six planes, docs, recipe | verb, MCP unit tests, live MCP script |
| 6 | Benchmarks after the change, the A/B of the recording rows | B1-B3 numbers in this file |
| 7 | All platforms: clang, gcc (docker), MSVC, MinGW; full core-tests | then land |

## Tests

S1 is a synthetic program, generic (no game): a Pentagon program whose phases are known because the test writes them.

| Phase | The program does | The facts that must show |
|-------|------------------|--------------------------|
| A | polls EAR in a tight IN loop (a loader's edge loop), no tape in the deck: a poll, no `tape` tag (the rate with a tape in: `ttdtimeline_tape_test.cpp`) | `ula_reads` high, no `tape` |
| B | fills 16 KB of RAM with a computed pattern by LDIR (an unpacker) | `burst`, `written` high |
| C | HALT-paced loop polling the keyboard; the test presses keys | `halt`; `keys` named in the phase facts |
| D | per frame: writes AY registers, toggles the beeper, redraws part of the screen, pages 7FFD | `sound`, `screen`, `paging` |

Tests (`core/tests/debugger/ttd/ttdtimeline_test.cpp`, about 10 ms each, the recording made once per suite):

- `FactsMatchTheDirectQueries`: per bucket, `executed / written / read` equal `coverage-summary`; `in / out` equal the
  port journal counted directly; `keys` equal the input journal; `events` equal `markers` and `bookmarks`.
- `PhasesFollowTheScript`: S1's four phases, each boundary within half the tagging window (5 frames), nothing split or missed.
- `PhasesDoNotDependOnTheBuckets`: the phases are the same with no buckets and with 1, 5, 100 or 500 of them.
- `MissingFactsAreNamed`: write journal off -> `writes` missing; port journal dropped -> `ports` missing, tags that
  need ports not set; a TS-Conf video mode -> `screen` missing.
- `LiveStoppedAndLoadedAgree`: the same reply (minus `cost`) while recording, stopped, and after a save and load.
- `WindowAndBucketOptions`: from / to, bucket size, limit clamp, `buckets` on / off, bad values refused (400).
- `QueryLeavesTheMachineAlone`: the machine state hash and the recording's frame count are unchanged by the call.
- Verb, MCP (`McpTools_Test.TimeTravel_Timeline_*`: the text lists the phases), CLI, WebAPI route list
  (`companion-routes_test.cpp`), the live MCP script (`tools/verification/mcp/test_time_travel.py`).
- S3, `EveryModelReportsItsScreenAndDevices` (the corpus models: 48K, 128K, +2, +2A, +3, Pentagon, Scorpion, Prof.
  Scorpion, ATM 7.10, ATM 4.50, ATM 3, Profi, Profi 3, TS-Conf, TS-Conf VDAC2, Sprinter, plus Next): a short program per
  model that draws to its displayed screen in its own video mode (and switches mode once) and touches each device of
  its port map: `screen_frames` equal the frames the program drew, each device's `in / out` equal the port journal
  counted by hand, each media slot's `sectors` equal the media journal. Quick tier: 48K, Pentagon, TS-Conf, Sprinter,
  Next; the rest in the long tier.
- Long tier (`UNREAL_LONG_TESTS=1`), S2: UMT23X loaded from tape inside a recording, then run on: the tape phase, the
  depacker's `burst` phase and the program after it, without any address in the test.

## Benchmarks: with and without the query

`core/benchmarks/debugger/ttd/ttd_timeline_benchmark.cpp`, the pattern of `ttd_analysis_query_benchmark.cpp`: one
source, built in both trees; `TTD_TIMELINE_BASELINE` (the tree before the change) keeps only what the old API can run.

| # | Row | Measures |
|---|-----|----------|
| B1 | `BM_Timeline_OneCall/<session>` | the `timeline` call, tier 1 only and with ports; counters `reply_bytes`, `port_records` |
| B1 | `BM_Timeline_OldLoop/<session>` | what an agent runs today for the same picture: `status`, `coverage-summary` x 3 kinds, `markers`, `bookmarks`, `port-events` for key / ear / border / beeper / ay-write per bucket window (paged by `limit`), `memory-diff` of the screen per bucket; counters `calls`, `reply_bytes` |
| B2 | `BM_Timeline_*` on sessions of 3,000 and 50,000 frames (S1 repeated) and the UMT23X recording | the scaling with frames and with port records |
| B3 | `frame_profile_benchmark` recording rows (`WorstNormal` and the TTD rows), before and after | TL-3: zero recording cost; 4 interleaved rounds, the minimum CPU time per row, judged against the A/A bias |

Reported here as a table: time, calls and reply bytes with the query and without it, per session. The old loop's
reply bytes stand for the agent's tokens (the MCP text of the same calls is measured by the live script and added).

## Measurements

2026-11-01, M3 Ultra (.20), one iteration per row for the old loop (10-190 s each), three for the call; the full
table and the cost breakdown are in [design.md §6](design.md#6-costs).

- B1 / B2: one `timeline` call against the old loop is 189-327x faster and its reply 33-117x smaller (3,000 and
  50,000 synthetic frames, old loop over 20 or 100 buckets); 50,000 frames take 403 ms (`ports=false`) to 863 ms
  (ports and 20 buckets), under the 1 s target. Before the stamp counting of distinct keys they took 1.7-3.4 s.
- Live UMT23X load (12,061 frames, `scratch/ttd-timeline/live_timeline.py`): REST 33x faster, 9x fewer bytes; MCP
  34x faster, 22x less JSON, 9x shorter text (126 calls against one).
- Buckets: 5 / 10 / 20 / 50 / 100 / 500 make the reply 1.8 / 2.6 / 4.1 / 8.6 / 16 / 74 times the phases-only one,
  the phases stay the same: buckets off by default, `limit` 20.
- `min_phase_ms` on UMT23X: 100 and 200 give the same four phases (typing, the burst after ENTER, a pause, the tape);
  500 merges the burst and the pause into the typing, 1000 leaves one phase: 200 by default.
- Recording cost of the `halt` fact: one counter increment per accepted INT and two fields per checkpoint; of the
  screen layout: one `DescribeDisplayedMemory` call per checkpoint into reused vectors, interned against the last
  layout (tens of ns against ~330 us a frame).
- B3 (2026-11-01, Mac, load average 70-100, 8 interleaved rounds, minimum CPU time per row, base = github/master
  c4f64fb44):

  | Row | base, us | branch, us | A/B | A/A (6 rounds) |
  |-----|---------:|-----------:|----:|----:|
  | `Pentagon_WorstNormal` | 340.3 | 345.9 | +1.6 % | -0.6 % |
  | `Pentagon_TtdRecordNormal` | 333.6 | 334.1 | +0.2 % | +0.6 % |
  | `Pentagon_TtdRecordSoundOff` | 462.3 | 459.1 | -0.7 % | +0.7 % |
  | `Pentagon_TsfmTune_TtdRecordNormal` | 950.0 | 909.9 | -4.2 % | +3.6 % |

  Every A/B row is inside the A/A spread of the same host: no recording cost measurable (TL-3 holds).

- Gate G on 48K, a real load (`scratch/ttd-timeline/after_load.py`: `load_software` autostart, the recording until
  the tape stops, then 1,500 frames; three runs): 3,520 frames, 26 ms, 5.8 KB. The phases are the ROM typing
  `LOAD ""`, the header, the loading screen, the data block, the depacker (`tape,burst` then `burst`, 30 frames,
  ~7,000-9,400 bytes written each) and the loader waiting for more tape. The runs differ by one frame at most.
- The depacker writes ~510 bytes per frame on its quiet frames: the burst floor is 256 bytes (it was 1,024, which
  left the depacker untagged).
- Found by the gate: a tape loaded while TTD records lands at the next frame boundary (an event of the recording),
  so the play right after the load (the `load_software` autostart) answered "no loadable blocks". `Tape::Play` is
  now the one play of every plane: it queues the play for a tape on its way and starts it when the tape lands
  (`timetravelcontroller_tapeload_test.cpp`).

- Step 4, the `screen` fact on every video family (`scratch/ttd-timeline/models_sweep.py`: a fresh machine of each
  corpus model, 100 recorded frames past its boot, the local Mac on 2026-11-01). The first cut compared 4 KB pieces
  and counted all 100 frames as `screen` on 48K, +2, +2A and Pentagon: the ROM's FRAMES counter (#5C78) shares a
  piece with the attributes. Byte exact (design §5) they count 0 frames, which is what their idle boot screens do.
  The others: +3 12, Scorpion 9, Prof. Scorpion 10, ATM710 0, ATM450 11, ATM3 100 (its picture changes every frame:
  the screen digest differs on each of 20 frames, `scratch/ttd-timeline/screen_changes.py`), Profi 2, Profi 3 5,
  TS-Conf 2, TS-Conf VDAC2 2, Sprinter 3, Next 5. `missing[]` on every model holds only `writes` (the journal is off
  by default); the query takes 0.5-8 ms for the 100 frames.
- Found by the sweep: the Next firmware menu polls #FE 3,000 times a frame with no tape anywhere, which the read rate
  alone tagged `tape`. The reads count as tape only while a tape is in the deck (the session's media versions of the
  `tape` slot). Any tape in the deck starts by itself once a loader listens (the tape's sustained-poll start), so
  with a tape in the reads matter only for a recording started while the tape already played; S1 phase A runs with
  no tape and is no tape phase now, the rule is covered by `ttdtimeline_tape_test.cpp`.

Progress: all steps done. Step 4: the screen layouts of every video family (`screendisplayedmemory_test.cpp`,
`ttdtimeline_screen_test.cpp`), the tape rule (`ttdtimeline_tape_test.cpp`), the host-read media slots and the device
memory regions (the Sprinter IDE sectors and video RAM in `ttdsprinter_test.cpp`; the device-state blobs left out).
Step 6: B3 above. Step 7 (2026-11-01, ee52875b2): clang zero warnings, full core-tests green; gcc 14.2 (CI image,
docker on .20) zero warnings, 10,537 passed, 0 failed; MSVC 2019 and MinGW zero warnings, full core-tests 0 failed;
the live MCP scripts 30/30 (`test_ws_surface` slow consumer made load proof: it keeps asking while it waits).
