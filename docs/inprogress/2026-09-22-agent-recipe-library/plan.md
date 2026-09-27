# Plan — AI Agent Recipe Library (`.recipe/`)

**Created:** 2026-09-22
**Status:** content complete — 31 files / 4,196 lines, verified against source;
every recipe carries MCP (preferred) + WebAPI sections; 2026-09-23 growth pass
added per-model and per-peripheral coverage; awaiting review + commit ([TODO.md](TODO.md))
**Artifact:** [`.recipe/README.md`](../../../.recipe/README.md) at repository root — this plan ships as living documentation, not a spec awaiting implementation

## Motivation

Agent-facing operational knowledge (how to insert media, boot a disk, arm a
profiler, record and reverse time) was spread across the WebAPI reference, MCP
docs, dated design folders and Python tools. Every agent session re-derived
the same invocation rituals — and occasionally invented endpoints. A recipe
library gives agents (and humans) copy-paste workflows whose accuracy is
pinned to source code, with one canonical entry point.

It also delivers the recipes half of **P3-3 "triage recipes / canonical RE
recipe"** (F-2 in [2026-09-14-automation-triage-gaps](../2026-09-14-automation-triage-gaps/),
PLAN.md T3 #15): `articles/bug-hunt-ttd.md` is the record-then-reverse
workflow that item called for.

## Goals

- Copy-paste runnable recipes for the recurring automation jobs: media
  insertion (disk/tape/snapshot), disk boot (autostart + manual TR-DOS),
  instrumentation (TTD, port trace, memory counters) with save/load/analyze.
- Both transports in every recipe, structured MCP-first: a `## MCP (preferred)`
  section (smart tools, `invoke_api` for the rest) followed by a `## WebAPI`
  curl walkthrough, plus a header note stating when WebAPI applies
  (host-side Python/bash pipelines, MCP faults) — policy in
  `_common/transports.md`. Honors the parity rule: same information from
  the same source.
- Accuracy pinned to source, not prose: every endpoint/body/response shape
  checked against the route registry and handler bodies.
- Worked articles that compose recipes into full investigations.

## Non-goals

- Not an API reference — that is
  `docs/emulator/design/control-interfaces/` and `GET /api/v1/openapi.json`.
- Not UI-oriented; GUI tools appear only as pointers (port-trace GUI).
- No new core/WebAPI/MCP code — documentation only.

## Library structure (as built)

```
.recipe/
├── README.md              index, conventions, rules of engagement
├── _common/
│   ├── setup.md           build/launch ritual, ports, models, lifecycle, verification primitives
│   ├── transports.md      WebAPI vs MCP, 13-tool catalog, search_api → invoke_api escape hatch
│   └── machines.md         model table (RAM/creatability), create/identity patterns, branch matrix
├── media/
│   ├── insert-disk.md     insert (path/multipart/MCP), catalog, blank disk, eject
│   ├── insert-tape.md     load, fast-load plan, seek by block, WAV import
│   ├── load-snapshot.md   load/save, round-trip regression, TTD interaction
│   ├── author-udi-images.md UDI authoring: capability matrix, host-side paths, in-emulator formatting, weak-bit limits
│   └── agent-screenshot-view.md server-side PNG save for agent viewing (no base64 through the transcript)
├── run/
│   ├── autostart-disk.md  one-call boot, autostart decision table
│   ├── manual-trdos-run.md catalog → RUN "NAME" (15619:REM: trick), .C entry points
│   └── tape-fastload.md   LOAD "", bounded polling, multi-load seek
├── analysis/
│   ├── ttd-recording.md       start modes, status, seek/bookmarks, dump/load .ttd
│   ├── ttd-reverse-debugging.md find-last, reverse-step/continue, coverage queries
│   ├── port-trace.md          feature gate, filters/presets, save formats, Python tools
│   ├── memory-counters.md     pages/counters drill-down, save yaml, coverage complement
│   ├── nonstandard-loader.md  detect custom loaders (port-PC, fastdisk litmus), trace hang/crash
│   └── ttd-visual-inspection.md  record once, then seek/step to any frame for stable inspection
├── machines/
│   ├── pentagon.md        128/512/1024 via ram_size, #EFF7 (GigaScreen/512x192/a4b), sound defaults
│   ├── scorpion.md        SCORPION/PROFSCORP, Shadow Monitor #1FFD, ProfROM #7EFD, built-in Beta128
│   ├── profi.md           branch-aware creatability, #7FFD+#DFFD paging, CMOS, Covox arbitration
│   ├── atm.md             ATM710/ATM3: #FF77/#FFF7/#EFF7, CP/M bit, shaden CMOS ports, turbo
│   └── spectrum.md        48K/128k/PLUS3 boundary: AY/FDC per model, clone-vs-Sinclair diffs
├── peripherals/
│   ├── generalsound.md    BASS HLE vs Z80 LLE (branch), #B3/#BB/#33 mailbox, GS state/trace/reset
│   ├── moonsound.md       OPL4 (branch-only), #C4-#C7 FM + #7E/#7F wave, YRW801, clone-only policy
│   ├── turbosound.md      slot AY pair vs TSFM (YM2203), /state/audio/{ay,fm}, decode math
│   └── covox-sounddrive.md CovoxFB/DD + SoundDrive quad DAC, mono compat, capture+trace proof
└── articles/
    ├── bug-hunt-ttd.md          4-phase crash hunt (record → ask → reverse → replay)
    ├── demo-boot-verification.md golden-digest regression gates, sweeps, model comparison
    ├── disk-protection-triage.md catalog vs raw sectors vs FDC state vs port trace
    └── physical-protection-forensics.md diskinfo scan, clock bitmap, flaky differential, RE the check
```

## Authoring conventions (defined in `.recipe/README.md`)

- Shell variables `BASE` (WebAPI root) and `EMU_ID` assumed set —
  established once in `_common/setup.md`, never re-explained.
- bash + `jq` only; every artifact written under `scratch/` per project rules.
- Fixed recipe shape: goal → header note (which section to use) →
  `## MCP (preferred)` → `## WebAPI` walkthrough → pitfalls.
- Cross-link between recipes; never duplicate API-reference material.
- kebab-case filenames, matching project documentation rules.

## Coverage of the originating request

| Requested | Delivered |
|:--|:--|
| insert tape / disk / snapshot via WebAPI and MCP | `media/insert-disk.md`, `media/insert-tape.md`, `media/load-snapshot.md` |
| autostart for disk | `run/autostart-disk.md` (drive-A rule, decision table, confirmation via OCR/digest) |
| manual TR-DOS selection + run `<name>.B` | `run/manual-trdos-run.md` (catalog → name cleanup → `USR 15619: REM: RUN "NAME"`) |
| TTD recording on/off, save/load/analyze | `analysis/ttd-recording.md` + `analysis/ttd-reverse-debugging.md` |
| port trace on/off, save/load/analyze | `analysis/port-trace.md` |
| memory counters on/off, save/load/analyze | `analysis/memory-counters.md` |

## Extra use cases added beyond the request

- Transport guide: when to use WebAPI vs MCP; unknown-endpoint discovery via
  `search_api` → `invoke_api`.
- Tape fastload (LOAD "" + bounded polling), multi-load `seek`, WAV import.
- Snapshot round-trip regression pattern (save → load → digest compare).
- Deterministic demo boot verification: golden digests, library sweeps,
  model comparison loops.
- Disk protection triage: catalog anomalies → raw sector/track dumps → live
  FDC state → correlated port trace → TTD hypothesis proof.
- Non-standard loader detection and tracing (port-PC attribution, fastdisk
  litmus, structural pre-scan; hang and crash flows).
- Physical copy-protection forensics: host-side `diskinfo` structural scan,
  clock-bitmap inspection, flaky-sector differential test (HFE live vs UDI
  weak-dropped), reversing the check with TTD.
- UDI image authoring: format capability matrix, host-side conversion and
  sector-level authoring (`tools/diskconverter`), in-emulator formatting
  through the real WD1793, real-hardware flux dumps (SCP/HFE).
- TTD bug-hunt worked example composing record/reverse/replay.
- Agent screenshot viewing: server-side binary save (MCP `capture_media`
  `filename`, WebAPI `path`), metadata-only mode, base64-decode fallback for
  older servers.
- TTD visual inspection: record once, seek to any frame, inspect
  registers/video/screenshot of that exact frame — stable by construction.
- Per-model recipes (2026-09-23): Pentagon variants + `#EFF7`, Scorpion/
  ProfROM, branch-aware Profi, ATM710/ATM3 port model, real-Sinclair
  boundary — each with `ram_size` semantics, `/ports` + `/state/paging`
  introspection and model-specific pitfalls.
- Per-peripheral recipes (2026-09-23): GeneralSound (BASS vs Z80 LLE),
  MoonSound (branch-only OPL4), TurboSound slot (2×AY vs TSFM), Covox/
  SoundDrive — each with port maps, config toggles, state endpoints vs
  stubs, and the audio-capture proof pattern (`dominant_hz`, per-channel
  peak/RMS).
- Shared machine reference `_common/machines.md`: model table with
  creatability, branch matrix (profi/generalsound/moonsound), and the
  create/identity/introspection patterns the per-model recipes assume.

## Accuracy verification (2026-09-22)

- Routes cross-checked against the `ADD_METHOD_TO` registry in
  `core/automation/webapi/src/emulator_api.h`.
- Request/response shapes verified in handler bodies
  (`core/automation/webapi/src/api/*.cpp`) and MCP schemas
  (`core/automation/mcp/src/mcp-tools.cpp`, `mcp-analysis.cpp`).
- Three shape defects caught and fixed during self-review:
  capture screen uses `.data` (base64) with gif default and
  `?format=png&mode=full`; an invalid Python-style jq slice replaced with
  valid jq; `/ttd/position` corrected to
  `{"current": {frame, tinframe}, "session_end": {…}, "state"}`.
- Cross-link check across all 19 files: zero broken relative links.
- 2026-09-23 growth pass (machines + peripherals): model table verified
  against `mem_model` (config.h) and `PortDecoder::IsModelSupported`;
  branch claims verified against `git diff master...{profi,generalsound,moonsound}`
  and branch chip headers (`soundchip_gs.h` ports/clocks, `soundchip_moonsound.h`
  port map, Pentagon-1024/ATM/Scorpion/Profi decoder headers); audio-capture
  result fields taken from `analyzers_api.cpp`; two branch-only source links
  de-linked to code spans so every relative link resolves on `master`.
  31 files / 4,196 lines, zero broken links after the pass.
- 2026-09-22 structural pass: every recipe (16 recipes + `_common/setup.md`)
  now carries the two sections; MCP call shapes re-verified against source
  (`time_travel` action list and params in `mcp-tools.cpp`, `debug_code`
  disassemble in `mcp-analysis.cpp`, `invoke_api` path form
  `/api/v1/...` with `{id}` substitution).

## Future work (opportunistic)

- Symbols/debugger recipes: `manage_symbols`, breakpoints, labeled disasm
  flows (the debugger surface is already MCP-first).
- Capture/recording recipes: screenshots are covered
  (`media/agent-screenshot-view.md`); AV recording surface is not.
- `machine_selftest` recipe once that lands (triage-gaps F-2 remainder).
- CI link check for `.recipe/` if the library grows past ~25 files.
