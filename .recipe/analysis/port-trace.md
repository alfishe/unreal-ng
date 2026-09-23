# Recipe: Port Trace (Port Diagnostic Recorder)

Goal: record every `IN`/`OUT` the machine performs — with filters, a bounded
buffer, server-side saves, and offline analysis.

The recorder is gated by the runtime feature `"porttrace"` (alias `"pt"`).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the
> `analyze_performance` one-shot answers quick questions, `invoke_api` drives
> full sessions. Use [WebAPI](#webapi) only inside host-side Python/bash
> pipelines (the `porttrace_*.py` tools) or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# one-shot — the default instrument for "what did this routine touch?":
analyze_performance {"action":"porttrace","frames":120,"limit":40}
#   → runs exactly N paused-forward frames (1–1000), returns the last `limit` events

# full session via invoke_api (same paths as WebAPI):
invoke_api  {"method":"PUT", "path":"/api/v1/emulator/{id}/feature/porttrace","body":{"enabled":true}}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/config",
             "body":{"capacity":4000000,"overflow":"ring"}}            # only while stopped
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/filter","body":{"preset":"fdc-only"}}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/start"}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/stop"}
invoke_api  {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/porttrace/status"}
invoke_api  {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/porttrace/events","query_params":{"limit":50}}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/save",
             "body":{"path":"scratch/fdc-trace.binz","format":"binz"}}
```

## WebAPI

The curl walkthrough below — right choice when the host-side Python tools
(`porttrace_capture.py`, `porttrace_gui.py`) drive the capture and the
offline analysis.

### Turn it on

```bash
# 1. Enable the runtime feature (idempotent)
curl -s -X PUT "$BASE/emulator/$EMU_ID/feature/porttrace" \
     -H 'Content-Type: application/json' -d '{"enabled": true}' | jq .

# 2. Configure the buffer (ONLY while stopped — 409 otherwise)
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/config" \
     -H 'Content-Type: application/json' \
     -d '{"capacity": 4000000, "overflow": "ring"}' | jq .
#    ring = keep newest when full; stop = keep the start of the run (auto-stop)

# 3. Arm a filter (optional)
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/filter" \
     -H 'Content-Type: application/json' \
     -d '{"preset": "fdc-only"}' | jq '.filter'
```

Presets: `all` `ay-only` `fdc-only` `no-fdc` `no-fe` `sound` `paging`
`outs-only` `ins-only` `unmapped`.

Custom rules — compound objects, AND inside a rule, OR across rules, exclude
wins over include:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/filter" \
     -H 'Content-Type: application/json' \
     -d '{"include": [{"port": "FFFD", "direction": "out"},
                      {"port": "BFFD", "direction": "out"}],
          "exclude": [{"device": "WD1793_Data"}]}' | jq '.filter'
```

Condition keys: `port` (decoded, hex string), `raw` (raw 16-bit port),
`device`, `direction` `in|out`, `pc` range, `value` range, `unmapped: true`.

### Capture

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/start" | jq '.session.state'  # clears buffer
# ... run the scenario (free-run, run_frames, whatever the test needs) ...
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/stop" | jq '.session'
# also available: /pause /resume /clear
```

Live counters while capturing:

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/porttrace/status" | jq '.session | {
  state, events, capacity, total_produced, total_evicted, total_filtered,
  auto_stopped, overflow, filter, activity}'
```

`activity` is the current frame's port summary (`in`, `out`,
`unmapped_in/out`, `beta128_gated`) — a cheap "is anything happening" probe.
`auto_stopped: true` means the buffer filled with `overflow: stop`.

### Read events

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/porttrace/events?limit=50" | jq '.events[:3]'
```

Query params: `limit` (default 0 = all — beware multi-MB responses), `since`
(t-state floor), `offset`/`count` (windowed retrieval).

Event fields (compact numeric form): `ts` (t-state), `frame`, `raw` port,
`dec` (decoded port), `rule` (decode-rule index), `val` (byte), `pc`,
`dev` (device id), `flags`.

### Save and analyze

```bash
# Server-side save — JSON carries the decode-rule table; binz is ~50-100x smaller
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/save" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/fdc-trace.binz", "format": "binz"}' | jq .
#   → {"saved": 128453, "path": "...", "format": "binz"}

# Re-read ANY saved binary trace server-side (PTRC v1 + PTR2 v2, decompressed for you)
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/readfile" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/fdc-trace.binz", "limit": 10}' | jq '.events'
```

Offline conversion / analysis:

```bash
# Capture N seconds straight from the shell, auto-enable + auto-size buffer:
python3 tools/porttrace/porttrace_capture.py --duration 5 --preset fdc-only \
        --to json,csv,markdown -o scratch/fdc

# Interactive GUI (filters, live counters, summaries, strictness report):
python3 tools/porttrace/porttrace_gui.py
```

Analysis questions the tools answer: per-port/per-device histograms, IN/OUT
ratios, unmapped-access detection (probing undiscovered ports), decode-rule
coverage, PC attribution ("who hammers port X").

## Pitfalls

- **Feature off → 400** on the session endpoints. Enable first (the Python
  tool does it unless `--no-enable`).
- **Reconfiguring mid-run → 409**: `capacity`/`overflow`/filter changes
  belong to the "applied on Start" phase — stop, reconfigure, start.
- **Ring buffer lies by omission**: with `overflow: ring` the *start* of a
  long run is evicted. If you care about the beginning, use `overflow: stop`
  or a bigger `capacity` (events are ~20 bytes; 250k events/s is the
  worst-case rate to size for).
- **`events` with no `limit`** on a 4M-event buffer is a ~100 MB JSON
  response — always page or save-to-file instead.
- **Beta128 gating**: TR-DOS paged-in accesses can be suppressed by the
  `beta128_gated` counter — compare `total_produced` vs `events` before
  assuming the disk controller was silent.
