# TD-5: TTD timeline summary

**Status 2026-11-01:** landed on github/master (`b0615018a`): every plane, every video family, the device regions, B1-B3 measured ([plan.md](plan.md)). Plan row [#7](../PLAN.md). Supersedes the 2026-09-14 design
([ttd-timeline-summary-design.md](../2026-09-14-automation-triage-gaps/designs/ttd-timeline-summary-design.md)): two
of its data sources do not exist, see "Why a new design" below.

Files:

| File | What |
|------|------|
| [design.md](design.md) | data sources, the query, the reply, phases, costs, the planes |
| [plan.md](plan.md) | steps, tests, benchmarks (with and without the query), acceptance |

## The problem (G-2)

An agent that opens a recording of 5,000 to 50,000 frames does not know where anything happened. Today it learns the
shape of a session from several calls, each with its own window and format:

- `coverage-summary`: distinct executed / written / read addresses per bucket (TD-7);
- `markers`, `bookmarks`: the tape, disk and debugger-edit events, the user's labels;
- `port-events`: key reads, EAR, border, AY writes - one event list per call, cut at `limit`;
- `memory-diff`, `memory-at`: what changed between two frames, one pair a call;
- `status`: the history's ends.

Nothing says "frames 0-310 tape loading, 311-402 unpacking, 403-1200 menu waiting for a key, 1201-... the game with
sound". The agent builds that picture itself, with tens of calls, and it costs tokens and time on every recording.

## The goal

One call returns the recording's timeline: per bucket of frames, the facts the engine already holds (no replay), and
the recording cut into **phases** - runs of buckets with the same activity - with a frame range and the facts of each.
The agent then asks the existing precise queries inside the phase it cares about.

## Requirements

| # | Requirement |
|---|-------------|
| TL-1 | One query, `timeline`, on all six planes: CLI, WebAPI (and the socket call), Lua, Python, MCP. Docs chain command-interface.md -> plane docs |
| TL-2 | No replay. Every fact comes from what the recording already stores: checkpoints, journal cursors, the coverage index, the input / marker / bookmark journals, the engine event log, the port journals |
| TL-3 | Zero recording cost: no new capture, no new per-frame field, no file format change. Proven by an A/B of the recording profile rows |
| TL-4 | A live recording (parked briefly, as `coverage-summary` does), a stopped one and a loaded `.ttd` session answer the same way |
| TL-5 | Phases are generic: one rule for every program and machine, built from the facts and thresholds relative to the session itself. No per-game tuning (owner rule 2026-10-26); hold-out recordings check it |
| TL-6 | A fact the session does not hold is reported missing, never guessed: write journal off, port journal invalid, coverage not indexed for a frame, a model without a known screen area |
| TL-7 | The reply fits an agent: the phases first, the buckets only when asked; the MCP text carries what an agent needs (an agent may see only the text) |
| TL-8 | Cost bounded by the window, not the session: tier 1 facts O(frames), the port classification O(port records) and opt-out; the reply size bounded by `limit` |
| TL-9 | Tests about 10 ms each, integration under 1 s; long variants behind `UNREAL_LONG_TESTS=1` |
| TL-10 | Benchmarks with and without: the one call against the loop of old calls an agent runs for the same picture (time, calls, reply bytes), on a synthetic session and on UMT23X; and the recording cost before and after |
| TL-11 | Every machine and every peripheral, not only the Spectrum ULA: each video family's displayed memory, each device on the port map (sound, storage, input, network, RTC, ...), each media slot. Order: the ULA machines first; once their quality and reply size are checked, the rest at once, in the same task (owner 2026-10-31) |

## Acceptance

- On the synthetic phase program (plan.md, S1) the phases match the script's phases: every boundary within one
  bucket, no phase missed, no phase split in two.
- On UMT23X (tape load, then the depacker, then the program) the depacker burst is its own phase, found without
  knowing any address (the 2026-09-15 litmus test of
  [ttd-coverage-evaluation.md](../2026-09-14-automation-triage-gaps/ttd-coverage-evaluation.md)).
- The one call against the old loop: fewer calls (1 against tens), fewer reply bytes, and not slower; numbers in
  plan.md.
- The recording rows of `frame_profile_benchmark` within the A/A bias before and after.
- Every model of the TTD corpus (16 models, the corpus test's list) answers with its own screen and devices: no
  `missing` entry for a fact the model has.
- Zero warnings on clang, gcc, MSVC, MinGW; full core-tests; the live MCP script.

## Why a new design

The 2026-09-14 design read `TTDCheckpoint::writeJournalOffset` (never filled, removed) and `ramPages.size()` (one entry
per RAM page in every checkpoint, not a dirty count). Its review (2026-09-16) moved the heatmap to TD-7
`coverage-summary`. That covers addresses only. TTD v2 now keeps much more per frame: the bus journal cursors in every
checkpoint (IN / OUT / sector reads per frame by subtraction), the changed 4 KB pieces per checkpoint, the input and
marker journals, the engine event log with media and config changes. This design builds on those.
