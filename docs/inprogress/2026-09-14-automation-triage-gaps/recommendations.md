# Recommendations — Prioritized Remediation Plan

Priorities: **P0** = blocks correct triage today, small effort; **P1** = makes
the named scenarios (ATM ports/modes, mouse-on-configs) efficiently triageable;
**P2** = peripheral observability incl. MoonSound design work; **P3** = knowledge
layer. Each item lists: proposal, gap(s) closed (see
[gap-analysis.md](gap-analysis.md)), sketch, acceptance criteria.

Guiding principle: **extend the WebAPI first** — MCP tools fan out to loopback
WebAPI, so endpoint fixes propagate to MCP, CLI and the OpenAPI router
(`search_api`/`invoke_api`) without protocol work. New MCP aspects/resources are
separate small follow-ups.

---

## P0 — Identity & correctness (blocks everything)

### P0-1. Machine identity in `GET /emulator/{id}`

> ✅ **DONE (2026-09-14, commit `34546478`).** Implemented as proposed; the
> acceptance criteria (machine aspect shows model/RAM/video mode; unit tests
> for two models) are covered by the committed tests.
>
> ✅ **Follow-up (2026-09-15):** the identity's `video_mode` had been silently
> broken for 128k/Pentagon/Scorpion — `Screen::GetVideoModeName` missed the
> `M_ZX128`/`M_PENTAGON128K`/`M_SCORPION` cases and answered `"Unknown"`.
> Fixed at the single source with `Screen_VideoModeName_Test` regressions;
> live check now returns `video_mode: "ZX128"`.

Closes A-1.

Add to `getEmulator` (lifecycle_api.cpp:263-268) and to the list response of
`GET /emulator`:

```json
{
  "id": "…", "state": "…", "is_running": true, "is_paused": false, "is_debug": false,
  "model": "ATM710",
  "model_full_name": "ATM-Turbo 2+ v7.10",
  "ram_kb": 512,
  "video_mode": "ATM16",
  "speed_multiplier": 1,
  "config_folder": "atm710"
}
```

All fields already exist in `EmulatorContext` (`config.mem_model`,
`config.ramsize`, `Screen::GetVideoModeName()`, `config.speed_multiplier`;
folder via the config.cpp:551-568 mapping). The MCP `machine` aspect picks
this up verbatim (no mcp-tools.cpp change needed).

**Accept:** `inspect_state aspects=["machine"]` on an ATM710 instance shows
model `ATM710`, 512 KB, current video mode. Unit test asserts the fields for
two different models.

### P0-2. Fail loudly on model create/switch

> ✅ **DONE (2026-09-14, commit `34546478`).** Strict-by-default 400 with the
> manager's failure reason; success echoes the resolved model. The optional
> `allow_fallback` escape hatch was not carried along (default-strict covers
> the triage need).

Closes A-2 (the critical one).

- `POST /emulator/create` and `POST /emulator/{id}/model`: on
  `CreateEmulatorWithModel[AndRAM]` returning nullptr, respond **400** with
  the failure reason (unknown model / RAM not supported for model /
  initialization failed — the manager already logs exactly these three,
  emulatormanager.cpp:242-255) instead of falling back to 48K.
- On success, echo the resolved `model` + `ram_kb` in the 201 (as in P0-1).
- Optional escape hatch for backwards compatibility: `{"allow_fallback":
  true}` opt-in flag, response then carries `"fallback": true, "requested":
  "ATM710"`. Default remains strict.

**Accept:** on master, `POST /emulator/create {"model":"ATM710"}` returns 400
with "model cannot be initialized" (or the config-folder reason); on the atm
branch it returns 201 with `"model":"ATM710"`. A test documents both.

### P0-3. Fix the documented model list

> ✅ **DONE (2026-09-14, commit `34546478`).** `AGENTS.md` carries the 16
> authoritative short names + the runtime-authoritative note pointing at
> `GET /emulator/models`. The P3 CI-generation idea remains open.

Closes A-3. Trivial.

- Update `AGENTS.md` (and any other doc listing models) to the authoritative
  short names: `PENTAGON, 48K, 128k, PLUS3, TSL, ATM3, ATM710, ATM450, PROFI,
  SCORPION, PROFSCORP, GMX, KAY, QUORUM, LSY256, PHOENIX`.
- Add a note that model availability is runtime-dependent (build/branch) and
  `GET /emulator/models` is authoritative.
- Longer term (P3): generate the doc table from `config.h` in CI, or drop the
  list from docs and point at the endpoint.

### P0-4. Build/branch fingerprint

> ✅ **DONE (2026-09-14, commit `cab13b99`).** `server` block (version, git
> branch/commit, build type, `models_creatable`) exposed via
> `/emulator/status` and the MCP `emulator_manage` `server` action.

Closes C-1 (agent-build mismatch class).

Add to `GET /emulator/status` (and optionally `/emulator` list):

```json
{ "server": { "version": "…", "git_branch": "atm", "git_commit": "b3bfc029…",
               "build_type": "Release", "models_creatable": ["…"] } }
```

`models_creatable` can be computed once at startup by dry-running the
decoder/config resolution for every table entry — this also turns "master
cannot create ATM" into a first-class, queryable fact instead of a 400
surprise.

**Accept:** an agent can refuse to triage ATM issues when `git_branch` is
`master`, before wasting a session.

---

## P1 — Mode-aware & port-aware state (the named scenarios)

### P1-1. Mode-aware screen state

Closes B-1, B-3 (partially), complements bug-report #3.

Rewrite `getStateScreen` / `getStateScreenMode` (state_screen_api.cpp) on top
of `Screen::GetVideoMode()` + `rasterDescriptors[mode]`:

```json
{
  "model": "ATM710", "video_mode": "ATM16",
  "mode_name": "ATM 16c",
  "resolution": "320x200", "framebuffer": "448x288",
  "colors": 16,
  "is_banked": true,          /* replaces is_128k, model-agnostic */
  "active_screen": 0,
  "screen_memory": { "source": "paging-window", "note": "ATM video surface follows FF77 paging" },
  "border_color": 0,
  "mode_selectors": { "pFF77": "0x00", "mode_bits": "16c" }
}
```

Keep the old fields for one release (`display_mode` mirrors `video_mode`)
to avoid breaking existing consumers; mark deprecated in OpenAPI.

**Accept:** on an ATM instance with mode ATM16 active, both endpoints report
`ATM16`/320x200; `/state/screen` and `/video/beam` agree on the mode. The
48K/128K golden responses stay byte-identical for those models (regression
fixtures).

### P1-2. Unified paging state endpoint

> 📝 **Design ready (2026-09-15):** [port-tags-paging-design.md](port-tags-paging-design.md)
> supersedes the sketch below — decoder-registered ports get semantic tags
> (memory / ROM / screen / sound members) plus `PagingLatch` live bindings,
> the decoder owns tag-indexed collections, and `/state/paging` reports a
> self-describing `latches` array (instead of the hardcoded per-model field
> dump below) next to the same `banks` table, with full parity. The sketch is
> kept for the acceptance criteria, which carry over verbatim.

Closes B-2, E-1 (reporting side).

`GET /api/v1/emulator/{id}/state/paging`:

```json
{
  "model": "ATM710",
  "p7FFD": "0x10", "p1FFD": "0x00", "pDFFD": null, "pFDFD": null,
  "pFF77": "0x00", "pEFF7": "0x00", "aFE": null, "aFB": null,
  "banks": [
    { "bank": 0, "address_range": "0x0000-0x3FFF", "type": "ROM", "page": 1, "name": "dos" },
    { "bank": 1, "address_range": "0x4000-0x7FFF", "type": "RAM", "page": 0 },
    { "bank": 2, "address_range": "0x8000-0xBFFF", "type": "RAM", "page": 5 },
    { "bank": 3, "address_range": "0xC000-0xFFFF", "type": "RAM", "page": 0 }
  ],
  "trdos_active": false, "paging_locked": false
}
```

Fields: null where the model has no such latch; per-bank pages from the
memory manager (the atm branch fixed `GetRAMPageFromAddress` for ATM — the
endpoint surfaces it; on master it reports the correct values for the seven
creatable models and the existing 65535 artifact is fixed alongside). Add an
MCP `inspect_state` aspect `paging`.

**Accept:** after `OUT (#FF77),0` on ATM710, `/state/paging` shows
`pFF77=0x00`; bank table never reports 65535 for mapped RAM; Scorpion shows
`p1FFD` + window latch; 128K output matches today's `/state/memory` values.

### P1-3. Screen digest active-surface mode

> ✅ **DONE (2026-09-14, this session).** `mode=active` implemented via
> `Screen::GetActiveSurfaceRAMPages(mode, p7FFD, bankedZX)`: ATM hardware
> modes (M_ATM16/ATMHR/ATMTX/ATMTL) hash the 7FFD-selected bit-plane pair
> `{videoPage-4, videoPage}` exactly like the DrawATM* renderers; ZX modes keep
> pages 5/7. Response carries `active_surface {video_mode, pages}`; invalid
> mode values are a 400; `banks=`/`start,end=` still override. Tests:
> `Screen_ActiveSurface_Test` (screenactivesurface_test.cpp). Note the
> flipping-surfaces acceptance needs an ATM build to exercise live; the
> page-derivation logic is unit-tested on master.
>
> ✅ **Parity (2026-09-15).** Replicated to CLI (`digest --active`, prints
> `Mode: <name>, pages: …`), Lua (`screen_digest(nil,nil,nil,"active")`)
> and Python (`screen_digest(mode="active")`, ValueError on a bad mode) —
> all over the same `GetActiveSurfaceRAMPages` source.

Closes B-4.

Add `mode=active` (default remains current behavior) to
`/state/screen/digest`: hash the memory region(s) the current video mode
actually displays, derived from the mode + paging state (ZX: pages 5/7 as
today; ATM/TSConf: the paging-window-derived surface). Keep the explicit
`banks=`/`start,end=` overrides.

**Accept:** on ATM, digest `mode=active` changes when (and only when) the
displayed surface changes — e.g. flipping FF77 between ZX and 16c surfaces
with constant underlying pages flips the digest.

### P1-4. Porttrace decode rules for the ATM decoders

Closes C-2 (atm-branch side).

On the `atm` branch, implement `getPortTraceDecodeRules()` in
`PortDecoder_ATM710` / `PortDecoder_ATM3` using the Pentagon128 override as
the template (rules covering #FE, #7FFD, #FFFD/#BFFD, #1FFD window latch,
#FF77 mode/paging, Beta128 gated set, mouse ports, registered peripherals),
and extend `getPortTraceSessionInfo`'s model-name switch
(portdecoder.cpp:423-433) with ATM3/ATM710/ATM450/TSL. Master should get the
same treatment for the Profi decoder (already on master).

**Accept:** a porttrace session on ATM710 yields events with device
attribution and rule indices; session info reports `modelName: "ATM710"`.

### P1-5. Static port-map introspection endpoint

> ✅ **DONE (2026-09-14, this session).** `GET /api/v1/emulator/{id}/ports`
> implemented from `PortDecoder::getPortMapEntries()` (per-model switch
> mirroring the IsPort_* decode equations: FE/AY universal rows, per-model
> paging latches, fitment-conditional mouse + Beta128 rows with gate strings,
> registered-handler rows deduped) plus a `live` block (`trdos_active`,
> `mouse_ports_decoded`, `mouse_routing_note`, `shadow_monitor_paged`).
> ATM FF77 rows land with the atm-branch decoders (P1-4). Tests:
> `PortDecoder_PortMap_Test` (portdecoder_portmap_test.cpp, 11 cases);
> OpenAPI `openapi_ports.inc`; smoke-verified live on 128k + SCORPION.
>
> ✅ **Parity (2026-09-15).** Replicated to CLI (new `ports` command: the same
> rows as a text table + the live routing block) and to Lua/Python
> (`ports_map()` with the same `model`/`entries`/`live` shape) — all from the
> same `getPortMapEntries`/`GetMouseRoutingState` sources.

Closes C-3 and D-1 (mouse Q4).

`GET /api/v1/emulator/{id}/ports`:

```json
{
  "model": "ATM710",
  "entries": [
    { "port": "0xFE",  "mask": "0x0001", "device": "Keyboard/Beeper/Border", "gate": null },
    { "port": "0x7FFD","mask": "0x8002", "device": "Memory paging",          "gate": null },
    { "port": "0xFF77","mask": "0x00FF", "device": "ATM mode/paging",        "gate": null },
    { "port": "0xFADF","mask": "0x0020", "device": "Kempston mouse buttons", "gate": "!CF_TRDOS" },
    { "port": "0x1FFD","mask": "0x8002", "device": "Scorpion window latch",  "gate": "model==SCORP*" }
  ],
  "live": {
    "trdos_active": false,
    "mouse_ports_decoded": true,
    "shadow_monitor_paged": false
  }
}
```

Implementation: derive `entries` from the same decode-rule tables as P1-4
(single source); derive `live` from `EmulatorState` flags + a small decoder
query (`WouldDecodeMouseNow()` or equivalent). Add `mouse_ports_decoded` to
the `/mouse/status` response as well (it is the Q4 field).

**Accept:** on Scorpion with a TR-DOS session open, `/ports` shows
`mouse_ports_decoded: false`; closing the session flips it true. On 48K,
mouse entries are absent (device not fitted) while `present:false` in mouse
status explains why.

---

## P2 — Peripheral observability

### P2-1. Mouse status routing + `mouse` aspect

> ✅ **DONE (2026-09-14, this session).** `/mouse/status` carries
> `routing {ports_decoded, note}`; `mouse` is in the `inspect_state` aspect
> enum (handler mirrors `fdc`, summary reports "ports decoded/shadowed" + the
> gate reason). Tests: `McpTools_Test.InspectState_MouseAspect_*` (2 cases) +
> `PortDecoder_PortMap_Test.MouseRouting_*` (3 cases incl. the Scorpion DOS
> trigger and Shadow Monitor canonical-port behavior).
>
> ✅ **Parity (2026-09-15).** The same routing answer now also surfaces on CLI
> (`mouse status` `Routing:` line, `CliMouseFormat_test.FormatRouting_*`),
> Lua (`mouse_status().routing`) and Python (`mouse_status()['routing']`) —
> on status only, not on injection responses, matching the WebAPI shape.

Closes D-1, D-2.

- Extend `MouseStateSnapshot`/`stateToJson` with `ports_decoded` (from P1-5's
  live query) and `decoder_note` (e.g. "hidden by CF_TRDOS").
- Add `mouse` to the `inspect_state` aspect enum (mcp-tools.cpp:571-572 +
  handler case; trivially mirrors `fdc`).

**Accept:** `inspect_state aspects=["machine","mouse"]` on two configs (one
with TR-DOS open) shows differing `ports_decoded` in one call.

### P2-2. MoonSound automation section in the design (do now, before implementation)

Closes D-3 (future-proofing). Write into
`docs/inprogress/2026-09-13-moonsound/opl4-unreal-ng-integration.md`:

- **State:** a `DeviceState::Moonsound` report following the FM pattern:
  FM half (OPL4/YMF262-compatible registers, timers, key-on) and PCM half
  (YRW801 sample access, mixer), exposed as `/state/audio/moonsound` and
  `/state/audio/moonsound/{half}`.
- **MCP:** `inspect_state` aspects `audio_opl4_fm` + `audio_opl4_pcm`
  (matching the design's two registry sources D5 — one stereo source cannot
  express "mute FM, keep samples").
- **Control:** feature flag + mixer volume via the existing `/features` and a
  settings surface (P2-3), so agents can enable/disable and observe gain.
- **Ports:** MoonSound entries in the P1-5 port map (the design already fixes
  the port decode: #C2-#C3 style indexed access — encode it as rules).
- **Capture:** the two registry sources in `/audio/capture` and multitrack
  (the `AudioSourceType::Moonsound` placeholder splits per D5).
- **TTD:** the design's `opl4-ttd-integration-tdd.md` already plans chip
  state capture — cross-link the WebAPI state to it.

**Accept:** the integration TDD contains an "Automation & observability"
section listing endpoints/aspects/port rules above; implementation PRs are
reviewed against it.

### P2-3. Capability & settings introspection

Closes A-4.

`GET /api/v1/emulator/{id}/capabilities` (read-only snapshot):

```json
{
  "peripherals": {
    "mouse": { "fitted": true, "type": "KEMPSTON", "wheel": "KEMPSTON" },
    "ay": { "variant": "YM2149", "chips": 2, "turbosound": true },
    "covox": { "fitted": true, "variant": "PENTAGON" },
    "gs": { "fitted": false },
    "moonsound": { "fitted": false },
    "beta128": { "fitted": true }
  },
  "mixer_volumes": { "ay": 8000, "beeper": 8000, "covox": 8000 },
  "speed_multiplier": 1
}
```

Source: the parsed `[INPUT]`/`[SOUND]` config values + device presence. Whether
to expose *writes* (fitment changes) is a separate decision — start
read-only; a follow-up `PUT` can reuse the settings/{name} mechanism with a
restart-required flag.

**Accept:** an agent can answer "is a mouse fitted on this config" in one
call without reading `data/configs` from disk.

### P2-4. GS/Covox DeviceState reports

Closes D-3 (placeholders). When those devices get core attention: replace the
`not_implemented` stubs with real `DeviceState` reports (state_audio_api.cpp:474-499)
and add MCP aspects. Low urgency — do opportunistically with core work.

---

## P3 — Knowledge & workflow layer

### P3-1. Per-machine MCP resources

Closes F-1. Static markdown, one per machine family:

- `unreal://machine/atm-turbo` — port map (#FE/#7FFD/#FF77/#FADF... table),
  FF77 mode selector table (16c/MC/ZX/TX/TL), memory windows, ROM layout,
  known quirks (CMOS/RTC on ATM3).
- `unreal://machine/profi` — #DFFD paging, 1024K layout, video mode.
- `unreal://machine/zx-evo` — TSConf port space, relations to `TSL` model.
- `unreal://machine/moonsound` — port decode, register summary, mix levels
  (write together with P2-2).
- Update `unreal://memory-map` to note it covers ZX-class models only, or
  split per model.

**Accept:** resources listed in `resources/list`; content reviewed against
the port-decoder sources (single review pass per machine, ideally by the
branch author).

### P3-2. ROM signature identification

> **📝 Design note (2026-09-15):** the paging design
> ([port-tags-paging-design.md](port-tags-paging-design.md) §5.2) already
> delivers the recognition+naming half on all paging surfaces, from the
> existing core
> catalog (`ROM::_signatures`, SHA-256 via `SignatureCache`) plus a new
> `ROM::GetROMPageRole` layout table: ROM bank rows carry
> `role`/`name`/`signature`, and `role` ≠ `name` flags a wrong-ROM load at a
> glance. The remaining P3-2 work is catalog growth (more ROMs, optional
> data-file loading) and the `rom` aspect's CLI/Lua/Python parity.

Closes E-2. Extend the `rom` aspect / `/state/memory/rom` with a
`signatures` block: match first N bytes + size against a curated table
(`data/symbols/` or `docs/` yaml: name, size, expected bytes/checksum,
machine). Start with the ROMs that ship in `data/rom/`.

**Accept:** on a healthy ATM710 boot the aspect names the DOS/SOS/SYS ROMs;
a wrong-path regression (ProfROM `:0` class) is reported as
`unidentified` instead of silently dumping bytes.

### P3-3. Triage recipes (docs + optional tool)

Closes F-2.

- Document the canonical "new machine doesn't boot" recipe in
  `docs/features/mcp/` (or a resource): create → verify `machine` aspect →
  ROM signature → `run_frames` + `frame_cost` → `screen_digest mode=active` +
  OCR → porttrace if stuck in a loop → TTD find-last.
- Optional follow-up: a `machine_selftest` MCP tool encoding the sequence
  (bounded, read-only) — only after P0/P1 land, so it has real signals to
  check.

**Accept:** the recipe doc exists and references only implemented
endpoints/aspects.

---

## Suggested sequencing

> Status (2026-09-14): step 1 ✅ done (`34546478`, `cab13b99`); P1-5 + P2-1
> from steps 3–4 ✅ done (this session); P1-3 ✅ done (this session). Steps 2
> (P1-1 + P1-2) and P1-4, P2-2/P2-3, P3 remain open.
>
> Status (2026-09-15): the done P1-3/P1-5/P2-1 surfaces were replicated to
> CLI/Lua/Python (full four-interface parity) and documented across
> `control-interfaces/`; the `GetVideoModeName` identity fix landed with
> regressions. Still open: steps 2 (P1-1 + P1-2), P1-4, P2-2/P2-3, P3.

1. **P0-1..P0-4** (one small PR batch; no schema risk) — immediately stops
   misattributed sessions.
2. **P1-1 + P1-2** (reporting over existing core state) — unblocks ATM video
   triage on the `atm` branch; fix `/state/memory` values in the same PR.
3. **P1-4** on the `atm` branch together with its decoders (same author,
   same context) + **P1-5** building on the rules.
4. **P2-2** before MoonSound implementation starts; **P2-1/P2-3** alongside.
5. **P3** opportunistically; resources are cheap enough to write while
   reviewing the atm branch.

## Testing & documentation obligations

Per repo policy: every endpoint addition needs OpenAPI `.inc` entries under
`core/automation/webapi/src/openapi/`, WebAPI tests
(`tools/verification/webapi/` + core-tests where drogon-free), MCP aspect
tests (`McpTools_Test`), and docs updates
(`docs/emulator/design/control-interfaces/webapi-interface.md`,
`docs/features/mcp/README.md` tool table). The acceptance criteria above are
written to fold directly into GTest cases.
