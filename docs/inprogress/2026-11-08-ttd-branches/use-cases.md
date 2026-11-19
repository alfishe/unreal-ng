# TTD branches: use cases and scenarios

Part of [TTD branches](README.md). Status: **decided** by the owner (2026-11-08): Q-1...Q-11 below.

Every later decision (when a branch starts, how long it lives, what a file keeps, what the UI and the deck show)
follows from which of these cases are served first and how. Each case lists the actor, the trigger, the scenario
step by step as the user would live it, what it needs from the design, and its open points. §4 ranks them; §5 lists
the questions for the owner, one decision each.

Terms: **branch** = one line of history inside a recording session; **trunk** = the first line; **fork point** =
where a branch leaves its parent; **lineage** = a branch with its ancestors back to the session start; **active
branch** = the one the machine is on. A branch shares everything before its fork point with its parent.

## 1. Actors

| Actor | Drives TTD through | Cares about |
|---|---|---|
| Player | keyboard, gamepad, Stream Deck, black box always on | getting back to a moment and trying again; never losing a good run; no dialogs |
| Developer / debugger user | Qt debugger, keyboard, ShuttlePRO, GDB / DeZog | what-if edits, comparing behavior, exact replays |
| Reverse engineer | Qt, WebAPI / MCP scripts | experiments on a game, documenting each one, comparing outcomes |
| AI agent | MCP, WebAPI, WebSocket | many cheap experiments from one moment, compact answers, cleanup |
| Automatic analyzer | the analysis pipeline, in process or over WebAPI | branches per experiment, created by explicit commands; cleanup after the run |
| Speedrunner / TAS author | gamepad, deck, scripts | many attempts of one section, keeping the best, assembling a route |
| Presenter / teacher | Qt, deck, video export | showing two outcomes side by side, a clean history to share |

## 2. Use cases

### UC-1. Undo an accidental "Rec From Here" (safety net)

Actor: any. Today: the future is gone the moment `resume` runs, from Qt, the deck's LIVE key, ShuttlePRO K13 or a
gamepad's east button, with no confirmation.

1. The user records 3 minutes, scrubs back 40 s to look at something, presses LIVE (or Rec From Here) by habit.
2. Recording continues from that moment; the user plays on for 10 s, then notices the old ending is wanted.
3. The previous future is still there as a branch: one action ("previous branch", a key, a menu item, MCP
   `branch_switch`) puts the machine on it, at the position it had when the user left it, or its end.
4. The user goes on recording at the end of the old branch, or drops the new one.

Needs: resume keeps the old future; a notice naming what happened ("the previous future is kept as branch 1");
one-step switch back; no confirmation dialog anywhere (the dialog is the problem branches remove).
Open: is the old future worth keeping when it is very short (a few frames)? (Q-3)

### UC-2. Try again from a moment (player retry)

Actor: player, black box on. The most frequent case by far.

1. The player dies at 2:10, rewinds 5 s with the dial or LT, and presses play / LIVE (input while browsing
   stays refused, Q-1).
2. A branch starts at that moment; the player survives this time and plays on for minutes.
3. Later rewinds and retries create more branches from other points.

Needs: branch creation without any extra step; very low cost (a retry that changes little shares nearly all
memory); old attempts must not clutter the UI or memory: they age out unless kept (Q-3); the 5-minute window
applies to the history as a whole, not per branch.
Open: does an ordinary player ever want to see the old attempts? If rarely, the default view shows only the active
lineage with a small "+2" badge (Q-5).

### UC-3. Practice a section (many attempts from one point)

Actor: speedrunner, player. Like UC-2, but the same start point on purpose.

1. The user bookmarks a moment ("boss start") and practices: play, fail, jump to the bookmark, play again, 20 times.
2. Every attempt is a branch from the same fork point; attempts carry automatic numbers ("attempt 7").
3. The user marks the best attempt ("keep" / pin, or rename), drops the rest in one action.
4. Optional: compare two attempts (time to a goal, score, which one is faster), see UC-6.

Needs: many sibling branches at one fork point; bulk delete ("drop unpinned siblings"); a cheap jump back to the
fork point; attempts listed with their length and an outcome readout (a watch, a score address).
Open: should attempts at one point show as numbered siblings of one fork rather than separate names? (Q-4)

### UC-4. What-if edit in the past (debugger)

Actor: developer, reverse engineer. Today: refused while browsing; in a paused recording the edit ends the
recording.

1. The user stops at frame 1200 where a sprite glitches, seeks back to 1150, pokes a different value into the
   sprite table (or changes a register, flips a port bit).
2. The edit starts a branch at that moment, recorded as its first event (D9), named for it ("poke #5C3A").
3. The user runs on: the glitch is gone. They switch to the trunk at the same frame and compare (UC-6).
4. They keep both, add notes (agent bookmarks already carry notes), and export the finding.

Needs: an edit in the past = branch + edit event; the same through Qt, GDB / DeZog, MCP and the CLI; branch names
that say what made them.
Open: does an edit while browsing start the branch at once, or only when the machine runs on (an edit alone, then a
seek elsewhere, would leave a one-frame branch)? (Q-1)

### UC-5. Agent experiments from one moment (search)

Actor: AI agent via MCP / WebAPI. The case that needs branches to be cheap and scriptable.

1. The agent stands at a moment (a bookmark, a found event), and runs N experiments: for each, create a branch there,
   feed it an input script (hold fire, jump at frame k, a POKE), run M frames, read the outcome (a watch value, the
   screen hash, `diff` against the trunk).
2. It ranks the branches, keeps the best one or two with notes, deletes the rest.
3. It reports with positions that name branches (`{branch, frame}`), so a human can open each one.

The automatic analyzer (the analysis pipeline) is an actor of this case too: it creates branches while it analyses,
and like every other caller it does so by an explicit command (`branch_create`, `resume`, an edit in the past),
never implicitly (owner 2026-11-08, consistent with Q-1).

Needs: `branch_create {at, name?}` without running the machine live; input scripts as events in a branch; run to a
frame on a chosen branch headless; `diff` across branches; bulk delete; answers within the answer budgets; every
plane (CLI, Python, Lua, MCP, REST, WebSocket topics for branch changes).
Open: is input fed into a branch from a script a recording (live machine) or a construction (events written, then
replayed)? (Q-6)

### UC-6. Compare two branches (out: owner 2026-11-08, §3)

Actor: all but the player.

1. The user picks two branches (or two attempts) and asks for the comparison.
2. They get the first divergence (frame, T, what differed first: input, edit, memory, port), then a side-by-side
   view: both pictures at the same frame, the memory and register differences (`diff` already compares moments),
   and per-frame difference strips.
3. They scrub both together (one playhead by frame number; branches share frame numbers along a lineage).

Needs: frame numbers shared along a lineage (a branch's frame 2000 is comparable with the trunk's 2000); `diff`
taking two `{branch, frame}`; a dual view in Qt; a clip export of two branches side by side.
Open: is a synchronized dual view in the first delivery, or `diff` and switching only? (Q-8)

### UC-7. Assemble the best route (TAS-like)

Actor: speedrunner, TAS author.

1. Over a session the author builds a route: from the best attempt of section 1, attempts of section 2 branch off,
   and so on; the best lineage runs through several branches.
2. They promote that lineage to be the main line (the trunk), and drop the rest.
3. They export the main line: a linear `.ttd` session, a clip, possibly RZX (RZX export is not built yet).

Needs: promote a lineage; delete everything else; export of one lineage as a linear session that older readers open.
Open: is promote wanted, or is "save this lineage as a new session" enough? (Q-7)

### UC-8. Save and reopen a branched session

Actor: all. Today a save writes one line; the recording folder writes segment files as it records.

1. The user saves the session ("Export .ttd", `dump`, the deck's SAVE TTD): the file keeps every branch, with names,
   notes and bookmarks per branch.
2. They reopen it later (or a colleague does): the branches are listed, the active one is the one that was active.
3. An older build opens the same file as its trunk only (the reserved 0x02xx streams are skipped).
4. Optionally: save only the active lineage, as a linear session.

Needs: the file layout for branches (reserved stream range; part ends and index already carry a branch id); the
recording folder written as it records with branches (today a resume from the past deletes the folder, finding F-1);
crash safety for every branch, not only the active one.
Open: does the black box's folder keep all branches or the active lineage? does the default save keep all branches?
(Q-9)

### UC-9. Black box with branches (always recording)

Actor: player, any user with the black box on (5-minute window by default).

1. Recording runs all the time; the user rewinds and plays on many times an hour (UC-2).
2. The window moves on: the oldest minute segments drop. A branch whose every frame is older than the window is
   gone with them; a branch that is pinned or active is kept (phase-4 TDD's rule).
3. Memory stays bounded by the window as today, now counted over all branches.

Needs: segment dropping that understands branches (a dropped segment of the trunk can be the parent of a live
branch: its pieces stay while referenced, the reference counts already do that); the window counted in frames of all
branches or of the active lineage (Q-3).

### UC-10. Present two outcomes (teaching, streaming)

Actor: presenter.

1. The presenter prepares a session with two branches ("without the fix", "with the fix") and names them.
2. On stream they switch between them from the deck (a key per branch or a "next branch" key), the strip shows which
   one is on and where.
3. They export a side-by-side clip of both.

Needs: switching from a control surface with the branch name on the key or strip; the clip export over two branches.

### UC-11. Debugger sessions with reverse execution (GDB, DeZog)

Actor: developer in an external debugger.

1. The user reverse-steps in GDB / DeZog to before a bug, changes a register, continues.
2. Today the continue from the past truncates (GDB) and DeZog relies on truncate and rewrite.
3. With branches the continue starts a branch; the debugger keeps its model (one linear history it sees), the
   emulator keeps the old future reachable for later.

Needs: the external debuggers stay on the active lineage, unchanged in protocol; a notice in the debugger console
(GDB `monitor`) naming the branch.

### UC-12. Media in branches (saving a game to disk in a branch)

Actor: player, developer with a disk or a write-through HDD image.

1. On branch A the game saves to the floppy; the user switches to branch B, which did not save, and saves something
   else; switching back to A must show A's disk content.
2. The media undo log is linear today (a write after a seek back drops the versions above): with branches, a
   medium's versions form a tree like the history.

Needs: media versions per branch (the undo log keeps the versions of other branches instead of dropping them);
write-through images: the file on the host holds one line only (Q-10).

## 3. Cases left out, and why

| Case | Why not here |
|---|---|
| Open this moment on other machines (what-if forks, grid, verdict) | linked sessions ruled out (owner 2026-10-05); a separate decision |
| A snapshot / tape load or a reset in the past as a branch point | D42 (owner 2026-10-06, final): these end the session |
| Comparing branches (UC-6: dual view, `diff` across branches) | owner 2026-11-08: no use case seen yet; experiments read their own outcomes |
| Merging two branches | no meaning for machine state; compare instead (what-if non-goal) |
| Branches in a session loaded from a file (continue live from its past) | postponed by the owner 2026-10-10 (coordinator §12); with branches it may be cheaper to allow: Q-11 |
| Branches across ZX-Poly's four machines | group control over a shared store (D22) is deferred; the group would branch all four together later |

## 4. Value and order (proposal)

| Case | Value | Cost on top of the core | Proposed |
|---|---|---|---|
| UC-1 safety net | every user who ever pressed LIVE by habit; removes the only silent data loss in TTD | none: it is the core | first |
| UC-2 retry | the most frequent player action | memory policy for old attempts | first |
| UC-8 save / reopen | without it branches vanish at exit; F-1 shows the folder is fragile already | file layout, folder writer | first |
| UC-9 black box | TTD's default for players | window over branches | first |
| UC-4 what-if edit | the debugger's main reason for branches | edit events (exist), edit in the past | first |
| UC-5 agent search | the agent-TTD roadmap's direction; measurable by MCP calls like the earlier features | headless branch create / run | second |
| UC-6 compare | no use case seen (owner 2026-11-08) | - | out |
| UC-3 practice | players, speedrunners | sibling grouping, bulk delete | second |
| UC-10 present | deck users, streams | surface keys, clip of two branches | second |
| UC-11 GDB / DeZog | developers | notices only | second |
| UC-12 media | correctness of UC-2 / UC-4 with disks | undo log as a tree | first for floppies in memory; write-through later |
| UC-7 route | TAS authors | "save this lineage" (no promote, Q-7) | later |

The first delivery would then be: resume and edit in the past start a branch; list, switch, rename, pin, delete;
the window and memory over branches; the session file and the recording folder with branches (memory window plus disk, Q-3); the Qt panel and the deck showing the active branch and the count; every automation plane.

## 5. Questions for the owner (one decision each)

- **Q-1. What starts a branch.** (a) Only explicit acts: `resume` in the past and a tool edit in the past (D7 /
  D9, today's verbs); input while browsing stays refused. (b) Also live input while browsing (the reference UI and
  the what-if design: pressing a key in the past forks at once). (b) makes UC-2 one step shorter; (a) keeps
  browsing safe from a stray key.
  **Decided 2026-11-08 (owner): (a), explicit acts only.** Live input while browsing stays refused; a retry
  (UC-2) is a press of play / LIVE.
- **Q-2. Fork point.** The frame boundary before the act (the what-if decision 2026-09-29, the new branch replays
  the frame's start), or the exact moment `{frame, T}` (the phase-5 TDD; D8 already pauses mid-frame).
  **Decided 2026-11-08 (owner): the exact moment `{frame, T}`.** The part of the frame before the act is shared
  with the parent; the engine needs a fork point inside a frame.
- **Q-3. Lifetime of old branches.** In the window like everything (dropped when older than it), plus: unnamed
  short branches (under N seconds) dropped at once / never; pinned and named branches kept beyond the window?
  **Owner direction 2026-11-08: two tiers, memory and disk.** Everything older than the black box's memory window
  is kept on disk, and a seek browses both the memory ring and the files on disk, for every branch. This reopens
  D41 / D28 (2026-10-04 struck "evicted and read back from the file on a seek", PR-8 replaced the 20 ms seek into
  evicted history). What exists: one self-contained file per minute segment (a baseline each), written as it
  records, with the branch id in its part ends; what is missing: the black box keeps deleting its oldest files
  (`EnforceHistoryLimit` -> `DropOldestSegments`), a seek cannot read a segment back from disk, and a resume from
  the past deletes the whole folder (F-1). **Q-3a decided 2026-11-08 (owner): the whole session, no disk limit**;
  recording stops only when the disk runs out of space, with a warning first. Open: the seek time into a segment on disk
  (Q-3b, a measurement). **Q-3c decided 2026-11-08 (owner): a clean close deletes the folder unless the session
  was saved** (as today; Save keeps every branch); folders left by a crash keep the 7-day cleanup.
- **Q-4. Names.** Automatic two-word names ("amber falcon", decided 2026-09-29), plain numbers ("branch 3"), or
  "attempt N" for siblings at one fork point with two-word names otherwise.
  **Owner 2026-11-08: a name alone is not enough; every branch carries metadata.** At least: when it was
  created (host time and the fork position `{branch, frame, T}`), by whom (the user through which surface: Qt,
  deck, controller, GDB / DeZog; an MCP / WebAPI client; the analyzer with its run id), why (the act: resume,
  memory / register / port edit with its bytes, explicit `branch_create` with the caller's reason text), tags and a
  note, editable later; the same shape as the agent bookmarks (label, note, tags, context). The name stays
  editable; its default (proposal, not yet confirmed) is the cause with a number ("resume 3", "poke #5C3A",
  "attempt 7"), and a caller may give one at creation.
- **Q-5. Default view.** The Qt panel and the deck strip show the active lineage plus a count, with the tree on demand;
  or lanes for all branches always (the reference UI).
  **Decided 2026-11-08 (owner): the active lineage with fork marks and the branch count**; the tree (lanes) on
  demand; a filter by creator and tags hides, for example, the analyzer's branches.
- **Q-6. Agent input into a branch.** Constructed (events written into the branch, then replayed, no live run) or
  recorded (the machine runs the branch live, fed from the script).
  **Decided 2026-11-08 (owner): recorded.** The command creates the branch and runs the machine N frames (turbo,
  no sound or picture), fed from the script; the branch is recorded like any other; the machine is busy for the
  run. A background build on a second machine is not planned.
- **Q-7. Promote.** A "make this lineage the trunk" operation, or only "save this lineage as a new session".
  **Decided 2026-11-08 (owner): no promote; "save this lineage" only** (a linear session or a clip); the trunk
  never changes and branch ids are never renumbered.
- **Q-8. Compare view.** In the first delivery: `diff` and switching only, or also the synchronized dual view.
  **Decided 2026-11-08 (owner): no comparison in this design.** The owner has not seen a use case for it; UC-6
  is out (§3). An agent or the analyzer ranks experiments by reading its own outcome (a watch, an address, a screen
  hash) on each branch; `diff` stays what it is, between moments of the active lineage.
- **Q-9. What a save keeps.** All branches by default (with "active lineage only" as an option), and the black-box
  folder written with all branches.
  **Decided 2026-11-08 (owner): all branches by default**, with their metadata and bookmarks; an older build opens
  the file as its trunk; an option saves the active lineage only, as a linear session (Q-7). The recording folder
  writes every branch (Q-3).
- **Q-10. Write-through media in branches.** The host file follows the active branch (switching rewrites it) or
  write-through images are held in memory while there is more than one branch.
  **Decided 2026-11-08 (owner): media writes go into the TTD stream.** The history stays an append-only stream:
  a guest's sector / track writes (and write-protect toggles) are appended to the stream of the branch that made
  them, on disk with the rest of the history (Q-3), with no 64 MB limit; the in-memory `MediaUndoLog` becomes a
  cache of it. The image file on the host (write-through) holds the state at the active branch's current moment,
  as a seek already sets it today (`MediaUndoLog::Move`, `HostWriteHold` during a replay). Today's facts: the undo
  log lives in memory only (64 MB, oldest versions dropped), its versions are not in the session file, and a write
  after a seek back drops the versions above (`DropAbove`).
- **Q-11. Branches in a loaded session.** Keep the 2026-10-10 postponement, or allow branches from the past of a
  loaded session now that the old future is never lost.
  **Decided 2026-11-08 (owner): allowed in the first delivery.** A resume or an edit in the past of a loaded
  session starts a branch; the loaded branches stay untouched, new ones are appended. This lifts the 2026-10-10
  postponement (coordinator §12) for branches.

## 6. Storage directions (owner 2026-11-08, for storage.md)

- **Branch identity.** Every branch gets a unique GUID at creation and an optional name; the branch's streams
  (sub-streams) are keyed by it. In the file a branch table maps each GUID to a small per-file number (the `branch`
  u16 already in `part_end` and `index_part`, `ttd.ksy:262, 313`), so records do not carry 16 bytes each; the table
  holds the parent GUID, the fork position `{branch, frame, T}` and the metadata (Q-4).
- **Fast demultiplexing** (to design). What exists: one file per minute segment; each file ends with an index whose
  part table already gives, per part, `part_end_offset`, `first_frame`, `frame_count` and `branch`, so reading one
  branch from a file is an index read and seeks to its parts. Options to weigh in storage.md:
  (a) parts of several branches mixed in a segment file, demuxed by that index;
  (b) a segment file per branch and minute (`segment-NNNN-<branch>.ttd`), demuxed by the file name, no extra index;
  (c) an extra index file for the folder (append-only: branch table, fork points, which segment files and parts hold
  which branch and frames), rebuildable from the files' own indexes after a crash.
  Measures: time to open a branch, seek into a segment on disk (Q-3b), crash safety, "save as" joining.

