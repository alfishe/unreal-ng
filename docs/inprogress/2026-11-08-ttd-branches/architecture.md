# TTD branches: architecture

Part of [TTD branches](README.md). Status: **design** (2026-11-08), from the owner's decisions in
[use-cases.md](use-cases.md) §5. Storage: [storage.md](storage.md). Surfaces: [surfaces.md](surfaces.md). Tests and
benchmarks: [test-strategy.md](test-strategy.md). Maintenance tools: [tools.md](tools.md).

## 1. Principles

1. **A branch is today's linear history, started at a fork.** Every structure the controller and the engine keep
   per session today (checkpoints in frame order, frame table, input journal, markers, bookmarks, write journal,
   coverage, bus / media journals, frame streams, events) stays a linear structure, **one per branch**. Branching
   adds a tree above them and a lineage view across them; it does not change the code that records, compresses or
   replays one line.
2. **Zero overhead when unused.** A session that never forks runs exactly today's code paths: no check per
   instruction, memory access, port access or frame, the same memory, and a session file byte-identical to the one
   today's writer produces (§8). The hot paths never read the branch id: it is a property of the structures they
   write into, chosen when the active branch is switched.
3. **Nothing is ever cut.** `TruncateAfter` stops being a user operation: a resume or an edit in the past forks.
   The only ways history leaves the session are deleting a branch (explicit, §5.5) and ending the session (D42).
4. **Explicit acts only** (Q-1): resume in the past, an edit in the past, `branch_create`. The analyzer and agents
   go through the same commands.
5. **Two tiers** (Q-3): memory holds the recent window, the disk holds the whole session (every branch), a seek
   reads either.
6. **Append-only on disk**: segments, branch records and media writes are appended; metadata edits and deletions
   are appended records too (storage.md).

## 2. Terms and identities

| Term | Meaning |
|---|---|
| Branch | one line of history: a fork position on its parent, then its own checkpoints and journals |
| Trunk | the session's first branch; never deleted, never renumbered (Q-7) |
| Fork position | `{parent, frame, T}` on the parent where the branch leaves it (Q-2: the exact moment) |
| Lineage | the branch and its ancestors, each up to the fork of the next: a list of spans |
| Active branch | the branch the machine is on; the only one that records |
| Branch end | the last recorded position of a branch; recording continues only there |
| GUID | 16 random bytes, the branch's identity everywhere: files, folders, automation, tools |
| Local id | u16, the branch's number inside one session (memory) or one file (the `branch` field of `part_end` / `index_part`); 0 = trunk |

Positions everywhere become `{branch, frame, T}` (D15). Inside the engine `TTDPosition` already has the field
(`engine/ttdtime.h:23-34`). The controller's `TTDTimePoint` (`ttdcheckpoint.h:49-62`) gains a `uint16_t branch`
(local id): bookmarks, markers and journals of one branch all carry the same value, so it is free in comparisons
inside a branch and decides only across branches. Automation addresses a branch by GUID, by name, or by local id.

**Frame numbers are shared along a lineage.** A branch forked at `{trunk, 1200, 5000}` records frames 1200 (its
second part), 1201, ... So a frame number means the same machine time on every branch of a lineage; two branches
may both have a frame 1300 (different content), told apart by the branch id.

## 3. Branch table

```text
struct TTDBranchInfo
{
    uint16_t id;                 // local id; never reused inside a session
    TTDGuid guid;                // 16 bytes
    uint16_t parent;             // kNoBranch for the trunk
    TTDPosition fork;            // on the parent
    uint64_t forkCheckpoint;     // the parent's last frame-boundary checkpoint at or before fork
    uint64_t endFrame, endT;     // branch end
    TTDBranchState state;        // Live, Deleted (tombstone until the files drop it)
    TTDBranchMeta meta;          // §3.1
    uint32_t firstSegment;       // into the segment list (§6)
};
```

### 3.1 Metadata (Q-4)

| Field | Set by | Notes |
|---|---|---|
| `name` | caller or default | optional; default the cause and a counter: "resume 3", "poke #5C3A", "attempt 7" (siblings at one fork position); unique among live branches, a clash gets " (2)" |
| `createdHost` | engine | host time, microseconds since the epoch |
| `createdBy` | caller | `{surface, client, run}`: `qt`, `deck`, `controller:<id>`, `gdb`, `dezog`, `mcp:<client>`, `webapi:<client>`, `cli`, `lua`, `python`, `analyzer`; `run` = the analyzer's or a script's run id |
| `reason` | engine + caller | the act: `resume`, `edit-memory {addr, bytes}`, `edit-register {reg, value}`, `edit-port {port, value}`, `create` with the caller's text |
| `tags` | anyone | a set of short strings; filters (Q-5) and pruning (tools.md) use them |
| `note` | anyone | free text, as agent bookmarks have |
| `pinned` | user | kept by pruning; delete asks for `force` |
| `lastVisited` | engine | the position the user left the branch at; a switch returns there |

The table is small (about 200 bytes a branch plus strings). Live branches per session are limited to 65 535 by the
u16 local id; `branch_create` refuses past it with a message naming `branch_prune`.

### 3.2 Indexes (fast lookup when used)

Built incrementally, nothing for a session with only the trunk (one entry, no index allocated until the second
branch):

| Query | Structure | Cost |
|---|---|---|
| by GUID / by name | two hash maps | O(1) |
| children of a branch | per branch a vector of child ids, sorted by fork position | O(log c) to find the children in a frame range (fork marks on the visible part of the timeline) |
| lineage of the active branch | cached span list `[(branch, fromPos, toPos)]`, rebuilt on switch | O(depth) once per switch; position lookup O(log depth) |
| by creator / tag / run / age / length | linear scan over the table | 65 535 entries at most; a few hundred microseconds |
| "which branches pass frame f" (the tree view's vertical slice) | per branch `[forkFrame, endFrame]`; an interval list sorted by `forkFrame` | O(log n + k) |

## 4. Engine (`TimeTravelEngine`)

### 4.1 Per-branch state

Today one engine holds one line: `_checkpoints` (deque), `_frames` (`TTDFrameTable`), `_changes`, `_segments`, the
capture head (`_live`, `_inMemory`, `_sinceSnapshot`, `_lastSnapshot`, `_deltaBase`), `_events`, `_busReads`,
`_busWrites`, `_busVectors`, `_mediaReads`, `_rzxFrames`, `_configs`, frame-stream copies
(`timetravelengine.h:585-702`).

They split into:

- **Shared by all branches:** the piece store (`TTDPieceStore`, reference counted per version, already shareable,
  D22), the reference tables (`TTDRefTables`), the stream registry, the device table, the media slot table.
- **Per branch** (`TTDBranchLine`): checkpoints, frame table, change records, segments, events, bus / media
  journals, RZX frames, configurations, frame-stream copies. Every one is the existing class, unchanged.
- **Per engine, for the active branch only:** the capture head. A switch rebuilds it from the target position's
  checkpoint (the code `TruncateAfter` runs today at `timetravelengine.cpp:1025-1059`, kept as `RebuildHeadFrom`).

The engine holds `std::vector<std::unique_ptr<TTDBranchLine>> _lines` and a pointer `_active`. Capture writes
through `_active` exactly as it writes through `this` today; with one branch the vector has one element and the
pointer never changes.

### 4.1a As built (P1, 2026-11-08)

- `TimeTravelEngine::BranchLine` holds the per-branch objects listed above; `_lines[id]` (the trunk is 0, ids never
  reused) and `_line`, the line read and written now. A session that never forks has one line; B-0 measured the
  indirection at +0.1 % to +0.8 % per frame against master, inside the 1.8 % A/A bias (P1+P2, 2026-11-10).
- **A line starts with a copy of its fork checkpoint.** `ForkAt(index, cut)` gives the new line checkpoint
  `index` again: every region's full table (the checkpoint's own, or the last one derived with the changes since:
  blocks and versions shared, nothing decoded or stored), its change records, and the fork frame's records up to
  the cut (bus reads, writes, vectors, media reads, events, RZX frames, the settings in force, the frame's stream
  copies), journal cursors rebased to 0. The line's own part is therefore self-contained: a restore or a replay at
  or after the fork never reads the parent. Indexes stay the lineage's: the line's first checkpoint has index
  `index` (`cpBase`), as a ring's first held one does. The frame of the fork belongs to the new line at checkpoint
  level; positions `[{f, 0}, {f, T})` replay identically on both lines, so a mid-frame fork needs no mixed event
  source (§4.3 below is superseded by this).
- **Viewing and capturing are apart.** `ViewLine(id)` moves the reads (checkpoints, records, journals) to another
  line at no cost: the capture head stays, live memory stays known (versions are the store's). A seek into the
  part of the active lineage an ancestor owns views that ancestor; a resume there forks from it, which is the
  owner rule of §4.5 without a lookup. The capture head is rebuilt only when a capture runs on a line it was not
  built for (from that line's last checkpoint) or at a fork (from the fork checkpoint).
- Journal modes pass to the line made current: recording stays recording, a replay stops.

### 4.2 Checkpoint parent and sharing

`TTDEngineCheckpoint::parent` (`timetravelengine.h:99`, today `index - 1`) becomes a global checkpoint reference:
a branch's first checkpoint has the parent's fork checkpoint as parent. Piece versions are shared by reference:
the branch's first checkpoint keeps every piece of the fork checkpoint (`AddRef`) and differs only where the
branch changed memory. XOR chains (K = 50) continue across the fork. A retry that changes little therefore costs
little memory (UC-2).

### 4.3 Fork inside a frame (Q-2)

A fork at `{parent, f, T}` with `T > 0`:

- `forkCheckpoint` = the parent's checkpoint at frame `f` (its frame-boundary state).
- The branch's frame `f` replays the parent's frame `f` from its boundary up to `T` using the **parent's** inputs
  and events with time `< T`, then the branch's own events from `T` on. The engine's in-frame replay
  (`ReplayWithinFrame`) takes an event source; the source for a branch's fork frame is "parent before T, then own".
- The branch's own first checkpoint is at the boundary of frame `f + 1`.
- A fork at `T = 0` is the degenerate case: no parent events used.

### 4.4 Branch switch

`SwitchBranch(target, at)`:

1. Pause the recording if one runs (D8 path, `PauseRecordingForBrowsing`).
2. Remember `lastVisited` of the branch being left.
3. Resolve `at` (default: the target's `lastVisited`, else its end) through the target's lineage.
4. `_active = target`; rebuild the lineage cache; `RebuildHeadFrom(checkpoint at or before at)`.
5. Seek (restore + in-frame replay), state Detached, the paused end = the target's end (D8 semantics per branch:
   running to the end continues recording on the target).
6. Media follow (§7), publish `ttd.branches` and `ttd.position`.

Cost: one checkpoint restore plus the head rebuild. The rebuild decodes the pieces of one checkpoint (as the
truncate does today); measured as B-2 in test-strategy.md.

### 4.5 Resume and edit in the past

`ResumeRecordingFrom(p)` (`timetravelcontroller.cpp:3991`) becomes:

- `p` = the active branch's end: continue (no branch), as today's `ResumeRecordingLive` / paused end.
- otherwise: `owner` = the lineage branch that holds `p`; `CreateBranch(owner, p, reason=resume)`; switch to the
  new branch at `p`; recording continues there. The old future stays on `owner` (and its other children).

`ContinueRecordingAt`'s nine truncations (`timetravelcontroller.cpp:4076-4104`) disappear from this path: the new
branch's journals start empty. `TruncateTimelineAfter` and `DiscardShadowFiles` are no longer called by a resume
(this also removes finding F-1).

`BeginToolEdit` in the past (`timetravelcontroller.cpp:2757-2785`): today it stops the recording; it becomes
`CreateBranch(owner, p, reason=edit-...)`, switch, then the edit is recorded as the branch's first event (D9,
`EndToolEdit` already stores the bytes). The coordinator policy (`machinepolicy.cpp:102-103`) allows ToolEdit in
Detached and Replaying modes as "forks a branch" instead of refusing.

### 4.6 Empty branches

A branch exists from the moment it records an event or a checkpoint. A resume in the past followed by a seek
elsewhere before anything was recorded leaves nothing: the branch is removed at once, without a notice. Otherwise
branches are never removed automatically (no age or length threshold: Q-3 keeps all history on disk).

### 4.7 Lineage queries

Analyses that walk history (`find-last`, `reverse-continue`, `reverse-step`, `find-event`, `cause-chain`,
`backtrace`, coverage, `memory-diff` between two moments of the lineage) run on the active lineage: the active
branch from the query point back to its fork, then the parent from that fork back, and so on. Each step is the
existing per-branch query, bounded by the span. Depth is small in practice (a retry chain); the cost is
O(depth) hops plus today's per-line cost. A query option `scope=all` (later, not in the first delivery) would run
over every branch.

### 4.7a As built (P1, 2026-11-09)

- **Time on the lineage.** `GlobalT`, `GlobalTInFrame` and `TimePointAt` read the frame table of the lineage line
  that holds the frame or the time (`LineageFrames`, `LineageFramesAtT`): frames of different lengths (Sprinter
  320/312 lines, turbo) stay exact across a fork. `FirstBarrierBetween` splits `(from, to]` by line, oldest first.
- **Two ways a query walks the lineage.**
  - Whole-line walkers (`FindLastAccess`, `FindAccesses`, `EnumerateM1InRange`) keep their body as `...OnLine`, and
    `WalkLineageBack` runs it on each line from the query point back, newest line first. Each line's part ends at
    its first checkpoint; `changes_only` candidates settle their old byte there, so the parts join as they come.
  - Index walkers (`find-event`, `cause-chain`, `backtrace`, `explain-frame`, `probe-frames`) go over engine
    checkpoint indices of the active lineage (`LineageFirstIndex` .. `LineageEndIndex`). `ViewLineageIndex` and
    `ViewLineageCheckpoint` view the deepest lineage line with `LineFirstCheckpoint <= index` before the index is
    read; `LineageEngine` wraps the engine calls a query makes, with the same names.
  - Frame walkers (`replay-check`, coverage, `cpu-view`, frame cache) view the owner of each frame
    (`OwnerOnLineage`). Coverage is per line from the fork on: `CoverageOfFrame`, `LineageCoveredRange`.
  - Every query leaves through `LineageViewGuard`: the viewed line is the owner of the machine's position again.
- **Replay and the view.** `RestoreCheckpointForReplay` views the line that holds the checkpoint it is given (by
  address), so a query that holds checkpoint references across lines replays each on its own line. A replay does not
  follow the lineage at a frame boundary (`OnFrameBoundary`: `!_inReplayMode`); a live run on a viewed ancestor does.
- **Bookmarks** carry their branch in `TTDBookmark::branch` (not in `TTDTimePoint`). `GetBookmarks` and the timeline
  readout list the active lineage's; `GetAllBookmarks` lists every branch's. A seek to a bookmark off the lineage
  switches to its branch; deleting a branch drops its bookmarks.
- **An empty branch leaves no trace (§4.6).** A resume or an edit forks at once. If the user then leaves the new
  tip before anything was recorded on it (`TipIsEmpty`: one checkpoint, no inputs or markers since the fork, its end
  at the fork), `SeekTo` to another position and `SwitchBranch` to another branch delete it (`DropEmptyTip`):
  the parent becomes the active tip again, its end is the paused end, the id is not reused. A seek to the fork
  itself keeps the branch.
- **Media in the recorded past are refused (O-1).** In Replaying and Detached, media insert, eject, discard and
  disk create are refused by the machine policy (`RefusalText`: go back to the branch end, or resume recording
  first, which forks). Recording keeps recording them; Live allows them.
- **Fuzz (F-2).** `TimeTravelController_BranchesFuzz_Test` runs random programs (run, seek, edit in the past,
  switch, delete off the lineage) against a model of the tree that keeps every frame's exact position and RAM
  hash, checks the table after each step and visits every frame of every live branch at the end. Memory only;
  1 seed by default, 20 seeds x 200 steps in the long tier.
- **Not lineage-wide (P1):** `BuildWriteJournal` builds the write journal of the viewed line only (journals are per
  line); `ttd.info` coverage figures are the viewed line's.

## 5. Controller (`TimeTravelController`)

### 5.1 Per-branch journals

`_timeline`, `_inputJournal`, `_externalEvents`, `_toolEditPayloads`, `_bookmarks`, `_coverageIndex`,
`_writeJournal`, journal segments (`timetravelcontroller.h:1788-1953`) move into a `TTDControllerLine` per branch,
pointed at by `_active` like the engine's. Bookmarks carry the branch in their time point; listing defaults to the
active lineage, `branch=all` lists all.

### 5.2 States

`TTDSessionState` stays `Idle / Recording / Detached`. What changes:

| Today | With branches |
|---|---|
| Detached, not paused, running past the session end: auto-pause | the same, per active branch end |
| Detached, paused, reaching `_pausedEnd`: continue recording | the same, `_pausedEnd` = the active branch's end |
| Resume before the end: truncate, Recording | fork, switch, Recording |
| Tool edit in a paused recording before its end: recording stops, edit unrecorded | fork, switch, edit recorded |
| Tool edit while Detached / Replaying: refused | fork, switch, edit recorded |
| Loaded session: read-only, resume truncates (and is postponed for live continue, 2026-10-10) | read-only branches stay; resume / edit forks a new branch (Q-11) |

### 5.3 History limit and memory

`EnforceHistoryLimit` (`timetravelcontroller.cpp:4268-4286`) today drops segments from memory and, for the black
box, deletes the files. With two tiers it only **evicts from memory** (the files stay, Q-3a); `SyncTimelineFront`
no longer trims journals, markers and bookmarks, because the history still exists.

Memory holds:

- the most recent `windowFrames` of capture, over all branches, in capture order (the black box's default 5 min);
- the active lineage's last `windowFrames` (a switch to an old branch reads its segments back);
- a read-back cache of segments loaded for seeks (default 4 segments, LRU, its own counter in the memory report).

A segment leaves memory only after the writer has flushed it (its file is complete); otherwise eviction waits.

### 5.4 Read back from disk

A seek whose checkpoint is in an evicted segment loads that segment's parts from its file (storage.md §4), into
the read-back cache, then restores. The segment is self-contained (baseline + deltas), so one file read suffices.
Target in test-strategy.md B-3 (Q-3b is measured, not assumed).

### 5.5 Delete

`DeleteBranch(b, force)`:

- the trunk and every branch of the active lineage are refused (switch first);
- pinned descendants are refused without `force`; the reply lists them;
- removes `b` and its descendants: their lines are released (piece references dropped; shared pieces stay for the
  parent), their files deleted from the recording folder, a tombstone appended to the folder index (storage.md).

`PruneBranches(criteria, dryRun)`: the same over a filter (creator, run, tag, age, length, unpinned, not on the
active lineage). Dry run is the default on every plane.

## 6. Segments with branches

A segment belongs to one branch. A branch starts a new segment at its first checkpoint; its first segment's
baseline in memory is the fork checkpoint's piece references (no copy). On disk every segment file is
self-contained (storage.md §3), so the first segment of a branch writes a baseline. The cost of that baseline for
many short branches (UC-5) is measured (B-4) before the layout is frozen; the fallback is a first segment that
depends on the parent's segment file (one more file read on a seek).

## 7. Media (Q-10)

Today `MediaUndoLog` (`emulator/media/mediaundolog.h`) keeps, per version, the before-image of each changed unit
(sector, track, write-protect switch), in memory, 64 MB, oldest dropped, and a write after a seek back drops the
versions above (`DropAbove`).

With branches:

- **Media writes are history records.** The writes a frame makes (unit, index, after-image) and the unit's
  base image on its first write in the session go into the branch's stream, appended, on disk with the rest.
- **Versions form a tree**: a version has a parent version; a branch's first version's parent is the version at
  the fork. `DropAbove` goes away.
- **Move(head, target)** finds the common ancestor of the two versions and writes, for every unit changed on either
  path since it, the target lineage's latest image of the unit (or the base image). A per-unit index (unit -> the
  versions that wrote it, per branch) keeps this proportional to the units changed, not to history length.
- `MediaUndoLog` stays as the in-memory cache of that history (its budget only limits the cache; a miss reads the
  stream).
- **The host image file** (write-through) holds the active branch's current moment, as a seek sets it today;
  during a replay `HostWriteHold` keeps guest writes in memory as today.
- The resume's `media=ask` decision (`ttdcontrol.cpp:1135-1250`) is needed only when a medium cannot be set to the
  fork moment (a medium inserted from outside the history); otherwise the medium always follows.

## 8. Zero overhead: what "unused" guarantees

| Area | Guarantee when the session has only the trunk | Checked by |
|---|---|---|
| Per instruction, memory, port, frame | no new code on the path: capture writes through the same objects | B-0 frame-overhead and matrix A/B, within the A/A noise |
| Memory | one branch line (the objects that exist today), one table entry; indexes not allocated | memory report equal to master's |
| Session file | no branch table, no 0x02xx streams: byte-identical to master's writer for the same session (UUID and time aside) | golden test against master's output and the `testdata/ttd/v2/` fixtures |
| Recording folder | the same files, the folder index not written until the second branch | folder golden test |
| Seek, step, reverse | lineage of one span; the lookup is today's | B-1 seek benchmarks equal to master's |
| Automation replies | positions gain `branch` only when the session has branches; otherwise today's replies, byte for byte | reply golden tests |

## 9. What stays out (owner 2026-11-08)

Parallel machines (background experiments, forks to other models), state transfers between branches, merging,
comparing branches, promote. The design leaves room: branches have GUIDs and the piece store is shareable, so a
second engine over the same store could later hold a branch, but nothing here depends on it.

## 10. Open points (owner)

- **O-1 decided 2026-11-08 (owner): media insert / eject / discard / disk create while browsing (Detached,
  Replaying) is refused with a hint** ("go back to the branch end or resume here first"); the history is never
  ended by it. Today it ends the session outside a recording (`MediaChanged` -> `EndSessionFor`); the coordinator
  policy (`machinepolicy.cpp`) changes these rows to Refused for Detached and Replaying. While recording it stays an
  event at the next frame boundary.
- **O-2. Snapshot load in the past** stays a session end (D42); nothing proposed.
