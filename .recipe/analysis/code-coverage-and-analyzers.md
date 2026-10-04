# Recipe: Code Coverage and the Analyzer Framework

Goal: answer "did this code run, and what lies between the parts that did?"
(executed-address coverage), and drive the named analyzers every emulator
instance carries (`trdos`, `coverage`, `aylog`, `audiocapture`,
`editor-input`): list them, switch them on and off, read their event buffers.
The TR-DOS analyzer is the worked example. This recipe also covers coverage
over a *recorded* TTD timeline in a short section at the end.

Related, not repeated here: memory-access *counters* (reads / writes / executes
per page and per address) are in [memory-counters.md](memory-counters.md);
telling a custom disk loader from a standard one, with port-PC attribution, is
in [nonstandard-loader.md](nonstandard-loader.md) (it uses the `trdos`
analyzer's raw FDC stream described below).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `analyze_performance` has first-class coverage actions; the analyzer routes
> have no dedicated tool and ride `invoke_api` (same paths as WebAPI). Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# --- executed-address coverage: first-class actions ---
analyze_performance {"action":"coverage_start","clear":true}
#   ...run the scenario: control_execution {"action":"run_frames","frames":500}...
analyze_performance {"action":"coverage_read","max_ranges":512}
analyze_performance {"action":"coverage_gaps","start":"0x4000","end":"0xFFFF","max_gaps":256}
analyze_performance {"action":"coverage_stop"}      # recording off, data kept
analyze_performance {"action":"coverage_clear"}

# --- analyzer framework: via invoke_api ---
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/analyzers"}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/analyzer/trdos"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/analyzer/trdos/session","body":{"action":"activate"}}
#   ...run the scenario...
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/analyzer/trdos/events","query_params":{"limit":50}}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/analyzer/trdos/raw/fdc","query_params":{"limit":50}}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/analyzer/trdos/raw/breakpoints","query_params":{"limit":50}}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/analyzer/trdos/session","body":{"action":"deactivate"}}
invoke_api {"method":"DELETE","path":"/api/v1/emulator/{id}/analyzer/trdos/events"}
```

`coverage_start` accepts `clear` (default `true`: the recorded set is reset
first). `coverage_read` takes `max_ranges` (default 512, `0` = unlimited).
`coverage_gaps` takes `start` / `end` as an integer or a `"0x…"` string
(defaults `0x4000` / `0xFFFF`) and `max_gaps` (default 256). The tool forwards
to the same handlers as the WebAPI, so the response fields are identical.

## WebAPI

### Coverage: start, read, gaps

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/coverage/start" \
     -H 'Content-Type: application/json' -d '{"clear": true}' | jq .
# ... run the scenario ...
curl -s "$BASE/emulator/$EMU_ID/coverage?max_ranges=512" | jq '{executed_count, coverage_percent, range_count, truncated, ranges: .ranges[:5]}'
curl -s "$BASE/emulator/$EMU_ID/coverage/gaps?start=0x4000&end=0xFFFF&max_gaps=256" \
  | jq '{window, executed_in_window, coverage_percent, gap_count, truncated, gaps: .gaps[:5]}'
curl -s -X POST "$BASE/emulator/$EMU_ID/coverage/stop"  | jq .   # data kept
curl -s -X POST "$BASE/emulator/$EMU_ID/coverage/clear" | jq .
```

The coverage set is over the 64 KiB Z80 address space (`total_addresses` is
always 65536), keyed by the address of each executed instruction byte as the
CPU saw it - not by physical page. A bank-switched routine is therefore
counted at its Z80 address; for the true bank use the memory profiler in
[memory-counters.md](memory-counters.md).

### Analyzer framework: list, switch, read

```bash
curl -s "$BASE/emulator/$EMU_ID/analyzers" | jq '.analyzers'
#   → [{"id":"trdos","enabled":false}, {"id":"coverage",...}, ...]

curl -s -X PUT "$BASE/emulator/$EMU_ID/analyzer/trdos" \
     -H 'Content-Type: application/json' -d '{"enabled": true}' | jq .
# or, with the explicit session verbs (also clears the TR-DOS buffers on activate):
curl -s -X POST "$BASE/emulator/$EMU_ID/analyzer/trdos/session" \
     -H 'Content-Type: application/json' -d '{"action": "activate"}' | jq .

curl -s "$BASE/emulator/$EMU_ID/analyzer/trdos" | jq '{enabled, state, event_count, total_produced, total_evicted}'
curl -s "$BASE/emulator/$EMU_ID/analyzer/trdos/events?limit=50" | jq '.events[] | {type, formatted, frame_number}'
curl -s "$BASE/emulator/$EMU_ID/analyzer/trdos/raw/fdc?limit=50" | jq '.events[:3]'
curl -s "$BASE/emulator/$EMU_ID/analyzer/trdos/raw/breakpoints?limit=50" | jq '.events[:3]'
curl -s -X DELETE "$BASE/emulator/$EMU_ID/analyzer/trdos/events" | jq .
```

`PUT` and `POST` on `/analyzer/{name}` are the same handler. Session verbs
(`/session`) accept `activate` or `start` and `deactivate` or `stop`.

### Worked example: what did TR-DOS do for this `RUN`?

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/analyzer/trdos/session" \
     -H 'Content-Type: application/json' -d '{"action":"activate"}' >/dev/null
# drive TR-DOS: see ../run/manual-trdos-run.md
curl -s "$BASE/emulator/$EMU_ID/analyzer/trdos/events?limit=200" \
  | jq -r '.events[] | "\(.frame_number)\t\(.formatted)"'
```

`events[].formatted` is a readable one-line rendering of each event. The
event `type` is a numeric enum index: the names behind it (TR-DOS entry/exit,
command start/complete, file found / not found, module load / save, the FDC
commands restore / seek / step / read / write / read-address / read-track /
write-track, sector transfer, CRC and other errors) are declared in
`core/src/debugger/analyzers/trdos/trdosevent.h`; prefer `formatted` over the
number. Which index is which name: not confirmed here, read the header.

## Response fields worth asserting

| Call | Fields |
|:--|:--|
| `coverage/start` | `success`, `active`, `recording`, `cleared` |
| `coverage/stop` | `success`, `active`, `recording`, `executed_count`, `instructions` |
| `coverage/clear` | `success`, `executed_count` (0) |
| `GET coverage` | `active`, `recording`, `executed_count`, `total_addresses`, `coverage_percent`, `instructions`, `ranges[]` (`start` / `end` as `"0x%04X"`, `size`), `range_count`, `truncated` |
| `coverage/gaps` | `window` (`start`, `end`, `size`), `executed_in_window`, `coverage_percent` (of the window), `gaps[]` (same shape as ranges), `gap_count`, `truncated` |
| `GET analyzers` | `analyzers[]` of `id`, `enabled` |
| `GET analyzer/{name}` | `enabled`; for `trdos` also `state` (`IDLE`, `IN_TRDOS`, `IN_COMMAND`, `IN_SECTOR_OP`, `IN_CUSTOM`), `event_count`, `total_produced`, `total_evicted` |
| `analyzer/{name}/events` | `events[]`, `total_events`, `showing`; each: `timestamp`, `type`, `formatted`, `frame_number`, `flags`, `context` (`pc`, optional `caller`, `original_caller`, `im`), and when relevant `track`, `sector`, `bytes_transferred`, `filename`, `service`, `user_command`, `fdc_status`, `fdc_cmd_reg` |
| `.../raw/fdc` | per event: `tstate`, `frame_number`, `command_reg`, `status_reg`, `track_reg`, `sector_reg`, `data_reg`, `system_reg`, `pc`, `sp`, `af`/`bc`/`de`/`hl`, `im`, `stack` |
| `.../raw/breakpoints` | per event: `tstate`, `frame_number`, `address` (+ `address_label`), `page_type`, `page_index`, `page_offset`, `pc`, `sp`, main and shadow register pairs, `ix`, `iy`, `i`, `r`, `im`, `stack` |

`events`, `raw/fdc` and `raw/breakpoints` return the **last** `limit` entries
(default 100) of the ring buffer.

## Coverage over a recorded timeline (TTD)

Distinct from live coverage: when a TTD session has been recorded (see
[ttd-recording.md](ttd-recording.md) and
[ttd-reverse-debugging.md](ttd-reverse-debugging.md)), an index answers "in
which frames was this address range executed / written / read" without
re-running the machine. All three are `GET`, under
`/emulator/{id}/ttd/coverage/`:

```bash
# one frame: was 0x5B00-0x5BFF executed in frame 120?
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/probe?frame=120&kind=executed&addr_from=0x5B00&addr_to=0x5BFF" | jq .
# which frames touched a range?
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/scan?from_frame=0&kind=written&addr_from=0x4000&addr_to=0x57FF&limit=200" \
  | jq '{scanned_frames, matching_frames, first_match, last_match, truncated, frames: .frames[:10]}'
# per-bucket distinct-address counts over the timeline
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/summary?bucket_size=50&limit=100" | jq '.buckets[:5]'
```

Parameters (hex with `0x` or decimal): `kind` is `executed` (aliases `exec`,
`execute`; the default), `written` (`write`) or `read`; `addr_from` defaults to
0, `addr_to` to `0xFFFF`; `phys_page` (0..255) narrows to a physical page.
`probe` requires `frame`; `scan` and `summary` take `from_frame` (default 0)
and `to_frame` (default: the session's current end frame); `scan` `limit`
defaults to 200, `summary` `limit` to 100 (both must be at least 1);
`summary` also takes `bucket_size`, and `kind` there is optional (all three
counts come back).

Assert: `index_available` first - when `false` every result is empty and means
"no index", not "nothing happened". `probe` -> `touched`; `scan` ->
`scanned_frames`, `matching_frames`, `first_match`, `last_match`, `truncated`,
`frames[]`, plus `covered_from` / `covered_to` (the window actually covered
when your range was clamped); `summary` -> `bucket_size`, `bucket_count`,
`buckets[]` of `frame_start`, `frame_end`, `executed_distinct`,
`written_distinct`, `read_distinct`, `has_keyframe`. Bad parameters return
HTTP 400 with `{"error","message"}`.

Not confirmed from source: MCP has no dedicated action for these three routes;
reach them with `invoke_api`. Whether the index is built in both TTD journal
modes was not checked here.

## Typical uses

| Question | Recipe |
|:--|:--|
| Did my patched routine ever execute? | `coverage_start`, run, `coverage_read`; look for its range |
| Which part of a loaded program is never reached (data, dead code)? | `coverage_gaps` over the program's window |
| What did TR-DOS do during this command? | `trdos` analyzer session + `events` |
| Is the loader driving the FDC itself? | `trdos` `raw/fdc` (PC of each FDC access); see [nonstandard-loader.md](nonstandard-loader.md) |
| In which recorded frame did this address first run? | `ttd/coverage/scan` (`first_match`), then seek |
| Which RAM bank does it thrash? | not this recipe: [memory-counters.md](memory-counters.md) |

## Pitfalls

- **`coverage/start` clears by default.** Pass `{"clear": false}` (MCP:
  `"clear":false`) to add to an earlier session.
- **`stop` keeps data; `clear` drops it.** Read after `stop` is fine.
- **Coverage records only while the machine runs and the analyzer is
  active.** `GET coverage` reports `active` and `recording`; zero ranges with
  `active: false` is "not recording", not "nothing executed".
- **`coverage/start|stop|clear` pause the emulation briefly** to change
  subscriptions race-free, then resume it; a machine you left paused records
  nothing. (Whether `PUT /analyzer/{name}` and `/session` do the same: not
  confirmed.)
- **`truncated: true`** means the range / gap list hit the limit. Raise
  `max_ranges` / `max_gaps` (`0` = unlimited for ranges) or narrow the window.
- **`coverage/gaps` window defaults to `0x4000`-`0xFFFF`** (RAM), not the whole
  space; `start` greater than `end` or an address above `0xFFFF` is a 400.
- **Event-style routes exist only for `trdos`.** `events`, `raw/fdc` and
  `raw/breakpoints` on any other analyzer reply with an empty `events` array
  and a `message`, not an error. An unknown analyzer name is a 404. For
  `coverage` use the `/coverage*` routes, for `aylog` / `audiocapture` their
  own routes (not covered here).
- **`/session` bad verb** answers 400; only `activate`/`start` and
  `deactivate`/`stop` are accepted, although the missing-field message also
  names `pause` / `resume`.
- **TR-DOS buffers survive deactivate.** `DELETE .../events` clears the
  `trdos` buffers; `session activate` clears them too, the plain `PUT
  {"enabled":true}` does not (read from the handlers; not exercised here).
- **`type` in events is a number.** Assert on `formatted`, or on the numeric
  value only after checking it against `trdosevent.h`.

Design background: `docs/inprogress/2026-01-14-analyzers/`
(`TODO.md` lists what is built and what is design-only) and
`docs/inprogress/2026-01-21-trdos-analyzer/DONE.md`.
