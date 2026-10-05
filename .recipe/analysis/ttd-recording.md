# Recipe: TTD — Turn Recording On/Off, Save/Load, Travel Through Time

Time-Travel Debug records every frame (checkpoints + compressed dirty pages)
and every port read and write. You can then scrub, step and replay the
machine backward and forward. The write journal (every memory write, for
instant "who wrote this last") is off by default: switch it on when you need
it, or build it later for any span - [ttd-write-journal.md](ttd-write-journal.md).

Reverse *queries* (`find-last`, `reverse-step/continue`) live in
[ttd-reverse-debugging.md](ttd-reverse-debugging.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the
> `time_travel` tool covers the whole TTD lifecycle (record, navigate,
> bookmarks, dump/load, coverage). Use [WebAPI](#webapi) only inside
> host-side Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# record
time_travel   {"action":"start"}                               # record (write journal off)
time_travel   {"action":"start","journal":true}                # or: also record the write journal
time_travel   {"action":"status"}                              # state, journal size, coverage frames
inspect_state {"aspects":["ttd"]}                              # status + position alongside other aspects
time_travel   {"action":"stop"}                                # history retained → idle
time_travel   {"action":"invalidate","reason":"next scenario"} # drop history

# navigate (refused while recording — stop first)
time_travel {"action":"position"}
time_travel {"action":"seek","frame":12000}                    # optional "tinframe"
time_travel {"action":"step_back_frame"}
time_travel {"action":"step_forward_frame"}
time_travel {"action":"step_back_instruction"}
time_travel {"action":"step_forward_instruction"}
time_travel {"action":"markers"}                               # replay barriers
time_travel {"action":"resume"}                                # needs detached (seek/step first); truncates the future

# bookmarks
time_travel {"action":"bookmark_add","label":"before-crash"}   # at the current position
time_travel {"action":"bookmark_list"}                         # → structuredContent.bookmarks
time_travel {"action":"seek_bookmark","label":"before-crash"}
time_travel {"action":"bookmark_delete","label":"before-crash"}

# save / load
time_travel {"action":"dump","path":"scratch/session-001.ttd"}
time_travel {"action":"load","path":"scratch/session-001.ttd"} # same model only; idle afterwards → seek

# coverage
time_travel {"action":"coverage_summary","kind":"executed"}    # activity heatmap buckets
```

The target is resolved as usual (`"target"` argument, or `auto`).

## WebAPI

The curl walkthrough below — right choice when a Python/bash pipeline drives
these steps directly over HTTP.

### Turning recording on

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" \
     -H 'Content-Type: application/json' -d '{}' | jq .
```

Optional body:

```json
{}                     // record; the write journal is off
{"journal": true}      // also record the write journal (12 bytes per memory write)
```

The write journal only speeds up "who wrote address X last": with it the
answer is instant, without it the search replays one frame (a few ms, same
answer). Switch it during the recording or build it afterwards:
[ttd-write-journal.md](ttd-write-journal.md).

StartRecording **auto-enables** the `timetravel` and `debugmode` runtime
features. On stop it turns `debugmode` back off if it was the one that
enabled it; `timetravel` stays on. Idempotent: starting twice is a no-op
(`already_active: true`).

While recording (and while the machine sits in `detached`) the
**acceleration lock** holds: host speed forced to 1x (2x-16x refused),
turbo mode off, fast tape / turbo tape / fast disk read as off (feature
lists show them off; `PUT /feature/{name} {"enabled": true}` on one answers
`409`, `PUT /settings/speed` other than 1 and `PUT /settings/turbo_mode true`
answer `409` too). The previous settings come back when the session returns
to `idle`. Full
rules: [command-interface.md → TTD Session Rules](../../docs/emulator/design/control-interfaces/command-interface.md#ttd-session-rules).

### Watch it record

```bash
curl -s "$BASE/emulator/$EMU_ID/ttd/status" | jq '{
  state, session_start_frame, current_end_frame, checkpoint_count,
  page_store_used_bytes, write_journal_records, coverage_index_frames, ttd_available}'
```

| Field | Meaning |
|:--|:--|
| `state` | `idle` / `recording` / `detached` |
| `current_end_frame` | live head of the timeline |
| `checkpoint_count` | frames captured |
| `page_store_used_bytes` | compressed dirty-page budget in use |
| `write_journal_records` | write journal entries (0 when it was not recorded) |
| `write_journal_segments` | the spans the write journal covers |
| `history_limit_frames` / `history_limit_bytes` | the history limit (0 = none), see below |
| `history_bytes` / `evicted_checkpoints` | checkpoint data held now / oldest checkpoints released by the limit |
| `ttd_available` | `false` → build lacks TTD (treat status as capability probe) |

MCP: `time_travel` `{"action":"status"}`.

### Stop / resume / invalidate

```bash
# Stop capturing — history RETAINED, browsable (state -> "idle";
# it becomes "detached" once you seek or step into it)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop" | jq '.state'

# Drop everything, back to "idle" (live machine untouched)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/invalidate" | jq '.state'

# Keep only the newest history while recording (long sessions, big machines
# such as TSL-VDAC2 at ~0.6 MB per frame): the oldest frames are released,
# the session start moves forward, a file saved later replays what is left.
# 0 = no limit; a field left out is kept. MCP: time_travel action
# "history_limit" with history_frames / history_bytes (also on "start").
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/history-limit" \
  -H 'Content-Type: application/json' -d '{"bytes": 4294967296}' | jq

# From a past point (after a seek/step, state "detached"): truncate the
# future there and continue recording. Refused (resumed:false) from "idle".
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/resume" | jq '.state'
```

Seek, step, find-last and reverse-* during `recording` pause the recording
(`backend: engine`): everything up to that instant is kept, the state is
`detached` and `/ttd/status` says `recording_paused: true`. Resume at the
paused point (`POST /ttd/resume` while there, or seek back to it first)
continues the same recording; running the machine forward through it
continues it too; `POST /ttd/stop` ends it. On `backend: v1` these return
`409 Conflict` - stop first.

### Traveling

```bash
# Where am I?
curl -s "$BASE/emulator/$EMU_ID/ttd/position" | jq '.'
# → {"current": {"frame": 12345, "tinframe": 0},
#    "session_end": {"frame": 12402, "tinframe": 0}, "state": "detached"}

# Seek to an absolute point (frame + optional intra-frame t-state)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/seek" \
     -H 'Content-Type: application/json' \
     -d '{"frame": 12000}' | jq '{reached, arrived_at, halt_reason}'

# Frame stepping
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/step-back"    | jq '.frame'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/step-forward" | jq '.frame'

# Instruction stepping (either direction)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/step-instruction" \
     -H 'Content-Type: application/json' \
     -d '{"dir": "back"}' | jq '{stepped, frame, tinframe}'
```

`halt_reason` on seek: `target` (arrived), `external_event` (blocked by a
replay barrier — tape transport command, WD1793 sector/track write, or a
memory edit made by a tool (WebAPI, CLI, Lua, Python, DeZog) while
recording; the response carries `blocking_marker`, and
`GET /ttd/markers` lists them all), `out_of_range`. Keyboard and mouse input
are journaled and replayed, so they are not barriers. On the engine
(`GET /ttd/status` -> `backend: "engine"`, the default) tape commands, disk
writes and tool memory / register edits are replayed too and never block a
seek; only v1 stops at them. A seek before the earliest kept position
answers `out_of_range` with `earliest` (also in `GET /ttd/status`).

Every scrub pauses the emulator first and leaves it paused at the new
position; the screen/UI repaint automatically. To continue live execution
from a past point: seek, then `POST /ttd/resume` (it resumes the emulator).

### Bookmarks (agent-friendly labels)

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/bookmarks" \
     -H 'Content-Type: application/json' \
     -d '{"label": "before-crash"}' | jq .          # defaults to current position
# or at a specific point:
#   {"label": "menu", "frame": 5000}

curl -s "$BASE/emulator/$EMU_ID/ttd/bookmarks" | jq '.bookmarks[]'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/seek" \
     -H 'Content-Type: application/json' \
     -d '{"bookmark": "before-crash"}' | jq '.arrived_at'
curl -s -X DELETE "$BASE/emulator/$EMU_ID/ttd/bookmarks/before-crash" | jq .
```

MCP: `time_travel` actions `bookmark_add` / `bookmark_list` /
`bookmark_delete` / `seek_bookmark`. Labels: non-empty, ≤ 63 chars.
Bookmarks are advisory annotations — they never block replay.

### Save the session (.ttd) and load it back

```bash
# Serialize (read-only: does not invalidate the live timeline; pause first for a stable dump)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/dump" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/session-001.ttd"}' | jq .

# Later (same or fresh instance): replace the session entirely
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/load" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/session-001.ttd"}' | jq .
```

The path is opened by the **emulator process** (relative paths resolve
against its working directory). A load only succeeds into an instance of
the same model the session was recorded on; otherwise it answers 400 with
`ok: false` and an `error` naming both model ids — provision a matching
instance and retry. After `load` the session is idle/browsable (use
`seek` to position the machine): `GET /ttd/status` now shows
`loaded_from_file: true`, `source_path`, `captured_at_unix_ms`, plus the
recording machine's `model_id`/`model_ram_pages`. The `.ttd` header pins a ROM signature:
replaying against a different ROM set is refused, not silently wrong.
The binary format is portable (Kaitai schema `core/src/debugger/ttd/ttd.ksy`;
Python analyzer in `tools/verification/ttd-analyzer`), so captures outlive
the process.

### Inspect a file, search port journals, export a clip

```bash
# Describe a .ttd without loading it (no emulator instance involved; MCP time_travel "file_info")
curl -s "$BASE/ttd/file-info?path=scratch/session-001.ttd" \
  | jq '{session_start_frame, session_end_frame, sections, machine}'
# 400/404 with ok:false + error when the path is not a readable .ttd

# "When did the program ...?" from the port journals, no replay (MCP time_travel "port_events")
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/port-events" -H 'Content-Type: application/json' \
     -d '{"event":"key","arg":"enter","newest":true,"limit":3}' | jq -c '.hits[] | {frame, tinframe, port, value, pc}'

# Lossless frame clip, written inside the core (one call instead of seek + capture per frame;
# MCP time_travel "export_clip")
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/export-clip" -H 'Content-Type: application/json' \
     -d '{"from":100,"to":200,"path":"scratch/clip1"}' | jq '{ok, frames, bytes, planeb, width, height, seconds}'
```

- `file-info` reports `path`, `file_bytes`, `schema_version`, the frame range, `checkpoint_count`, which
  `sections` are present (`write_journal`, `coverage_index`, `bookmarks`, `input_journal`, `port_journals`, ...)
  and the recorded `machine` (model, ROM signature, devices): provision a matching instance before `load`.
- `port-events` needs a stopped or paused recording (it also works on a loaded file, or on a file on disk with
  `"file": "<path>"`). `event` is one of `key`, `ear`, `ay-read`, `ay-write`, `ay-select`, `border`, `beeper`,
  `in`, `out`; `arg` (MCP: `event_arg`) is the key name or AY register. `in`/`out` narrow with `port` /
  `port_mask` / `value` / `value_mask`; `newest` returns the last hits first. The reply has `event`,
  `direction`, `count`, `truncated`, `scanned` and `hits[]` (`index`, `frame`, `tinframe`, `port`, `value`,
  `pc`, plus `ay_register` for AY events).
- `export-clip` takes `from`, `to` (frames), `path` (an absolute directory, as seen by the emulator process) and
  optional `chunk` (frames per chunk). It is synchronous, pauses the emulator, and is refused while recording.
  The same export on the other surfaces: MCP `time_travel` action `export_clip` (`from_frame`, `to_frame`,
  `path`, `chunk`), CLI `ttd export-clip <from> <to> <dir> [--chunk N]`, Lua `ttd_export_clip(from, to, dir)`,
  Python `emu.ttd_export_clip(from_frame, to_frame, path)`.

### Coverage heatmap (when did my code run?)

With the coverage index enabled you get per-frame executed/written/read
bitmaps — queried via `time_travel` MCP actions `coverage_probe` /
`coverage_scan` / `coverage_summary` or the WebAPI trio
`GET /ttd/coverage/{probe,scan,summary}` (parameters in
[ttd-reverse-debugging.md](ttd-reverse-debugging.md)).

## Pitfalls

- **Clean up after every scenario.** A TTD session holds its history until
  it is dropped: `POST /ttd/invalidate` (MCP `time_travel` action
  `invalidate`) when you no longer need it, and delete the `.ttd` files you
  dumped once you have read them (keep dumps under `scratch/`). From the
  session file onwards (TTD v2, Phase 4) every recording also writes a
  folder under `~/.unreal-ng/ttd/` (Windows `%USERPROFILE%\.unreal-ng\ttd\`)
  while it lives: invalidate removes it, a saved recording stays as a file
  there until you delete it. Startup cleanup only removes crashed leftovers
  after 7 days; a script that forgets to clean fills the disk (about 2 GB
  per hour of heavy content).
- **409 on scrub** → `backend: v1` and still recording; `POST /ttd/stop` first
  (the engine pauses the recording instead, `recording_paused: true`).
- **`seek` beyond `current_end_frame`** → `halt_reason: "out_of_range"`,
  machine stays where it was.
- **Memory budget**: development mode costs ~64 MB journal + page store;
  long captures grow — check `page_store_used_bytes` and dump+invalidate
  between scenarios.
- **Markers stop backward replay** by design (external inputs can't be
  un-happened); list them with `GET /ttd/markers` before wondering why a
  seek halted early.
- **Loads wipe the session.** Snapshot load, tape load, disk load/create,
  ROM reload and a host speed change on a stopped session drop the whole
  history — treat snapshot+TTD as sequential experiments, not interleaved
  ones ([load-snapshot.md](../media/load-snapshot.md)). Dump first if you
  need the recording.
- **Reset keeps history.** A reset (or a disk autostart's quick reset)
  stops the recording and keeps what was captured; a machine sitting in
  history goes back to `idle`.
- **ZX-Evo / ATM3 SD card** activity ends the recording (history dropped)
  at the next frame boundary: TTD cannot follow the SD card yet.
