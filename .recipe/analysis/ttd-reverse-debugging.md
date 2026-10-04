# Recipe: TTD Reverse Debugging — find-last, reverse-step, reverse-continue, coverage

Precondition: a recorded session that is **stopped/detached** — see [ttd-recording.md](ttd-recording.md).
`find-last`, `reverse-step` and `reverse-continue` return `409` while
recording is active; the coverage queries answer at any time. Session rules
(states, what wipes history, markers):
[command-interface.md → TTD Session Rules](../../docs/emulator/design/control-interfaces/command-interface.md#ttd-session-rules).

These are the "answer the question backwards" primitives:

- *Who wrote this memory?* → `find-last`
- *What ran just before the crash?* → `reverse-step` / `reverse-continue`
- *Which frames touched this address range?* → coverage probe/scan/summary

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — the
> `time_travel` tool covers find-last, reverse-step/continue and the coverage
> queries. Use [WebAPI](#webapi) only inside host-side Python/bash pipelines
> or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# coverage queries are first-class time_travel actions:
time_travel {"action":"coverage_probe","frame":11782,"kind":"executed",
             "addr_from":"0x4000","addr_to":"0x7FFF"}
time_travel {"action":"coverage_scan","from_frame":11000,"to_frame":12000,"kind":"written",
             "addr_from":"0x8000","addr_to":"0xBFFF","phys_page":5,"limit":200}
time_travel {"action":"coverage_summary","kind":"executed"}

# reverse travel (session must be stopped/detached):
time_travel {"action":"find_last","addr":16384,"access":"write"}
time_travel {"action":"find_last","addr_from":"0x4000","addr_to":"0x57FF","access":"write",
             "pc_from":"0x8000","pc_to":"0x8FFF","phys_page":5,"before_frame":11800}
time_travel {"action":"reverse_step","count":50}               # or {"tstates":2000} — exactly one
time_travel {"action":"reverse_continue","pcs":[33156,"0x82F0"]}

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
| `value` | the byte written/read, 0..255 |
| `pc_from` / `pc_to` | restrict by the PC that performed the access |
| `phys_page` | 0..255 (JSON number): only accesses to this physical RAM page |
| `before_frame` / `before_tin` | search at or before this point instead of the current position (`before_tin` only counts together with `before_frame`) |

At least one of `addr`, `addr_from`, `addr_to`, `pc_from`, `pc_to`, `value`
is required. Address, value and PC fields may be JSON numbers or strings
(`"0x4000"`, `"#4000"`, `"$4000"`, `"16384"`).

Result — the most recent match in recorded history:

```json
{
  "found": true,
  "frame": 11782, "tinframe": 2941,
  "pc": 33156, "value": 66, "phys_page": 5, "access": "write"
}
```

`phys_page` is `null` when the access had no RAM page (ROM, I/O).
`found: false` ⇒ nobody touched it in the recorded window, or a replay
barrier blocked the search — then the response also carries
`"blocked": true` and `marker_frame`, `marker_tinframe`, `marker_kind`,
`marker_reason`.

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
#   → {"reached": true, "mode": "count", "frame": 11770, "tinframe": 12345}

curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-step" \
     -H 'Content-Type: application/json' -d '{"tstates": 2000}' | jq .
```

`tstates` lands on the nearest instruction boundary ≤ target. Specifying
both `count` and `tstates` (or neither) is a 400.

### reverse-continue — run backward to a breakpoint

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' \
     -d '{"pcs": [33156, 33500]}' | jq .
#   → {"matched": true, "pc": 33156, "frame": 11753, "tinframe": 4001}
```

Non-empty `pcs` array required; execution rewinds until any PC matches.
Blocked by replay barriers like any backward travel
(`blocked_by_marker: {kind, reason, frame, tinframe}` in the response).

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
6. `seek` to the earliest hit, `resume` (this discards the recorded
   future and records again from there), watch it happen again with a
   breakpoint armed.

Full worked example: [articles/bug-hunt-ttd.md](../articles/bug-hunt-ttd.md).

## Pitfalls

- **Without the write journal** a write `find-last` still answers, the same
  way: the coverage index finds the newest frame that wrote the address and
  that frame is replayed (a few ms; a never-written address walks the whole
  index). For instant answers over a span, record or build the journal there:
  [ttd-write-journal.md](ttd-write-journal.md).
- **`find-last` `access:"io"`** matches port writes (value = byte out); it
  answers from the port journal, no replay.
- **Z80 vs physical addresses**: `find-last` works in Z80 space, reports
  `phys_page` in the result, and takes an optional `phys_page` filter;
  coverage endpoints likewise take Z80 ranges plus an optional `phys_page`.
- **Reverse travel across a marker** (tape transport command, WD1793
  sector/track write, debugger memory edit) halts with the marker
  description — replay cannot reproduce the event, so it will not cross it.
  Keyboard/mouse input is journaled and is not a barrier.
