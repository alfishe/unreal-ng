# Recipe: Memory Access Counters (Reads / Writes / Executes)

Goal: quantify *how* the machine uses memory — which pages are hot, which
addresses are read vs executed, what a routine touches — and export the
numbers.

Two related but distinct instruments:

- **Memory profiler** (`/profiler/memory/*`) — per-page and per-address
  R/W/X counters, session-scoped (start → stop), exportable. This recipe.
- **Executed-address coverage** (`/coverage/*`, MCP `coverage_*`) — which
  *code* ranges executed, and the gaps between them. Complementary; use it
  for "did my routine run at all", this recipe for "what data did it touch".

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `analyze_performance` covers the coverage instrument, `invoke_api` the
> profiler endpoints. Use [WebAPI](#webapi) only inside host-side Python/bash
> pipelines (jq post-processing of the counter arrays) or when MCP is
> unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# memory profiler via invoke_api (same paths as WebAPI):
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/memory/start"}
#   ...run the scenario: control_execution {"action":"run_frames","frames":500}...
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/memory/stop"}
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/memory/pages","query_params":{"limit":20}}
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/memory/counters",
                   "query_params":{"page":5,"mode":"physical"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/memory/save",
                   "body":{"path":"scratch/memprofile.yaml","format":"yaml"}}

# the complementary coverage instrument is first-class:
analyze_performance {"action":"coverage_start","clear":true}
analyze_performance {"action":"coverage_read","max_ranges":512}
analyze_performance {"action":"coverage_gaps","start":"0x4000","end":"0xFFFF"}
analyze_performance {"action":"frame_cost"}        # work vs idle t-states per frame
```

## WebAPI

The curl walkthrough below — right choice when jq post-processing drives the
analysis (the per-address counter arrays are multi-MB JSON).

### Start / stop

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/memory/start" | jq .
#   → starts capture and enables the underlying feature
# ... run the scenario ...
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/memory/stop" | jq .
# also: /pause /resume /clear
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/status" | jq .
#   → {"session_state":"stopped","feature_enabled":true,"tracking_mode":"physical",...}
```

Data survives `stop` (queryable) until `clear` or the next `start`.

### Per-page summaries — the first question

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/pages?limit=20" | jq '.pages[:5]'
```

```json
[
  {"page": 0, "type": "RAM", "reads": 15234, "writes": 1200, "executes": 45000},
  {"page": 5, "type": "RAM", "reads": 8900,  "writes": 990,  "executes": 0}
]
```

Interpretation patterns:

- `writes` concentrated on pages 5/7 (screen banks) → rendering loop
- `executes` on a ROM page + `reads` on a RAM page → loader copying code
- surprisingly-active page → candidate hidden bank / paging bug

Sort in jq to find the hot pages:

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/pages" \
  | jq '[.pages[]] | sort_by(-(.reads + .writes + .executes))[:8]'
```

### Per-address counters — the drill-down

```bash
# Physical addressing: one 16 KiB page's per-address counters
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?page=5&mode=physical" | jq '.counters | keys'

# Z80 addressing: the whole 64 KiB space as the CPU saw it
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?mode=z80" | jq '.counters | keys'
```

Each of `reads`/`writes`/`executes` is a flat `uint32[]` (16 384 entries in
physical mode, 65 536 in z80 mode) — index = offset (or address). Post-process
for hotspots:

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?page=5&mode=physical" \
  | jq '.counters.writes
        | to_entries
        | sort_by(-.value)[:10]
        | map({offset: (.key|tonumber), writes: .value})'
```

Monitored regions (opt-in named ranges with caller/data-value tracking) are
listed via `GET .../profiler/memory/regions`.

### Save / export

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/memory/save" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/memprofile.yaml", "format": "yaml"}' | jq .
```

Legacy aggregate view (whole-tracker snapshot, useful in a pinch):

```bash
curl -s "$BASE/emulator/$EMU_ID/memcounters" | jq .
```

### Coverage and frame cost (WebAPI)

The complementary instrument over plain REST: `POST /coverage/start|stop|clear`,
`GET /coverage`, `GET /coverage/gaps`; frame-level CPU cost is
`GET /frame_cost` (work vs idle t-states per frame).

## Typical uses

| Question | Recipe |
|:--|:--|
| Which RAM bank does this demo thrash? | start → run 500 frames → `pages` sorted by writes |
| Is the loader copying from ROM into RAM? | `pages`: ROM page reads + RAM page writes paired |
| Which addresses in the screen bank change? | `counters?mode=physical&page=5`, top writes |
| Did my patched routine ever execute? | coverage (`/coverage`), not this recipe |
| Before/after comparison of a fix | start/clear around each variant, export YAML, diff |

## Pitfalls

- **Counters are cumulative within a session** — `clear` (or
  `coverage_start` with `clear: true`) between scenarios or you'll diff
  against stale numbers.
- **Physical vs Z80**: paging models remap Z80 windows constantly; physical
  counters identify the true bank, z80 counters preserve program perspective.
  Know which question you're asking.
- **Memory cost**: tracking allocates ~72 MB of counter arrays on start —
  stop and clear when done.
- **Snapshot load mid-session** keeps counters running (they track accesses,
  not contents) — reset them explicitly if the baseline matters.
