# Article: Disk Protection Triage — Catalog vs Raw Sectors vs Port Trace

A worked workflow for diagnosing disks that "don't boot", hang in the
loader, or behave differently from a reference emulator: combine
[catalog inspection](../media/insert-disk.md), raw sector access, live FDC
state and a [port trace](../analysis/port-trace.md).

Companions: proving the loader is non-standard and tracing crashes —
[nonstandard-loader.md](../analysis/nonstandard-loader.md); physical
protection layouts (flaky sectors, clock marks) and image authoring —
[physical-protection-forensics.md](physical-protection-forensics.md),
[author-udi-images.md](../media/author-udi-images.md).

Typical protection tells: fake catalog entries, non-standard sector
layouts/IDs, weak/short sectors, controller-state polling loops, reads
outside the filesystem area, timing checks on the FDC interrupt.

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `inspect_state` decodes the FDC, `invoke_api` covers catalog, raw sectors
> and the trace endpoints. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines or when MCP is unavailable (policy:
> [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# steps 1-2 — what the disk claims vs what is on the metal:
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sysinfo"}
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/catalog"}
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/sector/0/0/1/raw"}
invoke_api         {"method":"GET","path":"/api/v1/emulator/{id}/disk/A/track/0/0/raw"}

# step 3 — live controller state in one decoded call:
inspect_state      {"aspects":["fdc"]}

# step 4 — the port trace around the load attempt:
invoke_api         {"method":"PUT", "path":"/api/v1/emulator/{id}/feature/porttrace","body":{"enabled":true}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/config",
                   "body":{"capacity":2000000,"overflow":"stop"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/filter","body":{"preset":"fdc-only"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/start"}
control_execution  {"action":"run_frames","frames":600}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/stop"}
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/porttrace/events","query_params":{"limit":40}}

# step 5 — prove the hypothesis with TTD (record first: ../analysis/ttd-recording.md):
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/find-last","body":{"addr":63,"access":"io"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/reverse-continue","body":{"pcs":[33156]}}
```

Mitigation patching is `debug_code` action `assemble` (two-pass Z80
assembler writing straight into memory) — see Mitigations below.

## WebAPI

The curl walkthrough below — right choice when a bash pipeline chains the
five steps.

### Step 1 — What does the disk claim to be?

```bash
curl -s "$BASE/emulator/$EMU_ID/disk/A/sysinfo" | jq .     # TR-DOS system sector: free sectors, labels
curl -s "$BASE/emulator/$EMU_ID/disk/A/catalog" | jq '{file_count, files: [.files[] | {name, type, length, sectors}]}'
```

Anomalies visible from the host side alone:

- catalog entry with `length` inconsistent with `sectors` (`length` should
  be `sectors*256` rounded up in TR-DOS terms) → fake/obfuscated entry
- file claiming to start at `first_track`/`first_sector` inside the reserved
  system area → deliberate overlap
- `file_count` vs free-space arithmetic in `sysinfo` mismatching → hand-patched
  directory (protection or damage)

### Step 2 — What is actually on the metal?

Read raw sectors (data and flux-level streams):

```bash
# Decoded sector data
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/1" | jq '{data_size, has_data, data_base64}' 

# RAW stream: bytes from the ID address mark through the data CRC — this is
# where non-standard sector IDs, gaps and CRC tricks show up
curl -s "$BASE/emulator/$EMU_ID/disk/A/sector/0/0/1/raw" \
  | jq '{raw_offset, raw_size, has_data, raw_base64}' 

# Whole-track raw dump
curl -s "$BASE/emulator/$EMU_ID/disk/A/track/0/0/raw" | jq '{track_size, base64}' 
```

Compare against the catalog: does the file at `first_track/first_sector`
actually start there? Do sectors have unusual IDs (sector numbers > 10,
interleave games) or data sizes ≠ 256? The `raw` view preserves those;
the decoded view normalizes them.

MCP: same endpoints through `invoke_api` — e.g. path
`/api/v1/emulator/{id}/disk/A/sector/0/0/1/raw`.

### Step 3 — Watch the controller while it loads

The FDC (WD1793) state is inspectable live:

```bash
curl -s "$BASE/emulator/$EMU_ID/state/fdc" | jq .
```

Interesting fields: command register, status (BUSY/DRQ/INTRQ/CRC_ERROR,
RECORD_NOT_FOUND), current track/sector, head load state, IRQ line. Poll it
during a hang — a loader stuck polling DRQ or retrying RECORD_NOT_FOUND on
the same sector is the protection check failing.

MCP: `inspect_state` `{"aspects":["fdc"]}` decodes registers, status, the
command FSM and drives in one call.

### Step 4 — Correlate with a port trace

Arm the trace before the load attempt:

```bash
curl -s -X PUT  "$BASE/emulator/$EMU_ID/feature/porttrace" \
     -H 'Content-Type: application/json' -d '{"enabled":true}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/config" \
     -H 'Content-Type: application/json' \
     -d '{"capacity":2000000,"overflow":"stop"}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/filter" \
     -H 'Content-Type: application/json' -d '{"preset":"fdc-only"}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/start" >/dev/null

# attempt the boot / load, run until it hangs (+ some slack)
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
     -H 'Content-Type: application/json' -d '{"frames":600}' >/dev/null

curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/stop" >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/save" \
     -H 'Content-Type: application/json' \
     -d '{"path":"scratch/protection-fdc.binz","format":"binz"}' | jq '{saved}'
```

Then interrogate the trace:

```bash
# last 40 FDC interactions with the PC that made them
curl -s "$BASE/emulator/$EMU_ID/profiler/porttrace/events?limit=40" \
  | jq -r '.events[] | "frame=\(.frame) ts=\(.ts) pc=\(.pc) port=\(.dec) val=\(.val)"'
```

What the pattern tells you:

| Trace shape | Meaning |
|:--|:--|
| tight `IN status` loop from one PC | loader polling, waiting for a condition that never comes → find why (CRC mismatch, missing weak sector) |
| reads of track/sector outside catalog bounds | hidden data area — dump those sectors, they often hold the real code |
| alternating WRITEs to the track register before reads | non-linear read order = interleave/layout check |
| access bursts to `1FFD/3FFD` (paging) between FDC ops | code being banked in/out around the check |

### Step 5 — Prove the hypothesis with TTD

Once a suspect PC (the check loop) is known from the trace:

```bash
# (record the attempt first — see analysis/ttd-recording.md)
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" \
     -H 'Content-Type: application/json' \
     -d '{"addr": <port>, "access": "io"}' | jq .       # last OUT to the FDC port
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' -d '{"pcs":[<check-loop-pc>]}' | jq .
```

That reconstructs *why* the loop was entered — usually a status bit set
differently than on real hardware.

## Mitigations (once understood)

- Patch the check in memory (`POST /memory/write`, MCP `debug_code`
  `assemble`) and re-run — verify with the same port trace (loop gone?)
- Provide the missing raw layout by writing a corrected image from the
  sector data you dumped
- File a core bug if the FDC behavior diverges from the documented WD1793
  flow (`docs/WD1793/` has the reference material)

## Pitfalls

- **`overflow: ring` eats the beginning** of a protection sequence — prefer
  `stop` for capture-the-hang workflows (you want the *first* accesses).
- **Beta128 gating** can hide FDC traffic while TR-DOS ROM is paged out;
  `total_produced` vs `events` tells you if filtering happened.
- **Raw base64 blobs are large** — pull them once, cache under `scratch/`,
  never in a loop.
- **Write-protection matters**: some protections *write* markers to disk on
  first boot; work on a copy unless you want that behavior.
