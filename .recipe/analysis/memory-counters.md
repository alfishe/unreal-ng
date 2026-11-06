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
# session control
analyze_performance {"action":"profiler","profiler":"memory","op":"start"}
#   ...run the scenario: control_execution {"action":"run_frames","frames":500}...

# per-address counters: every cell is a text line (0x5C08 r=12 w=3 x=0)
analyze_performance {"action":"memory_counters","view":"top","kinds":"x","limit":16}          # hot code
analyze_performance {"action":"memory_counters","space":"phys","view":"top","kinds":"w"}      # hot variables, with the bank
analyze_performance {"action":"memory_counters","addresses":"0x5C00,0x9C40-0x9C43"}          # exactly these cells
analyze_performance {"action":"memory_counters","space":"ram5","view":"sparse","start":"0x1800","length":"0x300"}

# watches: a region or a partially decoded port, with the PCs and the values
analyze_performance {"action":"memory_watch","op":"add","name":"lives","start":"0x9C40","length":"1","callers":true,"values":true}
analyze_performance {"action":"memory_watch","op":"add","name":"kempston","port":"0x1F","mask":"0xFF","callers":true}
analyze_performance {"action":"memory_watch"}                                                  # list (the default op)

invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/memory/save",
                   "body":{"path":"scratch/memprofile.yaml","format":"yaml"}}

# unified profiler control (opcode counters + memory status in one report):
analyze_performance {"action":"profile_start"}     # also profile_stop, profile_status, profile_report

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
physical mode, 65 536 in z80 mode) — index = offset from `start` (0 by
default). `mode` defaults to `z80`; `physical` needs `page=N`, the absolute
page index `/pages` lists.

Narrow the window and ask only for touched addresses — far smaller for a game:

```bash
# only the addresses a routine or game touched, as [address, reads, writes, executes]
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?mode=z80&start=0x5B00&end=0xFFFF&format=sparse" \
  | jq '.count, .addresses[:5]'
```

A typical use: after a session covering play, addresses that are **read but
never executed or written** are the game's constant data (graphics, fonts,
maps, tables); addresses that are **written** are variables and buffers.

Post-process the dense arrays for hotspots:

```bash
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?page=5&mode=physical" \
  | jq '.counters.writes
        | to_entries
        | sort_by(-.value)[:10]
        | map({offset: (.key|tonumber), writes: .value})'
```

The `MemoryCounters` form takes a space and returns cells, so a client never downloads the 64K arrays:

```bash
# the 16 most executed CPU addresses
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?space=cpu&format=top&kinds=x&limit=16" | jq -c '.cells[]'
# the most written cells of the whole physical memory, each with its page ([address, r, w, x, "ram5"])
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?space=phys&format=top&kinds=w&limit=32" | jq -c '.cells[]'
# exactly these cells, zeros too
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/counters?addresses=0x5C00,0x9C40-0x9C43" | jq -c '.cells[]'
# a page's window as raw uint32 arrays (reads, writes, executes) for numpy
curl -s -o scratch/ram5.bin "$BASE/emulator/$EMU_ID/profiler/memory/counters?space=ram5&format=binary"
```

Watches are named regions and ports, with the PCs (callers) and values seen:

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/memory/watches" -H 'Content-Type: application/json' \
     -d '{"name":"kempston","port":"0x1F","mask":"0xFF","callers":true,"values":true}' | jq .
curl -s "$BASE/emulator/$EMU_ID/profiler/memory/watches" | jq '.watches[] | {name, reads, writes, callers}'
curl -s -X DELETE "$BASE/emulator/$EMU_ID/profiler/memory/watches/kempston"
```

The `debug.watches` WebSocket topic pushes the totals at every frame's end. The `/memory/regions` and
`/memory/region/{name}` endpoints are a different feature (named memory region access), not profiler counters.
Python: `emu.memory_counters(...)`, `emu.memory_watch_port(...)`; Lua: `memory_counters{...}`,
`memory_watch_port(...)`; CLI: `profiler memory counters ...`, `profiler memory watch ...`
([module page](../../docs/emulator/design/debugger/memory-counters.md)).

### Save / export

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/memory/save" \
     -H 'Content-Type: application/json' \
     -d '{"path": "scratch/memprofile.yaml", "format": "yaml"}' | jq .
```

`single_file: true` writes one file; the default writes a directory of files
next to `path`. The reply's `path` names what was written.

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
| Which addresses in the screen bank change? | `counters?space=ram5&format=top&kinds=w` |
| What is a POKE address: code, constant or variable? | `counters?addresses=...` after a run: executed = code, read only = constant, written = variable |
| Which input does a game read? | port watches `0x1F`/`0xFF` and `0xFE`/`0xFF` ([game-start-input.md](game-start-input.md)) |
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
