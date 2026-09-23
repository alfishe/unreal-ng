# Recipe: TTD Reverse Debugging — find-last, reverse-step, reverse-continue, coverage

Precondition: a recorded session (development mode for the fast journal) that
is **stopped/detached** — see [ttd-recording.md](ttd-recording.md). All
endpoints below return `409` while recording is active.

These are the "answer the question backwards" primitives:

- *Who wrote this memory?* → `find-last`
- *What ran just before the crash?* → `reverse-step` / `reverse-continue`
- *Which frames touched this address range?* → coverage probe/scan/summary

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `time_travel`
> covers the coverage queries, `invoke_api` the reverse-travel endpoints. Use
> [WebAPI](#webapi) only inside host-side Python/bash pipelines or when MCP
> is unavailable (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# coverage queries are first-class time_travel actions:
time_travel {"action":"coverage_probe","frame":11782,"kind":"executed",
             "addr_from":"0x4000","addr_to":"0x7FFF"}
time_travel {"action":"coverage_scan","from_frame":11000,"to_frame":12000,"kind":"written",
             "addr_from":"0x8000","addr_to":"0xBFFF","phys_page":5,"limit":200}
time_travel {"action":"coverage_summary","kind":"executed"}

# reverse travel via invoke_api (not yet in the time_travel tool):
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/ttd/find-last",
             "body":{"addr":16384,"access":"write"}}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/ttd/reverse-step","body":{"count":50}}
invoke_api  {"method":"POST","path":"/api/v1/emulator/{id}/ttd/reverse-continue","body":{"pcs":[33156]}}

# follow-ups once find-last names a writer PC:
debug_code  {"action":"disassemble","address":"0x8174","count":12}   # pc-16 window (= /disasm)
time_travel {"action":"bookmark_add","label":"writer"}               # pin the finding
```

## WebAPI

The curl walkthrough below — right choice when scripting the
reverse-continue "previous N hits" loop in bash.

### find-last — the last access matching a query

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" \
     -H 'Content-Type: application/json' \
     -d '{"addr": 16384, "access": "write"}' | jq .
```

Query fields (combine freely; at least one criterion required):

| Field | Meaning |
|:--|:--|
| `addr` | exact Z80 address |
| `addr_from` / `addr_to` | address range (use instead of `addr`) |
| `access` | `"write"` (default) / `"read"` / `"execute"` / `"io"` |
| `value` | the byte written/read (writes & reads) |
| `pc_from` / `pc_to` | restrict by the PC that performed the access |
| `before` | only matches strictly before this frame |

Result — the most recent match in recorded history:

```json
{
  "found": true,
  "frame": 11782, "tstate": 2941,
  "addr": 16384, "value": 66, "pc": 33156, "physpage": 5
}
```

`found: false` ⇒ nobody touched it in the recorded window (or a replay
barrier blocked the search — the response tells you which marker).

Classic triage chain: value went bad at address X →
`find-last {addr:X, access:"write"}` gives the writing `pc` →
`GET /disasm?address=<pc-16>` shows the code →
`find-last {addr:X, access:"write", pc_from:<pc>, pc_to:<pc>}` counts
predecessors.

### reverse-step — walk backward deterministically

```bash
# Back 1 instruction, 50 instructions, or 2000 t-states (exactly one of the two):
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-step" \
     -H 'Content-Type: application/json' -d '{"count": 50}' | jq .
#   → {"reached": true, "frame": 11770, "tinframe": 12345}

curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-step" \
     -H 'Content-Type: application/json' -d '{"tstates": 2000}' | jq .
```

`tstates` lands on the nearest instruction boundary ≤ target. Specifying
both `count` and `tstates` is a 400.

### reverse-continue — run backward to a breakpoint

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' \
     -d '{"pcs": [33156, 33500]}' | jq .
#   → {"matched": true, "pc": 33156, "frame": 11753, "tinframe": 4001}
```

Non-empty `pcs` array required; execution rewinds until any PC matches.
Blocked by replay barriers like any backward travel
(`blocked_by_marker` in the response).

Loop it from bash for "previous N hits":

```bash
PCS='[33156]'
for i in 1 2 3 4 5; do
  R=$(curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
       -H 'Content-Type: application/json' -d "{\"pcs\": $PCS}")
  echo "$R" | jq -c '{pc, frame, tinframe}'
  # optionally reverse-step once more so the next continue finds an earlier hit
  curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-step" \
       -H 'Content-Type: application/json' -d '{"count": 1}' >/dev/null
done
```

### Coverage index — which frames touched a range

Three GET endpoints:

```bash
# Did frame 11782 execute anything in $4000-$7FFF?
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/probe?frame=11782&kind=executed&addr_from=16384&addr_to=32767" | jq .
#   → {"touched": true, "index_available": true, ...}

# Which frames in [11000, 12000] wrote to page 5 of that range?
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/scan?from_frame=11000&to_frame=12000&kind=written&addr_from=32768&addr_to=49151&phys_page=5&limit=200" \
  | jq '{matching_frames, first_match, last_match, truncated}'

# Activity heatmap over the whole session (auto bucketing)
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/summary?kind=executed" \
  | jq '.buckets[:5]'
```

- `kind`: `executed` / `written` / `read`
- `phys_page` optional (0–255) for physical addressing
- frames outside the indexed window answer `index_available: false` — not
  "no", just "unknown"
- Use scan to shortlist frames, then `seek` to them and inspect

## Workflow: crash post-mortem (summary)

1. Record through the crash in development mode
   ([ttd-recording.md](ttd-recording.md)); stop.
2. Bookmark the crash position: `POST /ttd/bookmarks {"label":"crash"}`.
3. `find-last` the corrupted address → writer PC.
4. `reverse-continue {"pcs":[writer]}` → the call site that set it up.
5. `coverage/scan` the routine's range → which frames to replay.
6. `seek` to the earliest hit, `resume`, watch it happen again with a
   breakpoint armed.

Full worked example: [articles/bug-hunt-ttd.md](../articles/bug-hunt-ttd.md).

## Pitfalls

- **Journal-off sessions** still answer these queries — via checkpoint
  replay, orders of magnitude slower on long timelines. Prefer development
  mode when you plan to ask questions.
- **`find-last` `access:"io"`** matches port writes (value = byte out).
- **Z80 vs physical addresses**: `find-last` works in Z80 space with a
  `physpage` in the result; coverage endpoints take either Z80 ranges or an
  explicit `phys_page`.
- **Reverse travel across input markers** halts with the marker description —
  the machine state before an external event cannot be re-derived past it.
