# Recipe: Detecting and Tracing a Non-Standard Disk Loader

A *non-standard* loader bypasses the TR-DOS service API (`LD C,service / CALL
$3D13`) and drives the WD1793 ports directly — `#1F` command/status, `#3F`
track, `#5F` sector, `#7F` data, `#FF` ROM page/control. Copy protections,
turbo loaders and non-standard DOS variants all live here. This recipe: prove
the software is non-standard, then trace it when it fails, hangs or crashes.

For protection-shaped hangs, continue with
[disk-protection-triage.md](../articles/disk-protection-triage.md); for the
physical-layout side, see
[physical-protection-forensics.md](../articles/physical-protection-forensics.md).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred —
> `analyze_performance` traces ports, `inspect_state` decodes the FDC,
> `invoke_api` the rest. Use [WebAPI](#webapi) only inside host-side
> Python/bash pipelines (the diskinfo pre-scan) or when MCP is unavailable
> (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
# signal 1 — FDC traffic with PC attribution (session form, fdc-only filter):
invoke_api         {"method":"PUT", "path":"/api/v1/emulator/{id}/feature/porttrace","body":{"enabled":true}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/filter","body":{"preset":"fdc-only"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/start"}
control_execution  {"action":"run_frames","frames":600}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/profiler/porttrace/stop"}
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/profiler/porttrace/events","query_params":{"limit":100}}
#   quick variant, no session: analyze_performance {"action":"porttrace","frames":600,"limit":100}

# signal 2 — fastdisk litmus:
invoke_api         {"method":"PUT", "path":"/api/v1/emulator/{id}/feature/fastdisk","body":{"enabled":true}}

# hang triage:
inspect_state      {"aspects":["fdc"]}                     # BUSY/DRQ/INTRQ, retries — decoded

# crash post-mortem:
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/start","body":{}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/stop"}
inspect_state      {"aspects":["registers","screen_ocr"]}  # death state + error message on screen
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/find-last","body":{"addr":24576,"access":"execute"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/ttd/reverse-continue","body":{"pcs":[24576]}}

# dumping the loader:
invoke_api         {"method":"GET", "path":"/api/v1/emulator/{id}/memory/read/0x6000",
                   "query_params":{"length":2048,"format":"full"}}
invoke_api         {"method":"POST","path":"/api/v1/emulator/{id}/labels",
                   "body":{"name":"LOADER","address":24576,"type":"code"}}
debug_code         {"action":"disassemble","address":"0x6000","count":40}   # = /disasm, labels apply
```

## WebAPI

The curl walkthrough below — right choice when the host-side diskinfo
pre-scan and jq pipelines drive the investigation.

### Detection signal 1 — port trace with PC attribution

The decisive test: who talks to the FDC, and from where?

```bash
curl -s -X PUT  "$BASE/emulator/$EMU_ID/feature/porttrace" \
     -H 'Content-Type: application/json' -d '{"enabled":true}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/filter" \
     -H 'Content-Type: application/json' -d '{"preset":"fdc-only"}' >/dev/null
curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/start" >/dev/null

# run the load attempt
curl -s -X POST "$BASE/emulator/$EMU_ID/run_frames" \
     -H 'Content-Type: application/json' -d '{"frames":600}' >/dev/null

curl -s -X POST "$BASE/emulator/$EMU_ID/profiler/porttrace/stop" >/dev/null
curl -s "$BASE/emulator/$EMU_ID/profiler/porttrace/events?limit=100" \
  | jq -r '.events[] | "pc=\(.pc) port=\(.dec) val=\(.val)"'
```

Interpretation — TR-DOS ROM occupies `#0000-#3FFF` while paged, so:

| Pattern | Verdict |
|:--|:--|
| FDC writes only from PC < `#4000` | standard DOS ROM code — loader is *not* custom (look elsewhere) |
| FDC writes from PC >= `#4000` (RAM) | **custom loader confirmed** — record those PCs, they are the loader |
| no FDC traffic while data appears in memory | not disk-loaded at all (embedded/tape hybrid) — recheck media |

Keep the PC list: it is the loader's code range for later
[disassembly](#dumping-the-loader-code).

### Detection signal 2 — the fastdisk litmus test

The `fastdisk` feature serves `$3D13` loads host-side (near-instant). It
declines — inertly, at authentic FDC speed — for direct-port software and for
non-5.03/5.04T ROMs:

```bash
curl -s -X PUT "$BASE/emulator/$EMU_ID/feature/fastdisk" \
     -H 'Content-Type: application/json' -d '{"enabled":true}' | jq .
# time the same load with it disabled and enabled:
#   still slow with fastdisk on  →  custom loader (or non-standard TR-DOS ROM;
#   distinguish via disk sysinfo / ROM signature at $3D13: 00 C3 69 2F = 5.03/5.04T)
```

Caveat: TTD recording **auto-disables** fastdisk (and restores it on stop) —
do the litmus before arming TTD.

### Detection signal 3 — structural pre-scan

Run the host-side forensic scan before booting at all:

```bash
python3 tools/diskinfo/diskinfo.py scratch/game.trd --tracks
```

`id-mismatch` notes, sector numbers `R >= #C0`, duplicate IDs or deliberate
CRC errors in the `protection analysis` block predict a loader that does more
than CAT+LOAD — see
[physical-protection-forensics.md](../articles/physical-protection-forensics.md).

### Tracing a hang

1. Poll `GET /state/fdc` — BUSY/DRQ/INTRQ stuck, or RECORD_NOT_FOUND retries,
   tell you *what* the loader is waiting for.
2. Re-read the port trace: the tight `IN status` loop PC is the wait point;
   sectors actually requested vs the catalog show what it expected to find.
3. Full worked flow: [disk-protection-triage.md](../articles/disk-protection-triage.md)
   steps 3-4.

### Tracing a crash

```bash
# 1. record from a clean reset
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/start" >/dev/null
#    ...run until it crashes...
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/stop" >/dev/null

# 2. capture the death state
curl -s "$BASE/emulator/$EMU_ID/state/registers" | jq '{pc, sp, af, hl}'
curl -s "$BASE/emulator/$EMU_ID/capture/ocr" | jq -r .text   # error message?

# 3. when did we last execute the crash PC? walk back from there
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/find-last" \
     -H 'Content-Type: application/json' \
     -d '{"addr": <crash_pc>, "access": "execute"}' | jq .
curl -s -X POST "$BASE/emulator/$EMU_ID/ttd/reverse-continue" \
     -H 'Content-Type: application/json' -d '{"pcs":[<loader_pcs>]}' | jq .
```

Details on find-last / reverse-continue semantics:
[ttd-reverse-debugging.md](ttd-reverse-debugging.md). A disk crash is usually
"status bit differed from expectation → wrong branch": find-last on the FDC
port with `"access": "io"` reconstructs the last real controller answer.

### Dumping the loader code

With the PC range from the trace:

```bash
curl -s "$BASE/emulator/$EMU_ID/memory/read/0x6000?length=2048&format=full" \
  | jq '.data[:8]'                      # hexdump default; format=full|sparse also exist
# label it, then disassemble:
curl -s -X POST "$BASE/emulator/$EMU_ID/labels" -H 'Content-Type: application/json' \
     -d '{"name":"LOADER","address":24576,"type":"code"}' >/dev/null
curl -s "$BASE/emulator/$EMU_ID/disasm?address=24576&count=40" | jq '.instructions[:5]'
```

The TR-DOS analyzer (`GET /analyzers`, name `trdos`) tracks loader states
including custom loaders — useful as a second opinion on what phase the
software died in.

## Pitfalls

- **PC < `#4000` heuristic**: exotic setups can page RAM low; confirm the ROM
  is actually TR-DOS (`sysinfo` responds, catalog decodes) before trusting the
  verdict.
- **Beta128 gating**: FDC ports answer only when the TR-DOS ROM is paged in on
  some machines — traffic can *pause* mid-trace; `total_produced` vs returned
  events count reveals filtering, not absence.
- **fastdisk + TTD**: recording disables the trap (deterministic replay) —
  never litmus-test while recording.
- **Trace capacity**: a full loader run is thousands of FDC events; prefer
  `overflow:"stop"` plus a tight `fdc-only` filter over a ring buffer.
