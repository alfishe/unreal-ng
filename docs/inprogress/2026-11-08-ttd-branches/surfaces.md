# TTD branches: user surfaces and automation planes

Part of [TTD branches](README.md). Status: **partly built**: §1 Qt panel (`66f6811ae`), the debuggers ([debuggers.md](debuggers.md), `0744664bb`), §5 every verb on every plane (`e0ac62a53`, `c675c6a37`), the `ttd.branches` topic and §5.3 the analyzer's loop (2026-11-13). Open: §2 actions and readouts, §3 Stream Deck, §4 controllers. Design 2026-11-08. Model: [architecture.md](architecture.md).

Rule for every surface: without branches nothing changes (no new widget, key face or reply field appears until the
session has a second branch). With branches, the default view is the active lineage with fork marks and the
branch count; the tree is on demand (Q-5).

## 1. Qt (`unreal-qt/src/widgets/ttdwidget.cpp` and its managers)

| Element | Today | With branches |
|---|---|---|
| Timeline slider | linear `[sessionStartFrame, currentEndFrame]`, journal spans | the active lineage; each span tinted by its branch; fork marks (ticks) where other branches leave the lineage, a number when several leave at one point; hover: name, creator, reason, length |
| Fork mark click | - | a popup with the branches leaving there (name, age, length, tags); click switches |
| Branch chip (next to the position) | - | "resume 3 · 5 branches ▾"; opens the branch tree |
| Branch tree (dock or popup) | - | lanes: one row per branch, horizontal = frames, the active lineage highlighted; columns name, created, by, reason, length, tags, pinned; filter by creator / tags / run; actions switch, rename, tag, note, pin, delete, save lineage |
| "Rec From Here" | tooltip "Truncate future history and resume..."; media question | label stays; tooltip "Continue recording from here. The current future is kept as a branch."; the media question only when a medium cannot follow (architecture.md §7) |
| After a fork | - | a toast: "Previous future kept as 'resume 2'. [Back]"; Back = switch to the branch left |
| Status bar (`statusbarmanager.cpp:833-852`) | TTD state, position | + the branch name when there are branches |
| Edit in the past (memory viewer, registers) | refused / stops the recording | allowed; the toast names the branch the edit started |
| Black-box menu (`menumanager.cpp:1287-1313`) | | + "Branches..." (opens the tree), "Previous branch" |

**Done in the TTD panel (2026-11-10).** The owner's rule: extend the panel's existing expanding rows instead of new
popups or docks. So the chip, the tree and the fork popup became:

- **Branches (N)** pill next to Advanced, hidden while the trunk is alone. It opens a **Branches row** below the
  scrubber, which works like the Advanced row: it expands, the window grows by its height, and its open state is
  kept in `ttd/branchesOpen`.
  - The row holds the lanes (`branchlanes.cpp`): one row per branch in tree order, frames horizontal, connectors to
    the parent and the current-moment line. The active branch is bold and pinned branches end in " *". Five lanes
    are in sight; more scroll.
  - Below the lanes are the buttons Switch, Rename..., Tags..., Note..., Pin / Unpin, Delete and Save Lineage...
    (`dump lineage=true`); a right click on a lane offers the same.
  - Switch is off for the active branch, and Delete is off for any branch on the active lineage. Delete asks first
    only when the branch has children or is pinned.
- **Slider** (`journalspanslider.cpp`):
  - The lineage's parts off the trunk are tinted along the top.
  - A fork mark appears where another future goes on: a branch leaving the line, or the parent's own future at the
    point where the line left it (after a resume, that is the trunk's old future). Several branches leaving at one
    point make the mark taller.
  - Hovering a mark lists each branch with its length; a double click on a mark selects the branch and opens the row.
- **After a fork** the status line reads "New branch '+00:00:25.150', previous future kept on 'trunk'" for 8 s. There is
  no Back button: Switch in the row does it.
- **Status line**: " | Branch: <name> (N)".
- **Rec From Here** is gone. Where a recorded future follows the position the button reads **Branch Here**: it
  records a new branch from there (the owner's choice, 2026-11-10: the fork must be visible on the button, not only
  in its tooltip). At the end of the recording it reads **Continue Rec**: the same session goes on, while Start Rec
  begins a new one. Both names fit the button, so the slider keeps its length.
- **Under remote control** the panel turns none of its controls on (it updates them every 100 ms, the guard relocks
  every 250 ms: the two made the panel flicker).

Not done yet:

- the main window's status bar;
- the menu items and keys below;
- the lane filters (creator / tags / run);
- edits in the past.

A rename, note, tag or pin changes the session in memory; for a folder session it is not yet written to
`branches.tdx`, which has no update record.

Keyboard: `Ctrl+Alt+B` previous branch (the one left last), `Ctrl+Alt+Shift+B` the tree. Assigned through the action
catalogue like the other TTD actions.

## 2. Actions (shared by Qt, Stream Deck, controllers)

New built-in actions (`emulator/actions/builtinactions.cpp`), each a thin call into `TTDControl`:

| Action | Does |
|---|---|
| `ttd.branch_prev` | switch to the branch left last (a stack of switches; repeated presses walk back) |
| `ttd.branch_next_sibling` / `ttd.branch_prev_sibling` | among the branches forking at the nearest fork position on the lineage, by creation order |
| `ttd.branch_menu` | open the branch list on the surface |
| `ttd.branch_pin` | toggle pin of the active branch |
| `ttd.branch_delete` | deletes the branch chosen in the branch menu (never one of the active lineage); a hold-to-confirm on surfaces |

`ttd.resume_live` keeps its meaning; it is now safe (a fork, never a cut), so no confirmation is added.

Readouts (the `show:` vocabulary, `readouts.h`): `ttd.branch.name`, `ttd.branch.count`, `ttd.branch.depth`,
`ttd.branch.created_ago`, `ttd.branch.pinned`.

## 3. Stream Deck (`companions/streamdeck/`, scenario `data/streamdeck/scenarios/src/unreal.spec.yaml`)

| Element | With branches |
|---|---|
| Strip `DrawTimeline` (`scenariorender.cpp:433-560`) | the capsule = active lineage, fork ticks, the branch name at the left; segments in memory vs on disk drawn in two tones |
| Touch seek (`runtime.cpp:793-833`) | unchanged (seeks on the lineage); a long touch on a fork tick switches to its newest branch |
| Home | LIVE stays (fork-safe); a BRANCH key: `show: ttd.branch.name` with the count as a badge; press = branch menu, hold = previous branch |
| Time menu | + PREV BRANCH, SIBLING ◀ ▶, PIN, DELETE (hold 1 s) |
| Branch menu | a page of keys, one per branch (newest first, active lineage first): name, age; press switches; dial scrolls pages |
| OPS dial | press stays `ttd.resume_live`; rotate while held = siblings at the nearest fork |

Prerequisite fix (finding F-2): the FRAME key reads `ttd.position.current.frame`, the WebSocket topic carries
`frame` at the top level. Either the topic adds the REST shape or the scenario reads `ttd.position.frame`; the
`ttd-surface-contract` verification gets a case for it. The new branch readouts go into that contract from the
start.

## 4. Controllers

| Device | Mapping |
|---|---|
| ShuttlePRO | K13 resume (unchanged, now a fork); K12 previous branch (proposal); jog shuttle seeks on the lineage |
| Gamepad (pad profile) | east = resume (unchanged); hold back + east = previous branch |
| Surface Dial | press-hold menu gains "Branches" |
| Paddles | unchanged |

Profiles under `data/controllers/profiles/` get the new actions in their defaults; users remap as today.

## 5. Automation planes

Every capability on CLI, Python, Lua, MCP, REST and WebSocket (`docs/guidelines/automation-planes-guidelines.md`),
all through `TTDControl` verbs.

### 5.1 Verbs (`ttdcontrol.cpp`)

| Verb | Options | Reply |
|---|---|---|
| `branches` | `filter` (creator, run, tag, name prefix, min/max length, created before/after, pinned), `scope` (lineage / all), `limit`, `offset` | the tree: per branch GUID, local id, name, parent, fork, end, length, meta; the active one |
| `branch-info` | `branch` | one branch with its children and its files |
| `branch-create` | `at {branch?, frame, tinframe}` (default here), `name`, `reason`, `tags`, `note`, `run {frames, input}` | the new branch; with `run`: after the recorded turbo run (Q-6), its end and the outcome readouts asked for (`read: [addresses / watches]`) |
| `branch-switch` | `branch`, `at` | the position reached |
| `branch-update` | `branch`, `name`, `tags` (set / add / remove), `note`, `pinned` | the branch |
| `branch-delete` | `branch`, `force` | what was deleted (descendants, bytes, files) or the refusal with the pinned / active branches named |
| `branch-prune` | the `branches` filter, `dry_run` (default true) | the list that is (would be) deleted, bytes freed |
| `save` (existing `dump`) | + `lineage=<branch>` | the file; default all branches |

As built (2026-11-13):

- **`branch-create`** takes flat options: `frame` / `tinframe` (default the current moment; on a stopped recording the
  machine seeks there first, a moment outside the history is refused) or `branch` (fork on it, at `frame` / `tinframe`
  on it, default its end), `name`, `reason` (stored as `create` or `create: <reason>`), `tags`, `note`, `pinned`,
  `run` (the run id), `created_by` (default the calling plane: `cli`, `webapi`, `mcp`, `lua`, `python`, ...), `media`
  (as `resume`). An explicit branch stays while it is still empty (a resume's empty tip is dropped, a created one is
  not). The run: `run_frames` (at most 10000) recorded frames with an input script `input`, each line
  `frame:key[+key][:hold|:down|:up]` (frame counted from the branch start, hold in frames, default 2, 1..1000; `down`
  / `up` hold or release until told; the presses are journaled like any keyboard input), then `read` (`addr[:len]`,
  len 1..256: `reads[] {address, length, bytes, hex}`) where the run ended, then `go_back` (the option is not called
  `return`: a keyword in Lua and Python) switches back to the branch and moment the call started from. A run the user
  takes control from stops (`aborted`).
- **`branch-info`** gives `children` (with fork and length), `ancestors` (from the trunk), the branch's segment
  `files` and `file_bytes`.
- **`branch-prune`** needs at least one filter; it never deletes a branch on the active lineage, a pinned one or one
  holding either below it: each stays in `kept` with the reason. A selected branch whose ancestor is selected goes
  with it. The reply counts the frames, the file bytes and the memory freed.
- **Events**: the controller publishes the last branch change (`created`, `switched`, `updated`, `deleted`, with a
  sequence number, the branch and how many a delete removed) with the branch table; the WebSocket topic
  `ttd.branches` pushes it (`NC_READOUTS_CHANGED` area `ttd.branches`, never per frame).

Existing verbs: positions take an optional `branch` (`seek`, `resume`, `bookmark-add`, `memory-at`, `diff`
between two moments of the lineage, `find-*`); replies carry `branch` in every position when the session has
branches. `resume` reports `{branch: new, kept: old}` and no longer "history after that point was discarded".

### 5.2 Per plane

| Plane | Form |
|---|---|
| CLI (`cli-processor-ttd.cpp`) | `ttd branches`, `ttd branch create|switch|update|delete|prune|info` |
| REST (`emulator_api.h`, `api/ttd_api.cpp`) | `GET /api/v1/emulator/{id}/ttd/branches`, `POST .../branches`, `GET|PATCH|DELETE .../branches/{branch}`, `POST .../branches/{branch}/switch`, `POST .../branches/prune` |
| WebSocket | every route as a `call`; topic `ttd.position` gains `branch {id, guid, name}` and `branch_count` (only with branches); new topic `ttd.branches` with `{available, count, revision, active, viewed, last: {seq, event: created|updated|deleted|switched, branch, removed}}` |
| MCP `time_travel` (`mcp-tools.cpp`) | actions `branch_list`, `branch_info`, `branch_create`, `branch_switch`, `branch_update`, `branch_delete`, `branch_prune`; the text answers name the branch and stay inside the answer budgets (lists summarised past 20 rows) |
| Lua / Python | Lua `ttd_branches{...}`, `ttd_branch_info`, `ttd_branch_create{...}`, `ttd_branch_switch`, `ttd_branch_update`, `ttd_branch_delete`, `ttd_branch_prune{...}`; Python `emu.ttd_branches(...)` and the same set with keyword options |

### 5.3 The analyzer (UC-5)

Design: the analysis pipeline uses `branch-create` with `run` and `createdBy = analyzer` plus its run id; it removes
its branches with `branch-prune {run: <id>}` after the run, unless it pins the ones it reports. Its positions name
branches by GUID so a human can open them.

As built (2026-11-13, owner decision: clones, a branch on request):

- **Experiments stay on analysis clones.** `analysis experiment` runs every spec on a muted headless clone seeked
  from the captured trace (the L3 pool, `l3experiment.h`); the source instance is not touched by default. Running
  hundreds of experiments as branches of the user's session would fill its table and pause the user's machine for
  each one.
- **A branch on request.** A spec with `"keep_branch": true` (or a name, 1..63 characters), or the action option
  `keep_branches=observed|all` (default `asked`), replays the experiment in the source instance's TTD session after
  the batch (`l3experimentbranch.h`): `branch-create` at the experiment's start frame, its pokes and registers as tool
  edits (`poke` / `regs`, recorded so the branch replays them), its input as the input script (keys, `joy.<button>`,
  `release_all`), its frames as `run_frames`, `created_by analyzer`, the batch's run id (`run`, default
  `experiment-<n>`), tags `analyzer,experiment[,<finding>]`, the claim and the result line as the note, then
  `go_back`. The memory checks' bytes are read on the branch and compared with the clone's: each result carries
  `branch {created, id, guid, name, run, reproduced, mismatch}` (or `{created: false, error}`); the reply counts
  `branches_kept` and names `branch_run`. `ttd branch prune --run <id> --apply` removes them.
- **Not kept**: a coverage experiment's pins (a byte held before every instruction has no branch form), an
  experiment that did not run, and one whose medium changed after its start frame (`media ask`: the reply names the
  decision, the analyzer does not make it).
- The source ends paused where it stood (`branch-create` pauses first). A recording in progress ends at its last
  committed frame (the fork closes the branch it recorded), and `go_back` returns to that frame: the frame that was in
  progress was never recorded.

## 6. External debuggers

GDB and DeZog see the active lineage as one linear history (protocol unchanged). A reverse-continue followed by a
continue in the past forks; GDB `monitor ttd branches` lists, `monitor ttd branch switch <name>` switches.
