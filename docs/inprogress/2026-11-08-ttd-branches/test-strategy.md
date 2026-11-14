# TTD branches: test strategy, cases, fuzzing, benchmarks

Part of [TTD branches](README.md). Status: P1-P2 cases and benchmarks built with them (`e0ac62a53`); P5's T-C12, T-C13, T-A3, T-A4 built 2026-11-13 (`TTDControl_BranchCreate_Test`, `WsHub_Test.TtdBranches*`, `McpTools_Test.TimeTravel_Branch*`, the MCP live block `test_branch_create_info_prune`), the analyzer's loop (`AnalysisL3_Test.ExperimentKeptAsABranch*`, `ExperimentWithPinsIsNotKept`) and B-4 (`BM_TTD_Branch_AnalyzerWorkload`); the rest come with their phases. Design 2026-11-08. Covers [architecture.md](architecture.md),
[storage.md](storage.md), [surfaces.md](surfaces.md), [tools.md](tools.md).

The bar: branching unused costs nothing measurable; branching used is exact (every recorded position of every
branch restores to the machine state it had) and fast. Tests are written before each step's code (TDD), with the
repository's rules (`core/tests/README.md`): about 10 ms a test, integration under 1 s, full variants behind
`TestTier::Long()`, no `sleep_for`, `TestWait`, scratch through `TestPathHelper`, turbo on boot-bound tests.

## 1. The oracle

Every engine and controller test that records uses one oracle: **while recording, hash the machine state at every
checkpoint and at chosen in-frame moments** (memory pages, registers, device state through the state registry the
byte-identity checks already use), keyed by `{branch, frame, T}`. A check restores the position and compares the
hash. A branch's positions before its fork are the parent's: the oracle stores them once and the check asks the
branch's lineage.

The model for the fuzzers (§4) is a plain in-test structure: the branch tree (parent, fork, end, meta), each
branch's recorded positions and hashes, the expected active branch and position.

## 2. Test cases by layer

### 2.1 Branch table and lineage (unit, no machine)

| Id | Case |
|---|---|
| T-B1 | create / lookup by GUID, name, local id; names unique among live branches, clash suffix |
| T-B2 | local ids never reused inside a session; the 65 535 limit refuses with the prune hint |
| T-B3 | lineage spans for depth 1, 2, 50; position -> owning branch at fork boundaries (`T` before, at, after the fork) |
| T-B4 | children in a frame range (fork marks), several forks at one position, sorted |
| T-B5 | filters: creator, run, tag, age, length, pinned; combination; empty result |
| T-B6 | delete rules: trunk refused, active lineage refused, pinned descendant refused without force, descendants removed |
| T-B7 | metadata update records replay to the same table (append-only log semantics) |
| T-B8 | without a second branch no index is allocated (allocation counter) |

### 2.2 Engine

| Id | Case |
|---|---|
| T-E1 | fork at a frame boundary; record on the branch; every position on both branches restores to its hash |
| T-E2 | fork mid-frame `{f, T}` for `T` = 1, a mid value, the frame's last T; the branch's frame `f` replays the parent before `T` then its own |
| T-E3 | fork from a fork (depth 3), fork from an ancestor at a position before a child's fork |
| T-E4 | switch to every branch, at its end, at `lastVisited`, at a given position; capture head rebuilt (a capture after the switch is byte-identical to one after a cold restore) |
| T-E5 | piece sharing: a branch that changes one byte costs one version; reference counts after delete return to the parent's; `LiveVersions()` back to the pre-fork value |
| T-E6 | XOR chain across the fork stays bounded by K = 50 |
| T-E7 | delete a branch with descendants: every version they alone held released; the parent restores unchanged |
| T-E8 | empty branch (fork, seek away) leaves no trace |
| T-E9 | per-branch journals: events, bus reads / writes / vectors, media reads, RZX frames, frame streams replay per branch |
| T-E10 | single-branch session: engine objects and capture path identical to master's (no `_lines` growth, `_active` constant) |

### 2.3 Controller

| Id | Case |
|---|---|
| T-C1 | resume in the past forks; the old future intact (positions, inputs, markers, bookmarks, coverage, write journal); the reply names both branches |
| T-C2 | resume at the branch end continues (no branch) |
| T-C3 | edit in the past (memory, register, port): forks, edit recorded as the first event, replay reproduces it |
| T-C4 | edit while Detached / Replaying: forks (coordinator policy changed), not refused |
| T-C5 | paused end per branch: running to the active branch's end continues recording on it |
| T-C6 | bookmarks per branch; listing lineage vs all; seek to a bookmark on another branch switches |
| T-C7 | lineage queries: `find-last`, `reverse-continue`, `reverse-step`, `find-event`, `cause-chain` cross the fork into the parent and stop at the session start; results identical to the same queries on a linear session recorded along that lineage |
| T-C8 | history limit: eviction from memory only, journals not trimmed, earliest seekable = session start |
| T-C9 | seek into an evicted segment reads it back; the read-back cache bounded; memory report counts it |
| T-C10 | eviction never drops a segment whose file is not complete |
| T-C11 | loaded session (read-only): fork from its past, the loaded branches unchanged, save joins both |
| T-C12 | `branch-create` with `run`: the turbo run records, the outcome readouts returned, the machine back where it was if asked |
| T-C13 | prune by run id leaves pinned branches and the active lineage |

### 2.4 Media

| Id | Case |
|---|---|
| T-M1 | floppy: write on branch A, other write on B; switching A/B/A shows each branch's tracks; base images restored at the session start |
| T-M2 | HDD write-through: the host file equals the active moment's content after every switch and seek (compare against a reference image built from the records) |
| T-M3 | write-protect toggles in branches |
| T-M4 | `HostWriteHold` during replay on a branch: nothing reaches the file until the replay ends |
| T-M5 | `Move` between two leaves of a deep tree writes only the changed units (counter) |
| T-M7 | insert / eject / discard / create while Detached or Replaying: refused with the hint, the session intact (O-1) |
| T-M6 | a medium inserted from outside the history: resume asks / follows the policy |

### 2.5 File and folder

| Id | Case |
|---|---|
| T-F1 | save / load round trip, all branches: tree, meta, every position's hash |
| T-F2 | **zero overhead, file:** a session without branches written by the branch build is byte-identical to master's (fixed UUID and time); the `testdata/ttd/v2/` fixtures load as before |
| T-F3 | save lineage: a linear file, loadable by the reader with branch support off, positions equal |
| T-F4 | branch-table damage: dangling parent, cycle, duplicate GUID, local id out of range, fork past the parent's end: refused with a note, the trunk still loads |
| T-F5 | **old reader:** a branched file through the reader with branch support compiled out loads the trunk, identical to a trunk-only save |
| T-F6 | folder: (b)+(c) layout written as it records; `branches.tdx` deleted -> rebuilt from the segment trailers, identical |
| T-F7 | crash at every record boundary of the writer (cut files) -> recovery keeps every complete part of every branch |
| T-F8 | resume in the past never deletes a file (F-1 regression) |
| T-F9 | delete / prune: tombstone appended before files go; a crash between them recovers without the branch |
| T-F10 | disk-full: the free-space warning, then the pause; nothing half-written loads |
| T-F11 | clean close deletes an unsaved folder; a saved one keeps the saved file |

### 2.6 Automation and surfaces

| Id | Case |
|---|---|
| T-A1 | every verb on every plane (CLI, Lua, Python, MCP, REST, WebSocket `call`): same results (the planes' shared test fixture) |
| T-A2 | replies without branches are byte-identical to master's (golden) |
| T-A3 | `ttd.branches` topic events in order: created, switched, updated, deleted; `ttd.position` carries the branch |
| T-A4 | MCP text answers: the branch named, list summarised past 20 rows, budgets kept (`*Mcp*` unit tests + `tools/verification/mcp/` live block) |
| T-A5 | surface readouts `ttd.branch.*` through REST and WebSocket the same shape (`ttd-surface-contract`, includes the F-2 case) |
| T-A6 | actions `ttd.branch_*` dispatch from deck, ShuttlePRO and pad profiles (action tests) |

## 3. Concurrency

| Id | Case |
|---|---|
| T-X1 | switch while the background writer has queued parts of the old branch: the parts land in the old branch's file, the new file opens after |
| T-X2 | WebSocket readers of `ttd.position` (seqlock) during switches: never a torn branch / frame pair |
| T-X3 | delete while a read back of the same branch is in flight |
| T-X4 | TSan run of the stress round (`tools/sanitize/stress_round.sh`) with branch verbs added |

Each follows the repository's concurrency-test rules: wait for the second thread to start, bound by both sides,
assert the race happened.

## 4. Fuzzing

| Id | Target | Method | Budget |
|---|---|---|---|
| F-1 | session file reader with branches | extend `ttdsessionfile_fuzz_test.cpp` to branched fixtures: flipped bits, cuts, size fields 0xFFFFFFFF, garbage, plus branch-table records damaged (§2.5 T-F4 kinds) | thousands of loads in about 1 s; long tier 100x |
| F-2 | operation sequences (model-based) | seeded random programs over a small machine (Pentagon 128K, turbo): record n frames, seek, step, resume, edit, switch, create with run, update, pin, delete, prune, evict, read back, save, load, media writes; after every op the invariants of §4.1 | 200 ops in under 1 s per seed in the default tier; the long tier runs many seeds for 60 s; a failure prints the seed and the shortest failing prefix (delta reduction) |
| F-3 | folder crash | F-2 programs with a kill point at a random writer record; recovery checked against the model's complete parts | default 20 kill points, long tier 1000 |
| F-4 | Python tools | `tools/ttd` pytest with the same damaged fixtures: never a traceback, the same verdict as the C++ reader (`expected.json`) | seconds |
| F-5 | sanitizers | F-2 and F-3 under ASan + UBSan and TSan (`tools/sanitize/configure.sh`) | the long tier, before landing each phase |

### 4.1 Invariants checked by F-2

1. Every recorded position of every live branch restores to its hash.
2. The tree equals the model (parents, forks, ends, meta, active branch).
3. The piece store's live versions equal the sum the model predicts (no leak after delete, no early free).
4. Memory report within FR-16's ±5 % of the real total; read-back cache within its bound.
5. Disk: every file the folder index names exists and matches its CRC; no orphan file after delete.
6. With one branch the run is indistinguishable from master's (same file bytes, same replies).

## 5. Benchmarks

All A/B runs interleave rounds and run at once (no quiet waits), on the TTD benchmark matrix
(`core/benchmarks/debugger/ttd/`, `tools/verification/ttd-bench/`, baselines in `testdata/ttd/bench/`). The A/A
bias measured before (1.8 %) is the noise floor; a result inside it is "no difference".

| Id | What | Pass |
|---|---|---|
| B-0 | **zero overhead**: master vs branch build, branching unused: `ttd_frame_overhead_benchmark`, `ttd_capture_benchmark`, the matrix (all configurations and workloads), memory per minute, file bytes | within the A/A noise; memory equal; file bytes equal (T-F2) |
| B-1 | seek / step / reverse on a single-branch session | within the A/A noise |
| B-2 | branch switch, memory-resident, Pentagon 128K and the heavy configuration | p99 no worse than today's seek p99 x 2 |
| B-3 | seek into an evicted segment (Q-3b) on SSD, idle and heavy workloads | measured; proposed target p50 ≤ 50 ms, p99 ≤ 250 ms |
| B-4 | analyzer workload: 1000 experiments of 200 frames from one point (create + run + read + prune) | throughput, memory, disk bytes, number of files; baseline cost per branch on disk |
| B-5 | demux (storage.md §4.1): open one branch in a folder of 1000 branches; layout (a) vs (b)+(c) | (b)+(c) kept if no worse than (a) |
| B-6 | lookups: position -> checkpoint at lineage depth 1 / 10 / 100; list by tag / creator over 10 000 branches | microseconds; flat in session length (PR-7) |
| B-7 | lineage queries (`find-last`, `reverse-continue`) at depth 1 vs 10 vs a linear session of the same length | ≤ 10 % over the linear session at depth 10 |
| B-8 | media `Move` between leaves, 1000 changed sectors | proportional to changed units |
| B-9 | save all branches / load / save lineage of a 30-minute session with 100 branches | measured; load of the active lineage's window ≤ 1 s (PR-8) |

Benchmarks join the CI gate (`*TTDBench*`) where they have a pass line; the rest are reported per phase in a
results document as the migration's phases were.

Results so far (2026-11-10, load 9-14, three repetitions):

| # | Result | Pass |
|---|---|---|
| B-2 | switch p99 0.90 ms (p50 0.78 ms) against a seek of the same distance p99 1.50-1.64 ms | yes |
| B-0 | against the merge base 6547a2f66, 10 interleaved rounds (load 11-16): frame without TTD +0.40 %, Gaming +0.14 %, Development +0.75 %, full-frame capture +0.21 %, all inside the 1.8 % A/A bias; the full matrix (46 cases, ABBA): every memory, heap, resident and per-device byte counter equal in all four runs, file bytes differ by at most 2 bytes exactly as base differs from base | yes |
| B-5 | one branch of 1000: (b)+(c) p50 4.94 ms, (a) 5.73 ms; 52 KB per branch | yes, (b)+(c) kept |
| B-6 | owner lookup 3.7 / 7.1 / 18.7 ns at depth 1 / 10 / 100, the same at 100x the session length; select by tag over 10 000 branches 45 us, by creator 79 us | yes |
| B-7 | find-last: linear 71.6 ms, depth 10 72.9 ms (+1.8 %); reverse-continue: linear 94 us, depth 10 69 us | yes |
| B-4 | 2026-11-13, load 155 (the wall clock inflated 3.5x by it; CPU time is the figure): 1000 `branch-create` calls of the analyzer's form (a poke, an input script, 200 frames, a read, `go_back`) from one point of a 300-frame trunk: 95 s CPU, about 95 ms CPU per experiment (0.48 ms a frame); wall p50 316 ms, p99 700 ms; memory 36 KB per branch; disk 81 KB and 1 file per branch; one prune by the run id: 16 s for 1000 branches (16 ms each), after it 0.38 MB, 1 file and 1 MB of memory stay over the trunk alone | measured (no pass line) |
| B-9 | save all 240 ms (16.7 MB), save lineage 221 ms (14.8 MB), load 233 ms | yes (load < 1 s) |

F-1 (`DamageNeverLoadsAnotherBranch`): 3000 loads of
damaged branched files in about 0.4 s, 60 000 in the long tier; it found create records with a valid CRC naming
another fork, now refused (storage.md §5.1).

## 6. Live verification

- `tools/verification/mcp/` gets a `branches` block (create, run, list, switch, prune) run by `run_all.py`.
- A WebAPI recipe in `.recipe/` for the analyzer's experiment loop, measured by its MCP / WebAPI calls (the agent
  UX roadmap's acceptance rule).
- `tools/verification/ttd-surface-contract` covers the new readouts and the F-2 shape.

## 7. Order

Each phase lands only when its tests and its benchmarks pass, with B-0 run on every phase that touches capture,
the engine or the file.
