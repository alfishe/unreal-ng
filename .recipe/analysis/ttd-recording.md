# Recipe: TTD — Turn Recording On/Off, Save/Load, Travel Through Time

Time-Travel Debug records every frame (checkpoints + compressed dirty pages)
plus, in development mode, a write journal of every memory/port write. You
can then scrub, step and replay the machine backward and forward.

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
time_travel   {"action":"start"}                               # development mode (write journal on)
time_travel   {"action":"start","mode":"gaming"}               # or: no write journal
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

Optional body selects the journal mode:

```json
{}                                        // development mode (default): full write journal
{"enable_write_journal": false}           // gaming mode: ~90% smaller, no fast reverse queries
{"mode": "gaming"}                        // same, by name
```

- **Development mode** — fast "where was X last written?" queries, 12 bytes
  per write journaled. Use for debugging.
- **Gaming mode** — small footprint, seek/step-back still work,
  reverse-watchpoint queries fall back to slow checkpoint replay.

StartRecording **auto-enables** the `timetravel` and `debugmode` runtime
features. On stop it turns `debugmode` back off if it was the one that
enabled it; `timetravel` stays on. Idempotent: starting twice is a no-op
(`already_active: true`, and the journal mode of the running recording is
kept).

While recording (and while the machine sits in `detached`) the
**acceleration lock** holds: host speed forced to 1x (2x-16x refused),
turbo mode off, fast tape / turbo tape / fast disk read as off. The
previous settings come back when the session returns to `idle`. Full
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
| `write_journal_records` | journal entries (0 in gaming mode) |
| `ttd_available` | `false` → build lacks TTD (treat status as capability probe) |

MCP: `time_travel` `{"action":"status"}`.

### Stop / resume / invalidate

```bash
# Stop capturing — history RETAINED, browsable (state -> "idle";
# it becomes "detached" once you seek or step into it)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop" | jq '.state'

# Drop everything, back to "idle" (live machine untouched)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/invalidate" | jq '.state'

# From a past point (after a seek/step, state "detached"): truncate the
# future there and continue recording. Refused (resumed:false) from "idle".
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/resume" | jq '.state'
```

The invariant to respect: **scrub only while stopped/detached**. Seek,
step, find-last and reverse-* during `recording` return `409 Conflict` with
an explanation — the API guards the timeline for you.

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
debugger memory edit; the response carries `blocking_marker`, and
`GET /ttd/markers` lists them all), `out_of_range`. Keyboard and mouse input
are journaled and replayed, so they are not barriers.

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

### Coverage heatmap (when did my code run?)

With the coverage index enabled you get per-frame executed/written/read
bitmaps — queried via `time_travel` MCP actions `coverage_probe` /
`coverage_scan` / `coverage_summary` or the WebAPI trio
`GET /ttd/coverage/{probe,scan,summary}` (parameters in
[ttd-reverse-debugging.md](ttd-reverse-debugging.md)).

## Pitfalls

- **409 on scrub** → you're still recording; `POST /ttd/stop` first.
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
