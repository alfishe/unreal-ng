# Current State — Automation Surface & Machine/Peripheral Status

**Verified:** master working tree, 2026-09-14.

> **Status update 2026-09-14 (end of day):** this file is the pre-fix
> snapshot. Landed since: machine identity + strict create (P0-1/P0-2,
> `34546478`), AGENTS.md model list (P0-3, `34546478`), build fingerprint
> (P0-4, `cab13b99`), digest `mode=active` (P1-3), `GET /ports` + mouse
> routing (P1-5), `mouse` aspect (P2-1) — the affected claims are marked
> ✅ DONE inline below; everything else still describes the current state.
>
> **Status update 2026-09-15 (parity round):** the P1-3/P1-5/P2-1 surfaces
> were replicated to the CLI, Lua and Python bindings (`digest --active` /
> `screen_digest(..., "active")`, `ports` / `ports_map()`, `mouse status`
> `Routing:` line / `mouse_status().routing`), documented in
> `control-interfaces/`. Additionally the `GetVideoModeName` 128k/Pentagon/
> Scorpion `"Unknown"` bug was fixed (affects identity `video_mode`,
> `screen_get_mode`, `/state/screen`, digest `active_surface`).

## 1. WebAPI endpoint inventory

Source of truth: [emulator_api.h](../../../core/automation/webapi/src/emulator_api.h)
(route registration, regions per group) and `core/automation/webapi/src/api/*.cpp`
(implementations). The MCP router describes the surface as **28 endpoint
groups, 212 paths** (`docs/features/mcp/README.md`).

| Group | Representative routes | Count | Notes |
|:--|:--|:--:|:--|
| Root/OpenAPI | `/`, `/api/v1/openapi.json` | 2 | Swagger UI served from `/` |
| Lifecycle | `/emulator`, `/status`, `/models`, `/create`, `/start`, `/{id}` GET/DELETE, `/{id}/start|stop|pause|resume|reset|nmi`, `/{id}/model` (switch) | 15 | `nmi` supports Scorpion MNI `{"magic":true}` |
| Tape | `/tape/load|eject|play|pause|stop|rewind|seek`, `/tape`, `/tape/info`, `/tape/blocks/{index}`, `/tape/render`, `/tape/import` | 12 | audio bridge render/import are pure file conversions |
| Disk | insert/create/eject/info + drive listing + sector/track (raw) + image/sysinfo/catalog | 14 | TR-DOS centric |
| Snapshot | load/save/info | 3 | `.sna/.z80` |
| Capture | `/capture/ocr`, `/capture/screen` | 2 | screen capture now reads raster dims (partially fixed bug #1) |
| BASIC | run/inject/extract/clear/state/mode | 6 | |
| Settings | `/settings` GET, `/settings/{name}` GET/PUT | 3 | exposes only io_acceleration + disk_interface |
| Features | `/features`, `/feature/{name}` GET/PUT | 3 | runtime features from `features.ini` |
| Analyzers | get/set/events/session/raw + coverage start/stop/clear/get/gaps + ay/log + audio capture (3) | 21 | |
| Video recording | `/video/record`, `/video/record/status` | 2 | GIF native, quantum sampling |
| Memory state | `/state/memory`, `/state/memory/ram`, `/state/memory/rom`, read/write/find, page read/write, ROM protect | 10 | 7FFD-centric (see gap E-1) |
| Screen state | `/state/screen`, `/state/screen/mode`, `/state/screen/flash`, `/state/screen/digest`, `/video/beam` | 5 | mode endpoints hardcoded "standard" (gap B-1) |
| Audio state | AY (3) + beeper/gs/covox/channels (4) ×2 (id + active), FM (2), FDC (1) | 17 | gs/covox are `not_implemented` placeholders |
| Debug | stepping (12), debugmode (2), breakpoints (7), registers/memory (8), memcounters/calltrace (2), disasm (2) | 33 | |
| Profilers | opcode (7), memory (6), calltrace (6), porttrace (12), unified (6), frame_cost (1) | 38 | porttrace = the port triage workhorse |
| Keyboard | tap/press/release/combo/macro/type/release_all/abort/status/keys | 10 | |
| Mouse | move/press/release/click/buttons(P)/wheel/release_all/counters/status/buttons(G) | 10 | implemented 2026-09-12 |
| TTD | status/start/stop/invalidate/seek/step-back/step-forward/resume/position/markers/dump/load/find-last/step-instruction/reverse-step/reverse-continue | 16 | |
| Labels/symbols | labels CRUD + resolve, symbols load/save | 10 | |
| Source listing | listing/load, source_at, step_line, run_to_line | 4 | sjasmplus `.lst` |
| Assembler | `/assemble` | 1 | two-pass, labels |
| Python/Lua | exec/file/status/stop ×2 | 8 | |
| Videowall | `/videowall/singlesync` | 1 | |

## 2. MCP server surface

Source: [mcp-tools.cpp](../../../core/automation/mcp/src/mcp-tools.cpp),
`docs/features/mcp/README.md`.

- **Protocol:** MCP `2025-03-26`, stateless Streamable HTTP at
  `http://localhost:8092/mcp` (+ stdio via `unreal-mcp-bridge`).
- **Architecture:** all tools fan out to loopback WebAPI (`:8090`) — the WebAPI
  is the single source of truth; MCP fixes ride WebAPI fixes for free.
- **Registry composition** (`BuildFullRegistry`, mcp-tools.cpp:1323-1345):
  - Phase 1: `emulator_manage`, `load_software`, `control_execution`,
    `inspect_state`, `type_input`, `mouse_input`
  - Phase 2: `manage_symbols`, `debug_code`, `analyze_performance`,
    `capture_media`
  - Router: `search_api`, `invoke_api` (schema-driven, cover the whole WebAPI)
- **`inspect_state` aspects** (mcp-tools.cpp:571-572): `machine, registers,
  memory, disasm, stack, breakpoints, memory_banks, screen_ocr, screen_image,
  screen_digest, timing, rom, audio_ay, audio_fm, fdc`.
  No `mouse`, no `audio_gs`/`covox`, no `paging`/`ports` aspects.
  ✅ DONE (2026-09-14): `mouse` aspect added (P2-1); `paging`/`ports` aspects
  remain open (the `/ports` endpoint exists via P1-5, the `paging` endpoint
  does not — P1-2).
- **Resources** (6): `unreal://keyboard-layout`, `basic-reference`, `z80-isa`,
  `trdos-commands`, `memory-map` (48K/128K only), `emulator-state` (dynamic).
  No per-machine resources.

## 3. Runtime features (`features.ini`)

`kempstonmouse` (on), `hud`, `turbotape` (on), `fasttape` (on), `overscan`,
`porttrace`, `calltrace`, `opcodeprofiler`, `recording`, `timetravel` (on),
`screenhq`, `soundhq`, `sound` (on), `sharedmemory`, `debugmode` (on),
`breakpoints`, `memorytracking`.

Feature get/set is exposed via `/features` (group) and `/feature/{name}`
(individual) — this is the correct control-plane pattern new observability
features should follow.

## 4. Machine model table

Source: [config.h:45-63](../../../core/src/emulator/config.h) (`mem_model[]`).
16 entries; `GET /emulator/models` returns all of them.

| ShortName | FullName | Default RAM | Available RAMs |
|:--|:--|:--:|:--|
| PENTAGON | Pentagon | 128 | 128, 512, 1024 |
| 48K | ZX-Spectrum 48k | 48 | 48 |
| 128k | ZX-Spectrum 128k | 128 | 128 |
| PLUS3 | ZX-Spectrum +3 | 128 | 128 |
| TSL | TS-Config | 4096 | 4096 |
| ATM3 | **ZX-Evo** | 4096 | 4096 |
| ATM710 | ATM-Turbo 2+ v7.10 | 1024 | 128, 256, 512, 1024 |
| ATM450 | ATM-Turbo v4.50 | 512 | 512, 1024 |
| PROFI | Profi | 1024 | 1024 |
| SCORPION | ZS Scorpion | 256 | 256, 1024 |
| PROFSCORP | ZS Scorpion + PROF ROM | 256 | 256, 1024 |
| GMX | ZS Scorpion + GMX | 2048 | 2048 |
| KAY | Nemo's KAY | 256 | 256, 1024 |
| QUORUM | Quorum | 1024 | 128, 1024 |
| LSY256 | Orel' BK-08 (LSY) | 256 | 256 |
| PHOENIX | ZXM-Phoenix v1.0 | 1024 | 1024, 2048 |

Note the naming: **"ZX-Evo" is the FullName of short name `ATM3`**; ZX Evolution
TS-Conf mode is the separate `TSL` entry. `MM_NEXT` exists in `MEM_MODEL`
(platform.h:322) but has **no table entry** — it is not creatable.

### Config folder resolution (config.cpp:551-568)

`pentagon128k | pentagon512k | spectrum48 | spectrum128 | spectrum3 | ts-conf`
have dedicated folders in `data/configs/`; every other model derives
`<lowercase-shortname>` (e.g. `atm710`, `profi`) — folders that **do not exist
on master** (`data/configs/` contains only the six above plus `profscorp`,
`scorpion`, `zx-diagnostics`). The `atm` branch adds `data/configs/atm3/` and
`data/configs/atm710/unreal.ini`.

### Port decoder coverage (portdecoder.cpp:50-100)

`GetPortDecoderForModel` constructs decoders for: SPECTRUM48, PENTAGON
(128/512 by RAM), SPECTRUM128, PLUS3, PROFI, SCORP/PROFSCORP. **Everything
else — MM_TSL, MM_ATM3, MM_ATM710, MM_ATM450, MM_GMX, MM_KAY, MM_QUORUM,
MM_LSY256, MM_PHOENIX — throws `std::logic_error`.** On master, creating any
of these models fails during `Core::Core` (core.cpp:365).

### Porttrace decode rules

`getPortTraceDecodeRules()` is virtual with an empty default
(portdecoder.h:233); **only `PortDecoder_Pentagon128` implements it**. Session
info maps 7 models to names, everything else reports `"Unknown"`
(portdecoder.cpp:423-433). Trace event flags exist and are good:
`kWasDecoded, kHadHandler, kBeta128Gated, kHandledInline, kCfTrdosActive,
kViaLegacyBasePath`.

## 5. Video mode machinery (core) vs rendering

`VideoModeEnum` (screen.h:36-62) covers ZX48/ZX128/Pentagon variants plus
`M_TIMEX, M_TS16, M_TS256, M_TSTX, M_ATM16, M_ATMHR, M_ATMTX, M_ATMTL,
M_PROFI, M_GMX, M_SCORPION, M_BRD`.

Mode *selection* is implemented for ATM (screen.cpp:178-226 reads `aFE`
(ATM450) / `pFF77` bits with `FF77_16/MC/ZX/TX/TL` selectors) and there are
raster descriptors + mode names (screen.cpp:1329-1345).

Mode *rendering* on master is stubbed for every extended mode —
`DrawPMC/P16/P384/PHR/Timex/TS16/TS256/TSText/ATM16/ATMHiRes/ATM2Text/
ATM3Text/Profi/GMX` are all `(void)n;` no-ops (screen.cpp:1231-1298). Only
`DrawZX` and border drawing are real. The `atm` branch carries the ATM
rendering work (e.g. `atm_video_modes_suite_test.cpp`, 980 lines).

## 6. Peripheral implementation status

| Peripheral | Core status | Automation status |
|:--|:--|:--|
| Kempston Mouse | Implemented (funnel `DebugMouseManager`, atomic counters, TTD journal, replay guard). Per the 2026-09-12 design §0.3: every model decoder decodes the mouse ports, gated by `!CF_TRDOS`; `[INPUT] Mouse=/Wheel=` config parsed; feature `kempstonmouse` exists | Full: WebAPI `/mouse/*` (10 routes), MCP `mouse_input` (8 actions incl. `status`), CLI/Python/Lua. **Missing:** routing report (`ports_decoded`, open Q4), `mouse` aspect in `inspect_state`, any API to read/change mouse fitment. ✅ DONE (2026-09-14): routing report (`routing.ports_decoded` + note, from `PortDecoder::GetMouseRoutingState` — design Q4 closed) and the `mouse` aspect (P2-1); mouse fitment introspection remains open (A-4 / P2-3). ✅ Parity (2026-09-15): `mouse status` `Routing:` line (CLI), `mouse_status().routing` (Lua/Python) |
| TurboSound FM / YM2203 | Implemented | Full `DeviceState` reports: `/state/audio/fm`, `/state/audio/fm/{chip}`, MCP `audio_fm` aspect — the **extensibility template** |
| Beta Disk (WD1793) | Implemented | Full `DeviceState` report `/state/fdc`, MCP `fdc` aspect; analyzer raw endpoints |
| AY/SSG (incl. TurboSound pairs) | Implemented | `/state/audio/ay[/{chip}[/register]]`, MCP `audio_ay`; `/ay/log` write log |
| Beeper | Implemented | `/state/audio/beeper` |
| General Sound | Not implemented in core | Endpoint placeholder returns `not_implemented` |
| Covox | Core present (config, mixing) | Endpoint placeholder returns `not_implemented` |
| **MoonSound (OPL4)** | **Design only** — `docs/inprogress/2026-09-13-moonsound/` (3 docs: chip TDD, integration, TTD). Config keys `[SOUND] MoonSound/MoonSoundVol` + `MOONSOUND` ROM path already shipped in `data/configs/*/unreal.ini` but unparsed; `AudioSourceType::Moonsound` placeholder in recording | **None.** The integration TDD has no automation/observability section (grep for webapi/mcp/automation in `opl4-unreal-ng-integration.md` matches nothing relevant) |

## 7. Branch topology (as of 2026-09-14)

- `master` — all automation work; ATM/TSConf/Profi machines non-creatable
  (decoder throw + missing config folders).
- `atm` — ATM/ZX-Evo bring-up: `portdecoder_atm3.{cpp,h}`,
  `portdecoder_atm710.{cpp,h}` (+ tests: 306/544 lines),
  `data/configs/atm3/unreal.ini` (218 lines), `data/configs/atm710/unreal.ini`
  (216 lines), `core/src/emulator/memory/atm/` CMOS/RTC isolation,
  `zxevo_boot_test.cpp` (210 lines), ATM video mode suites (980+617 lines),
  TTD paging capture. Diff vs master: 75 files, +7,584/−25,931.
  **Zero changes under `core/automation/`** — machine bring-up is not extending
  the triage surface in parallel.
- Related merged work streams: `mcp-server`, `tsfm` (TurboSound FM),
  `scorpion-zs256`.

## 8. Prior gap analysis (2026-08-26)

`docs/inprogress/2026-08-26-automation-gaps/gap-analysis.md` ranked 10 gaps
(symbols, register writes, events, GDB/RSP, conditional breakpoints,
expressions, framebuffer access, transport parity, schema introspection,
determinism). Most were since implemented (labels API, `/registers/{name}`
PUT, TTD, porttrace, MCP). That analysis did **not** cover
machine-configuration/peripheral triage — this report complements it.
