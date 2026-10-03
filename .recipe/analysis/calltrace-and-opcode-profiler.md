# Recipe: Call Trace, Opcode and Unified Profilers (and Frame Cost)

Goal: answer "who calls this routine?", "which instructions dominate?" and
"how much of each frame is real work?" with the profiler sessions under
`/emulator/{id}/profiler/*` and the frame-cost counter.

Which instrument for which question:

| Question | Instrument |
|:--|:--|
| Who calls / jumps to a routine, what path led here | **Call trace** (`profiler/calltrace/*`): every taken JP, JR, CALL, RST, RET, RETI, DJNZ |
| Which opcodes execute most (hot instructions, prefix mix) | **Opcode profiler** (`profiler/opcode/*`): per-opcode counters plus a recent-instruction trace |
| Which memory pages/addresses are read, written, executed | **Memory profiler** — see [memory-counters.md](memory-counters.md) |
| Which port reads/writes happen | **Port trace** — see [port-trace.md](port-trace.md) |
| How much of a frame is work versus HALT idle | **Frame cost** (`/frame_cost`, MCP `frame_cost`) |

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the
> `analyze_performance` and `debug_code` tools cover the one-shot and unified
> forms, `invoke_api` reaches the per-profiler endpoints. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines (jq
> post-processing) or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# one-shot call trace: start -> run N frames -> stop -> read entries
debug_code          {"action":"trace","frames":50,"limit":64}
#   frames 1-1000 (default 10), limit 1-1024 (default 64)

# unified session over all three profilers (memory + calltrace + opcode)
analyze_performance {"action":"profile_start"}
control_execution   {"action":"run_frames","frames":200}
analyze_performance {"action":"profile_stop"}
analyze_performance {"action":"profile_status"}
analyze_performance {"action":"profile_report","limit":32}
#   report = opcode counters (top `limit`) + memory status + calltrace entries + unified status

# per-frame cost (work versus HALT idle)
analyze_performance {"action":"frame_cost"}

# individual profilers via invoke_api (same paths as WebAPI)
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/opcode/start"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/opcode/stop"}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/opcode/counters","query_params":{"limit":20}}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/opcode/trace","query_params":{"count":50}}

invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/calltrace/start"}
invoke_api {"method":"POST","path":"/api/v1/emulator/{id}/profiler/calltrace/stop"}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/calltrace/status"}
invoke_api {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/calltrace/entries","query_params":{"count":200}}
```

Notes on the MCP forms (from `mcp-analysis.cpp`):

- `debug_code trace` and `profile_report` append `?limit=` to the calltrace
  `entries` request, but that handler reads the query parameter `count`
  (see [Pitfalls](#pitfalls)). To control the entry count use `invoke_api`
  with `count`.
- `profile_start` / `profile_stop` / `profile_status` map to
  `POST /profiler/start`, `POST /profiler/stop`, `GET /profiler/status`.
  There is no MCP action for the unified `pause`, `resume` or `clear`; use
  `invoke_api` for those.
- `debug_code trace` stops the session before reading, which flushes pinned
  tight-loop events into the buffer the entries endpoint reads.

## WebAPI

The curl walkthrough below — right choice when jq post-processing drives the
analysis. `$BASE` is `http://localhost:8090/api/v1`; `$EMU_ID` is the
instance id ([_common/setup.md](../_common/setup.md)).

### Feature gates

Each profiler depends on a runtime feature, and the `start` endpoints turn
the needed features on themselves:

| Endpoint | Features it enables |
|:--|:--|
| `profiler/opcode/start` | `opcodeprofiler` |
| `profiler/calltrace/start` | `debugmode`, `calltrace` |
| `profiler/start` (unified) | `debugmode`, `memorytracking`, `calltrace`, `opcodeprofiler` |

All four are `state = off` in the shipped `features.ini`. The `status`
endpoints report `feature_enabled`; check it if a session appears to record
nothing.

### Call trace

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/calltrace/start" | jq .
#   -> {"profiler":"calltrace","action":"start","session_state":..., ...}
# ... run the scenario (control_execution run_frames, or let it free-run) ...
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/calltrace/stop" | jq .
# also: /pause /resume /clear
curl -s "$BASE/emulator/$EMU_ID/profiler/calltrace/status" | jq .
curl -s "$BASE/emulator/$EMU_ID/profiler/calltrace/entries?count=200" | jq '.entry_count, .total_count, .entries[:5]'
```

`status` returns `session_state` (`stopped` | `capturing` | `paused`),
`capturing`, `feature_enabled`, and, when the buffer exists, `entry_count`
and `buffer_capacity`.

`entries` returns `entry_count` (returned now), `total_count` (in the
buffer) and `entries[]`, each with:

| Field | Meaning |
|:--|:--|
| `type` | integer enum: 0 JP, 1 JR, 2 CALL, 3 RST, 4 RET, 5 RETI, 6 DJNZ |
| `from_address` | address of the control-flow instruction (PC at its M1) |
| `to_address` | resolved target |
| `sp` | stack pointer after the instruction |
| `loop_count` | consecutive repeats compressed into this entry |

`count` defaults to 100. The handler does not document an upper bound.

Who calls a routine (here `0x0556`):

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/calltrace/entries?count=1000" \
  | jq '[.entries[] | select(.type==2 and .to_address==1366)
         | {from: .from_address, sp, loop_count}]'
```

Compare `from_address` against loaded labels with `GET /labels/resolve` —
see [symbols-listings-and-source-stepping.md](symbols-listings-and-source-stepping.md).

### Opcode profiler

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/opcode/start" | jq .
# ... run the scenario ...
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/opcode/stop"  | jq .
# also: /pause /resume /clear
curl -s "$BASE/emulator/$EMU_ID/profiler/opcode/status" | jq .
curl -s "$BASE/emulator/$EMU_ID/profiler/opcode/counters?limit=20" | jq '.counters'
curl -s "$BASE/emulator/$EMU_ID/profiler/opcode/trace?count=50"    | jq '.trace[:5]'
```

Fields worth asserting:

- `start` / `pause` / `resume` / `clear` replies carry `profiler:"opcode"`,
  `action`, `session_state`; `stop` additionally returns `total_executions`
  and `trace_size`.
- `status`: `capturing`, `total_executions`, `trace_size`,
  `trace_capacity`, `feature_enabled`.
- `counters` (top `limit`, default 100): `total_executions`, `limit`,
  `count`, and `counters[]` of `{prefix, opcode, count, prefix_name}`;
  `prefix_name` is one of `none`, `CB`, `DD`, `ED`, `FD`, `DDCB`, `FDCB`.
- `trace` (last `count`, default 100): `trace_size`, `requested_count`,
  `returned_count`, and `trace[]` of
  `{pc, prefix, opcode, flags, a, frame, tstate}`.

Hot-opcode share, computed client-side:

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/opcode/counters?limit=10" \
  | jq '.total_executions as $t
        | .counters | map({prefix_name, opcode, count, share: (.count / $t)})'
```

### Unified profiler (memory + calltrace + opcode together)

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/start"  | jq .status
# ... run ...
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/stop"   | jq .status
# also: POST /profiler/pause, /profiler/resume, /profiler/clear
curl -s "$BASE/emulator/$EMU_ID/profiler/status" | jq .profilers
```

`start|stop|pause|resume|clear` reply with `action`, `message` and
`status.{memory,calltrace,opcode}` session-state strings. `GET
/profiler/status` returns `profilers.memory`, `profilers.calltrace` and
`profilers.opcode`, each with `session_state`, `capturing`, `feature_enabled`
(plus `entry_count` for calltrace and `total_executions` / `trace_size` for
opcode). Data from the memory half is read with the endpoints in
[memory-counters.md](memory-counters.md).

### Frame cost

```bash
curl -s "$BASE/emulator/$EMU_ID/frame_cost" | jq .
```

Fields: `frame`, `frame_tstates` (frame budget including the current CPU
frequency multiplier), `tstates_per_line`, `frames_per_second`,
`current_frame_halted`, and two blocks `last_frame` and `average`, each with
`tstates_total`, `tstates_halted`, `tstates_active`, `halted_percent`,
`active_percent` (`average` also has `frames`). "Halted" is time spent in the
HALT state, so `active_percent` is the share of the frame the program
actually computed.

## Typical workflow

1. Pick the instrument from the table above.
2. `clear` so earlier data does not mix in (whether `start` also resets data
   is not confirmed from the handlers).
3. Start, run a bounded scenario (`control_execution {"action":"run_frames","frames":200}`),
   stop.
4. Read: `counters` for hot opcodes, `entries` for call paths,
   `frame_cost` for budget.
5. Stop and clear when done; sessions hold buffers.

## Pitfalls

- **The legacy `GET /emulator/{id}/calltrace` is a stub.** Its handler
  returns `calltrace_enabled`, a `message`, and an always-empty `entries`
  array (the source carries a TODO for real entries). Use
  `profiler/calltrace/entries`.
- **`limit` versus `count`.** The calltrace `entries` handler reads `count`
  only, so the `limit` that `debug_code trace` and `profile_report` send is
  not applied to it (default 100 entries). The opcode `counters` endpoint
  reads `limit`; the opcode `trace` endpoint reads `count`.
- **Stop before reading calltrace.** `calltrace/stop` flushes pinned hot
  (tight-loop) events into the buffer that `entries` reads; reading while
  `capturing` can miss them.
- **Feature gates.** Features are off by default; `start` enables them, but
  turning `calltrace` or `opcodeprofiler` off afterward (feature API) leaves
  a session that records nothing. Check `feature_enabled` in `status`.
- **HTTP 500 instead of 409.** These handlers do not use 409: an emulator
  with an unknown id returns 404; a missing profiler/tracker/buffer returns
  500 with `message` (for example "Opcode profiler not available").
- **Pause races.** `run_frames` is synchronous and leaves the machine
  paused, so read after it; on a free-running machine `stop` or `pause`
  the session first, then read.
- **Loop compression.** Repeated identical control-flow events collapse into
  one entry with `loop_count > 1`; sum `loop_count` rather than counting entries.
- **Cost.** Per-instruction profilers slow emulation; do not leave them
  running after the measurement.
- Unconfirmed: the entry count above which `calltrace/entries` truncates, and
  whether the opcode counters distinguish repeated runs after `stop` without
  `clear` (the source clears only on `clear`).
