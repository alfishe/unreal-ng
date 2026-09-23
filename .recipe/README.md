# AI Agent Recipe Library

Battle-tested, copy-pasteable recipes for AI agents (and humans) driving the
Unreal-NG emulator through its two automation surfaces:

- **WebAPI** — REST over HTTP, `http://localhost:8090/api/v1/...`
- **MCP** — Model Context Protocol, `POST http://localhost:8092/mcp`
  (or the stdio bridge `cmake-build-release/bin/unreal-mcp-bridge`, already
  wired in the repo root `.mcp.json`)

Every recipe carries two sections — **MCP (preferred)** and **WebAPI** — plus
a short transport note at the top. **MCP is the preferred transport**: start
with the MCP section. Use the WebAPI section only when a step runs through
host-side Python tools (they speak plain HTTP) or when MCP is unavailable
(server/bridge fault). Recipes show the exact requests, the response fields
worth asserting on, and the pitfalls that actually bite (409s, feature gates,
drive restrictions, pause races).

## Conventions used by every recipe

- `EMU_ID` is the instance UUID returned by `POST /api/v1/emulator/start`.
  MCP recipes pass `target: "auto"` instead and let the server resolve it.
- `BASE` is `http://localhost:8090/api/v1` for WebAPI recipes.
- Shell examples are bash + `jq`; MCP examples are raw JSON-RPC 2.0 bodies for
  `curl -X POST http://localhost:8092/mcp`.
- **MCP notation:** recipes write MCP calls compactly as `tool {"args"}` —
  expand into the standard `tools/call` envelope shown in
  [_common/transports.md](_common/transports.md).
- Every file path passed to the emulator must be **readable by the emulator
  process** (it loads it server-side). The MCP bridge can additionally upload
  a file that exists on the *agent's* machine (embedded content).
- Test artifacts (`.trd`, `.tap`, `.sna`, traces, `.ttd` dumps) belong in
  `scratch/` — never in the project root.

## Library index

### `_common/` — read these first

| Recipe | What it covers |
|:--|:--|
| [_common/setup.md](_common/setup.md) | Build, launch, kill stale instances, port discipline, model choice, instance lifecycle |
| [_common/transports.md](_common/transports.md) | WebAPI vs MCP: when to use which, request idioms, `search_api`/`invoke_api` escape hatch, parity rule |

### `media/` — getting software into the machine

| Recipe | What it covers |
|:--|:--|
| [media/insert-disk.md](media/insert-disk.md) | Insert/eject disk images (`.trd .scl .fdi .udi .dsk .td0 .mgt .img`), drive A/B, blank disks, catalog/sysinfo inspection |
| [media/insert-tape.md](media/insert-tape.md) | Load/eject tapes (`.tap/.tzx`), play/pause/seek/rewind, block catalog, fast-load plan, WAV import |
| [media/load-snapshot.md](media/load-snapshot.md) | Load/save snapshots (`.sna/.z80`), verify a state took effect, snapshot round-trips |
| [media/author-udi-images.md](media/author-udi-images.md) | Creating proper UDI images: format capability matrix, host-side conversion/authoring, in-emulator formatting, weak-bit limits |

### `run/` — making software actually run

| Recipe | What it covers |
|:--|:--|
| [run/autostart-disk.md](run/autostart-disk.md) | One-call disk boot (`autostart: true`), the boot decision table, what to assert |
| [run/manual-trdos-run.md](run/manual-trdos-run.md) | Manual path: insert without autostart, read the catalog, enter TR-DOS, `RUN "NAME.B"` for a chosen file |
| [run/tape-fastload.md](run/tape-fastload.md) | Tape loading: `LOAD ""` + play, block seeking, fast-load, detecting load completion |

### `analysis/` — instrumenting the machine

| Recipe | What it covers |
|:--|:--|
| [analysis/ttd-recording.md](analysis/ttd-recording.md) | TTD on/off, gaming vs development journal, dump/save `.ttd`, load back, seek/step, bookmarks, coverage heatmap |
| [analysis/ttd-reverse-debugging.md](analysis/ttd-reverse-debugging.md) | Reverse queries: `find-last`, `reverse-step`, `reverse-continue`, coverage probe/scan |
| [analysis/port-trace.md](analysis/port-trace.md) | Port I/O tracing: feature gate, filters/presets, ring buffer, save `json/csv/bin/binz`, re-read server-side |
| [analysis/memory-counters.md](analysis/memory-counters.md) | Memory access counters: profiler start/stop, per-page summaries, per-address counters, YAML export |
| [analysis/nonstandard-loader.md](analysis/nonstandard-loader.md) | Detect custom loaders (port-PC attribution, fastdisk litmus, structural pre-scan), trace hangs and crashes |

### `articles/` — full workflows that combine recipes

| Article | What it covers |
|:--|:--|
| [articles/bug-hunt-ttd.md](articles/bug-hunt-ttd.md) | "Who corrupted this memory?" — snapshot + TTD + `find-last` + `reverse-continue` |
| [articles/demo-boot-verification.md](articles/demo-boot-verification.md) | Deterministic boot verification: autostart + `run_frames` + screen digest + OCR |
| [articles/disk-protection-triage.md](articles/disk-protection-triage.md) | Disk protection triage: catalog vs raw sectors + FDC state + `fdc-only` port trace |
| [articles/physical-protection-forensics.md](articles/physical-protection-forensics.md) | Physical copy-protection forensics: diskinfo structural scan, clock bitmap, flaky-sector differential test, RE the check |

## Ground truth links

- WebAPI endpoint registry: `core/automation/webapi/src/emulator_api.h`
  (`ADD_METHOD_TO` list), interactive spec at `http://localhost:8090/api/v1/openapi.json`
- MCP tool catalog: [docs/features/mcp/README.md](../docs/features/mcp/README.md),
  implementation `core/automation/mcp/README.md`
- Port-trace design: `docs/inprogress/2026-08-24-diagnostic-observability/`
- TR-DOS autostart policy: `docs/inprogress/2026-09-18-trdos-autostart/design.md`
- Universal track model / UDI / flaky sectors: `docs/inprogress/2026-09-02-universal-track-model/`,
  `docs/WD1793/FlakySectorEmulator.md`; worked protection case study:
  `docs/disasm/black-raven-voron-protection/`
- Host-side disk forensics: `tools/diskinfo/diskinfo.py`, conversion/authoring:
  `tools/diskconverter/diskconvert.py`
- Ready-made Python capture tools: `tools/porttrace/porttrace_capture.py`,
  `tools/verification/debugger/capture_debug_session.py`

## Rules of engagement (the ones agents forget)

1. **One emulator can own port 8090.** Kill stale instances before starting a
   fresh one (see [setup](_common/setup.md)).
2. **Scrubbing during TTD recording is a 409** — always `POST /ttd/stop`
   before `seek`/`step-back`/`find-last`.
3. **Disk autostart is drive A only** — a hard error elsewhere, never silent.
4. **Port trace needs its runtime feature on** —
   `PUT /feature/porttrace {"enabled": true}` (or let the capture tools do it).
5. **`target: "auto"` in MCP** creates a 128K machine when none exists and
   refuses when several exist — pin the id for multi-instance work.
6. **Responses are the contract** — assert on `status`/`state` fields, never
   on prose. The parity rule guarantees MCP and WebAPI return identical data
   from the same handlers.
