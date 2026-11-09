# TTD branches: history that keeps every future

Status: **design** (2026-11-08): use cases decided by the owner, architecture, storage, surfaces, test strategy
and tools drafted; nothing built. PLAN #76 (branch part), the TTD v2 migration's deferred Phase 5, Step 2b.

Acting in the past (continuing a recording from an earlier moment, editing memory or registers there) today cuts
the recorded future away. A branch keeps it: the new line of history starts next to the old one, both stay
reachable, and the user (or an agent, or the analyzer) moves between them and drops what is not wanted.

This folder restarts the design from what was decided and built since the first what-if design
([../2026-09-29-model-what-if/design.md](../2026-09-29-model-what-if/design.md), 2026-09-29, written before the engine).

| Document | What it holds |
|---|---|
| [use-cases.md](use-cases.md) | Actors, use cases with scenarios, the owner's decisions Q-1...Q-11 |
| [architecture.md](architecture.md) | Branch model, identities and metadata, engine and controller per branch, two tiers, media tree, zero overhead |
| [storage.md](storage.md) | Session file encoding, recording folder, demultiplexing, save / load / join |
| [surfaces.md](surfaces.md) | Qt, actions, Stream Deck, controllers, every automation plane, external debuggers |
| [test-strategy.md](test-strategy.md) | Oracle, test cases per layer, concurrency, fuzzing, benchmarks, live verification |
| [tools.md](tools.md) | Python maintenance tools: tree, validate, extract, prune, join, reindex, gc, stats |
| This README | Scope, decisions in force, what exists, findings, stale texts, phases |

## Phases (proposal)

Each phase: tests first, then code; lands when its tests, B-0 (zero overhead) and its own benchmarks pass.

| Phase | Content | Proves |
|---|---|---|
| P0 | Fix F-1 (a resume no longer deletes the folder) and F-2 (deck readout shape); benchmark baselines B-0 / B-1 on master | independent of branches |
| P1 | Branch table, GUIDs, metadata, lineage, per-branch engine lines, fork at boundary and mid-frame, switch, delete; resume and edit in the past fork | T-B*, T-E*, T-C1-C7, F-2 (memory only), B-0, B-2, B-6, B-7 |
| P2 | Storage: branch streams, folder (b)+(c), save all / lineage, load, old-reader compatibility | T-F*, F-1, F-3, B-5, B-9 |
| P3 | Two tiers: eviction to disk, read back, history limit without trimming | T-C8-C10, B-3 |
| P4 | Media in the stream, version tree, write-through following the active moment | T-M*, B-8 |
| P5 | Automation planes, `branch-create` with run, prune, the analyzer's loop | T-A*, T-C12-C13, B-4, MCP live block |
| P6 | Qt, actions, Stream Deck, controllers | T-A5-A6, surface contract |
| P7 | Python tools | tools tests, F-4 |
| P8 | Stale texts updated (§5), recipes, command-interface | docs |

## 1. Scope

In scope: branches inside one recording session (one machine, one configuration), on every automation plane and
in the Qt UI and the control surfaces; saving and loading branched sessions. Comparing branches is out (owner 2026-11-08: no use case seen).

Out of scope here (kept apart, by owner decisions in force):

- **Forks to other machines** ("open this moment on other models", the what-if grid): the what-if design's §5-6.
  They need linked sessions, which the owner ruled out on 2026-10-05; revisiting that is a separate decision.
- **Loads in the past as branch points**: a snapshot load, a reset, an autostart and a machine change end the
  session (D42, owner 2026-10-06, final).
- **Merging branches**: branches are compared, never merged (what-if non-goal, unchanged).

Owner 2026-11-08: parallel machines (background branch builds, forks to other models), state transfers and
merging are all for later; the first design stays simple.

## 2. Decisions in force that shape branches

| Decision | Source | What it means for branches |
|---|---|---|
| No history is ever cut short; a resume in the past starts a branch (D7), an edit in the past starts a branch (D9), a seek while recording pauses it (D8, built) | engine-decisions.md, 2026-10-02 | The goal itself. D8 is built; D7 / D9 wait for this work (today: cut / end the recording) |
| Positions are `{branch, frame, T}` (D15) | engine-decisions.md | The engine type has the field; the controller's `TTDTimePoint` does not |
| History in memory is a ring of minute segments (default 5 minutes) or a growable list; no byte budget with eviction and reading back (D41) | owner 2026-10-04 | Replaces the what-if design's 64 MB linked blocks and least-recently-visited release of branches |
| **Reopened 2026-11-08 (owner): history older than the memory window goes to disk and stays browsable** | use-cases.md Q-3 | Replaces D41's "memory only, nothing read back" for the past beyond the window; FR-15 already says the files keep every segment, the code does not yet |
| Reset, autostart, snapshot load, machine change end the session (D42) | owner 2026-10-06, final | Removes "loads as timeline events / branch points" from the what-if design |
| No linked sessions; branches postponed until v2 is stable | owner 2026-10-05 | v2 is the only engine since 2026-10-30; forks stay out (§1) |
| Media go back through an undo log per slot (M3) | owner 2026-10-10 | The log is linear today: a write after a seek back drops the versions above; branches need it to keep them |
| Continuing live from the past of a session loaded from a file is postponed | coordinator §12, owner 2026-10-10 | A branch in a loaded session hits this; to be revisited with branches |
| What-if decisions of 2026-09-29: automatic two-word names, no truncation anywhere, frame-boundary fork | what-if design §12 | Still the owner's words; the fork point needs a second look (D8 pauses mid-frame, phase-5 TDD forks at `{frame, T}`) |

## 3. What exists today

- **Engine type** `TTDPosition {branch, frame, tInFrame}` (`core/src/debugger/ttd/engine/ttdtime.h:23-34`); the
  engine refuses any branch but 0 (`CaptureFrame`, `CheckpointIndexOf`, `TruncateAfter`).
- **Checkpoint parent** field (`TTDEngineCheckpoint::parent`, `timetravelengine.h:99`), always `index - 1`; one
  linear change vector, frame table and capture head per engine.
- **Session file**: `branch` (u16) in the part end and the index part table; stream ids 0x0200-0x02FF reserved for
  branches; a reader without branch support skips them and opens the trunk (`ttd.ksy:105-109, 262, 313`).
- **Piece store** with reference counts per version, shareable by several engines (D22; one test shares it).
- **Resume from the past** (`TimeTravelController::ResumeRecordingFrom` -> `ContinueRecordingAt` ->
  `TruncateTimelineAfter` -> `TimeTravelEngine::TruncateAfter`) drops after the cut: the input journal, markers,
  tool-edit payloads, bookmarks, coverage frames, the write journal and index, checkpoints, piece versions, segments,
  media change records, configurations, frame streams, events, RZX frames, bus and media journals. It runs as the
  coordinator's `TimelineCut` op; media go back through the undo log or ask (`media=ask|empty|keep`).
- **Edit in a paused recording** before its end stops the recording there (the edit is applied, not recorded,
  `BeginToolEdit`); while browsing without a recording, tool edits are refused by the coordinator policy.
- **Surfaces**: Qt "Rec From Here" (tooltip "Truncate future history and resume..."; the only dialog is about media),
  the action `ttd.resume_live` on Stream Deck (LIVE key, OPS dial press), ShuttlePRO K13, gamepad east, all sending
  `resume` without any confirmation; MCP `resume` says "history after that point was discarded".

## 4. Findings while surveying (outside branches, to fix apart)

- **F-1. The recording folder loses its past at a resume from the past.** `TruncateTimelineAfter` calls
  `DiscardShadowFiles()`, which deletes the whole folder, including the history before the cut; the next capture
  opens a new folder whose writer starts at the current last segment (`TTDRecordingWriter::Begin`), so the segments
  before it are never on disk again. A crash after a "Rec From Here" keeps only the last segment of what is still
  in memory.
- **F-2. Stream Deck readout path.** The deck's `show:` key `ttd.position.current.frame` matches the REST shape of
  the `ttd.position` readout; the WebSocket topic carries `frame` at the top level, without `current`.

## 5. Stale texts this work will update

When branches land (not before: they describe today's behavior correctly): command-interface.md `ttd resume`,
time-travel-debugging-tdd.md §0 / §4.2 / §8.3 / Q3, time-travel-ux.md §3.4, the TTD recipes ("truncates the
future"), the MCP `resume` text, the Qt tooltip, gdb-reverse-debugging-tdd.md, the ZX-Poly group resume.

Stale now, independent of the build: the what-if design and TODO (v1 names, W1 "after Phase 1", linked blocks,
loads as events, stream ids 10-13, parent-link forks), PLAN #76's status text, `target-architecture.md` §6.1 and
`migration-trajectory.md` §6 item 3 (linked blocks, superseded by D41), FR-24 ("until branches land v1 keeps
truncating"), D26 / D38 / D39 ("linked to its parent", replaced by the 2026-10-05 rule). They get a pointer here
once the owner has confirmed the use cases.
