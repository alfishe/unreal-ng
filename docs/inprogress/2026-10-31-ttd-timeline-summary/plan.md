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
| A | polls EAR in a tight IN loop (a loader's edge loop) with a tape marker around it | `tape`, `ear_reads` |
| B | fills 16 KB of RAM with a computed pattern, then copies it (an unpacker) | `burst`, `written` high |
| C | HALT-paced loop polling the keyboard; the test presses keys | `idle`, `input`, `key_polls`, `keys` named |
| D | per frame: writes AY registers, toggles the beeper, redraws part of the screen, pages 7FFD | `sound`, `screen`, `paging` |

Tests (`core/tests/debugger/ttd/ttdtimeline_test.cpp`, about 10 ms each, the recording made once per suite):

- `FactsMatchTheDirectQueries`: per bucket, `executed / written / read` equal `coverage-summary`; `in / out` equal the
  port journal counted directly; `keys` equal the input journal; `events` equal `markers` and `bookmarks`.
- `PhasesFollowTheScript`: S1's four phases, each boundary within one bucket, nothing split or missed.
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

(filled in by steps 0, 2, G and 6)
