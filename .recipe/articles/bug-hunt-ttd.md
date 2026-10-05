# Article: "Who Corrupted This Memory?" — a Full TTD Bug Hunt

A worked end-to-end workflow combining
[snapshots](../media/load-snapshot.md),
[TTD recording](../analysis/ttd-recording.md) and
[reverse queries](../analysis/ttd-reverse-debugging.md).

Scenario: a program works for a while, then misbehaves — screen glitches,
wrong values, a jump into nowhere. You suspect a memory corruption but the
classic breakpoint hunt is a slog. TTD turns it into three API calls.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred — `time_travel`
> for the whole TTD side (record, find-last, reverse-continue, seek,
> bookmarks, dump, coverage), `debug_code` disassembly, `invoke_api` only
> for snapshot save and breakpoints. Use [WebAPI](#webapi) for scripted
> repeatable hunts (bash) or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# phase 0 — reach the bug deterministically:
emulator_manage    {"action":"create","model":"128k"}                       # → id
load_software      {"path":"scratch/buggy.trd","autostart":true}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/snapshot/save",
                   "body":{"path":"scratch/prebug.sna"}}

# phase 1 — record through the failure (development mode):
time_travel        {"action":"start"}
control_execution  {"action":"resume"}      # ...poll, then {"action":"pause"} at the symptom
inspect_state      {"aspects":["screen_ocr"]}                        # symptom on screen?
time_travel        {"action":"stop"}
time_travel        {"action":"bookmark_add","label":"symptom"}
time_travel        {"action":"dump","path":"scratch/bughunt.ttd"}

# phase 2 — the three questions:
time_travel        {"action":"find_last","addr":16384,"access":"write","value":0}
debug_code         {"action":"disassemble","address":"0x8174","count":12}   # code at the writer PC
time_travel        {"action":"coverage_scan","from_frame":11000,"to_frame":11800,"kind":"executed",
                   "addr_from":"0x816C","addr_to":"0x81B0"}
time_travel        {"action":"reverse_continue","pcs":[33156]}

# phase 3 — replay and confirm:
time_travel        {"action":"seek","frame":11781}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/breakpoints",
                   "body":{"type":"write","address":16384}}
time_travel        {"action":"resume"}      # truncates the future, records again, resumes the emulator
```

The full narrative with sample responses and the bash reverse-continue loop
lives in the WebAPI walkthrough below.

## WebAPI

The curl walkthrough below — right choice for scripted, repeatable hunts
(the whole point of Phase 0 determinism).

### Phase 0 — reach the bug, deterministically

```bash
# setup per _common/setup.md: fresh app, fresh instance
EMU_ID=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
         -d '{"model":"128k"}' | jq -r .id)

# reach the interesting state however you like:
#   snapshot load, autostart disk, tape fastload — your call
curl -s -X POST "$BASE/emulator/$EMU_ID/disk/A/insert" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/buggy.trd","autostart":true}' | jq '.autostarted'

# anchor "known good pre-bug" (belt and braces)
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/prebug.sna"}' >/dev/null
```

### Phase 1 — record through the failure

```bash
# development mode: we will ask write-history questions
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" \
     -H 'Content-Type: application/json' -d '{}' | jq '.state'

# let it run to the visible failure (free-run here; run_frames if you know the frame budget)
curl -s -X POST "$BASE/emulator/$EMU_ID/resume" >/dev/null
# ... poll OCR/digest until the symptom shows, or a fixed wall-time ...
curl -s -X POST "$BASE/emulator/$EMU_ID/pause" >/dev/null
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r .text     # symptom on screen?

# freeze the timeline
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop" | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/bookmarks" \
     -H 'Content-Type: application/json' -d '{"label":"symptom"}' | jq .
```

Save the evidence while it's hot (captures outlive the instance):

```bash
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/dump" \
     -H 'Content-Type: application/json' -d '{"path":"scratch/bughunt.ttd"}' | jq .
```

### Phase 2 — ask the three questions

Identify the corrupted location first (screen RAM from the symptom, a
variable from the program's map, or scan for the wrong value):

```bash
# Q1: who last WROTE 0x00 over the top of screen line 0 (addr 0x4000)?
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" \
     -H 'Content-Type: application/json' \
     -d '{"addr": 16384, "access": "write", "value": 0}' | jq .
#   → {"found":true, "frame":11782, "tinframe":2941, "pc":33156, "value":0,
#      "phys_page":5, "access":"write"}
#   (a replay barrier in the way → "found":false, "blocked":true, "marker_kind", ...)
```

```bash
# Q2: what code is at that writer PC? (disasm window around it)
curl -s "$BASE/emulator/$EMU_ID/disasm?address=33140&count=12" \
     | jq -r '.instructions[] | "\(.address): \(.mnemonic)"'
```

```bash
# Q3: where was that routine called FROM the last times it ran?
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' \
     -d '{"pcs":[33156]}' | jq .
# step back one instruction, repeat reverse-continue → the call chain
```

Corroborate with coverage — how often did the suspect range run:

```bash
curl -s "$BASE/emulator/$EMU_ID/ttd/coverage/scan?from_frame=11000&to_frame=11800&kind=executed&addr_from=33100&addr_to=33200" \
     | jq '{matching_frames, first_match, last_match}'
```

### Phase 3 — replay and confirm

```bash
# rewind to just before the write (the frame's start) and watch it happen
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/seek" \
     -H 'Content-Type: application/json' \
     -d '{"frame": 11781, "tinframe": 0}' | jq '{reached, arrived_at}'

# arm a watchpoint-style breakpoint on the address, then resume live
# (breakpoints are a feature, off by default)
curl -s -X PUT "$BASE/emulator/$EMU_ID/feature/breakpoints" -H 'Content-Type: application/json' -d '{"enabled": true}'
curl -s -X POST "$BASE/emulator/$EMU_ID/breakpoints" \
     -H 'Content-Type: application/json' \
     -d '{"type":"write","address":16384}' | jq .
# ttd/resume truncates the recorded future at this point, records again
# and resumes the emulator itself (the extra /resume is a harmless no-op)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/resume" | jq '.state'
curl -s -X POST "$BASE/emulator/$EMU_ID/resume" >/dev/null
# breakpoint fires with the exact writer PC and registers — case closed
```

### Phase 4 — archive and hand off

```bash
# the .ttd from Phase 1 plus:
curl -s -X POST "$BASE/emulator/$EMU_ID/snapshot/save" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/symptom.sna"}' >/dev/null
# findings: writer PC, call chain, frames — a colleague (or agent) can
# ttd/load the same file on an instance of the SAME model (a model mismatch
# is refused with both model ids named), then POST ttd/seek
# {"bookmark":"symptom"} to land on it verbatim (after load the session is
# idle; bookmarks travel inside the .ttd).
```

## Why this beats breakpoint ping-pong

| Classic | TTD |
|:--|:--|
| set watchpoint → wait → miss → re-arm | record once, ask `find-last` |
| crash → restart → hope it repeats | seek back, deterministic replay |
| "who called this?" → guess + breakpoints | `reverse-continue` walks the real chain |
| evidence lives in your session | `.ttd` + snapshot are shareable artifacts |

## Checklist

- [ ] recording started in **development mode** (journal on)
- [ ] scrubbing while recording pauses it (`recording_paused`; on `backend: v1` a 409 - `ttd/stop` first)
- [ ] no tape/disk load between `ttd/start` and the analysis — it is refused
      while recording and wipes a stopped session (a reset does not; a
      snapshot load on the engine is part of the recording)
- [ ] bookmark + dump **before** heavy experimentation
- [ ] `find-last` with `value`/`pc_from` filters to narrow repeat offenders
- [ ] verify the fix by re-running Phase 1 and proving `find-last` now names
      the *expected* writer (or finds nothing)
