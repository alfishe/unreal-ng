# TTD branches: user surfaces and automation planes

Part of [TTD branches](README.md). Status: **design** (2026-11-08). Model: [architecture.md](architecture.md).

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

Existing verbs: positions take an optional `branch` (`seek`, `resume`, `bookmark-add`, `memory-at`, `diff`
between two moments of the lineage, `find-*`); replies carry `branch` in every position when the session has
branches. `resume` reports `{branch: new, kept: old}` and no longer "history after that point was discarded".

### 5.2 Per plane

| Plane | Form |
|---|---|
| CLI (`cli-processor-ttd.cpp`) | `ttd branches`, `ttd branch create|switch|update|delete|prune|info` |
| REST (`emulator_api.h`, `api/ttd_api.cpp`) | `GET /api/v1/emulator/{id}/ttd/branches`, `POST .../branches`, `GET|PATCH|DELETE .../branches/{branch}`, `POST .../branches/{branch}/switch`, `POST .../branches/prune` |
| WebSocket | every route as a `call`; topic `ttd.position` gains `branch {id, guid, name}` and `branch_count` (only with branches); new topic `ttd.branches` with `{event: created|updated|deleted|switched, branch}` |
| MCP `time_travel` (`mcp-tools.cpp`) | actions `branch_list`, `branch_info`, `branch_create`, `branch_switch`, `branch_update`, `branch_delete`, `branch_prune`; the text answers name the branch and stay inside the answer budgets (lists summarised past 20 rows) |
| Lua / Python | `emu:ttd_branches()`, `emu:ttd_branch_create{...}` and the same set |

### 5.3 The analyzer (UC-5)

The analysis pipeline uses `branch-create` with `run` and `createdBy = analyzer` plus its run id; it removes its
branches with `branch-prune {run: <id>}` after the run, unless it pins the ones it reports. Its positions name
branches by GUID so a human can open them.

## 6. External debuggers

GDB and DeZog see the active lineage as one linear history (protocol unchanged). A reverse-continue followed by a
continue in the past forks; GDB `monitor ttd branches` lists, `monitor ttd branch switch <name>` switches.
