# TTD replay queries: where they run and what they cost (2026-11-27)

The check behind dropping the clone pool of POC 038 ([phase-2-plan.md](phase-2-plan.md#why-no-clone-pool)): which
TTD queries replay, on which machine, how long they take, and whether the background analysis instance
(`AnalysisInstance`, `ReplayWindow`) would serve them better.

Measured on the build Mac (M3 Ultra, master `56826b370`, Release, `unreal-qt` offscreen, calls through MCP
`invoke_api`), host load average 7-36 during the runs, three repeats within ±3 % (one outlier +15 %). Pentagon,
`testdata/loaders/sna/Dizzy X.sna`, recordings of 5,000 and 50,000 frames, coverage index on, no write journal.
Times are the median of three, "5k / 50k". Not measured: `export-clip`, the card CPU verbs, sessions with branches
or with history evicted to disk, a quiet host.

## Where each query runs

| Query | Runs | Holds the machine | 5k / 50k |
|---|---|---|---|
| `status`, `position`, `markers`, `bookmarks`, `branches`, `journal`, `seek-check`, `file-info` | stored data | no | milliseconds |
| `coverage-scan`, `coverage-rare` | coverage index | no (the machine keeps running and recording) | 8.9 / 71.5 ms, 15.9 / 140.7 ms |
| `timeline` | stored data | no | 5.2 / 33.4 ms |
| `port-events`, `find-last` of a port | port journal | no | 1.6 / 1.4 ms |
| `memory-series` | piece store | no; a job with cancel | 46 / 443 ms |
| `memory-at` at a frame start, `memory-diff` | piece store | pauses a running machine, though no CPU runs | 1.9 / 1.9 ms, 2.0 / 2.6 ms |
| `find-last` of a write with the write journal | journal | pause | 24.6 / 217.7 ms |
| `dump` | serialization | a job | 20 / 175 ms |
| `seek`, `step-back`, `reverse-step`, one `reverse-continue`, `resume`, `branch-switch` | the user's machine (the result is its state) | pause, the position moves on purpose | 3.0 / 2.8 ms, 2.9 / 2.7 ms, 5.0 / 4.7 ms |
| `memory-at` inside a frame, `screen-at`, `explain-frame`, `diff`, `backtrace`, `cause-chain`, `port-events` with context | the user's machine, one seek or a few frames | pause; the position comes back | 2.5 to 42 ms |
| `find-event` | candidates from stored data, a replay of the frames that hit | pause; a job | 45 / 431 ms (memory), 139 / 205 ms (exec, 100 hits) |
| `find-last`, `find-last all` (read, exec, a write without the journal) | the user's machine, a window; the coverage index prunes | pause for the whole call; no job, no cancel | usual 5 to 55 ms; worst case 3.8 / 38.7 s |
| `reverse-continue all`, `first_per_pc`, `condition` | the user's machine, a window | the same; no job | usual 5 to 72 ms; a condition that never holds 5.8 / 58.5 s |
| `probe-frames` | the user's machine, a window | pause; a job with progress and cancel | 3.7 / 34.3 s (port in), 5.2 / 52.3 s (read with `if`) |
| `replay-check` | the user's machine, a window | pause; a job | 3.9 / 41.0 s |
| `journal-build` | the user's machine, a window (writes into the live session) | pause; a job with cancel | 3.6 / 38.6 s |
| `analysis build`, `analysis trace`, `experiment`, `cover` | an analysis instance | only while the session is captured | (not TTD verbs) |

A window replays in place at 0.69-1.17 ms a frame, linear from 5k to 50k frames.

What "holds the machine" means: a query pauses the emulator and leaves it paused (the recording goes to
`recording_paused`; a resume continues both); queries that read restore the position they found. While a job runs,
`/ttd/status` and `/ttd/position` answer in 1.3-1.5 ms; any other TTD verb waits for the job on the controller's
lock, the stored-data ones included.

## The background instance does not answer faster

Making a clone (`TraceBundle::Capture`, `AnalysisInstance::Make`, loading the session, a seek) takes about 35 ms
plus 7 ms per 1000 frames: 70 ms at 5k, 378 ms at 50k, on every call (no clone is kept), and about 107 MB while it
lives. The same question over a 3000-frame window:

| Question | Session | In place (`probe-frames`) | Clone (`analysis trace` with a filter) |
|---|---|---|---|
| port in | 5k | 2.07 s (691 µs a frame) | 2.32 s (773 µs a frame) |
| read | 5k | 3.10 s | 2.23 s |
| port in | 50k | 3.03 s | 3.48 s |
| read | 50k | 3.13 s | 2.55 s |

The answers agree (reads of one address over 2001 frames: 2001 of 2001 with equal counts per frame). A clone can be
opened on any branch and moment (the session file holds every live branch, `SwitchBranch`), from the code; not run.

## Conclusion

Nothing moves to a clone: one query costs 2.5-40 ms against 70-380 ms to make a clone, a clone replays no faster
than the machine does in place, and a long window as a job no longer blocks the reads of the state. The pool of
POC 038 (several clones in parallel, measured there at 0.22 ms a frame per clone) stays unbuilt; no agent question
measured so far needs batch replay.

What the check found instead, in the open tasks of the
[TTD index](../../emulator/design/debugger/time-travel-debug/README.md#open-tasks):

1. `find-last` (the replay path and `all`) and `reverse-continue all` are the only window replays that are not jobs:
   38.7 s and 58.5 s at 50k frames, synchronous, no cancel.
2. `memory-at` at a frame start and `memory-diff` read the piece store only and still pause a running machine.
   Outcome (RQ-2, 2026-11-27): fixed. `memory-at` at a frame start, `memory-diff` and `memory-series` hold the
   machine for the call only; a running machine runs on and a recording keeps recording, with the bytes the
   stopped recording holds (`ttdstorereads_test.cpp`). `memory-series` on a recording did not leave it alone
   before either: it paused the recording, and the machine running on continued it at the next instruction (a
   new baseline in a recording folder). D8 stays for a point inside a frame, a read that replays or restores,
   and, while recording, a parent branch's frames or a segment evicted to its file. `find-last` (also of a port
   or with the write journal) and `find-event` still park the machine and pause a recording: `find-last` counts
   back from the machine's position and D8 names it, `find-event` replays its hits.
3. Should a long window without pausing the machine be needed, the background path exists (`analysis trace` with
   its `kinds` / `pc` / `addresses` filter); raising its 3000-frame limit is cheaper than moving verbs.

The scripts and raw outputs of the run are not in the repository.
