# Automation Triage Gaps — New Machine Configurations & Peripherals

**Date:** 2026-09-14
**Analysis target:** MCP + WebAPI surface vs. triage needs for newly developed
machine configurations and peripherals:
ATM Turbo (ports, screen modes), Profi, ZX Evo (TSConf), MoonSound, and the
Kempston Mouse across different configs.
**Baseline:** `master` branch, working tree as of 2026-09-14, cross-referenced
with the `atm` branch (`git diff master...atm`).
**Trigger:** the 2026-09-10 ATM video-mode debugging session
(`docs/inprogress/2026-09-10-atm-debugging/bug-report.md`) documented six bugs,
three of which are automation-surface gaps that remain open today.

## Status update — 2026-09-14 (end of day)

Remediation started the same day. ✅ Done (gap-analysis IDs in parentheses):

- ✅ **P0-1** Machine identity in `GET /emulator/{id}` / `machine` aspect (A-1) — committed `34546478`
- ✅ **P0-2** Strict model create/switch — loud 400, no silent 48K fallback (A-2) — committed `34546478`
- ✅ **P0-3** Authoritative model list in `AGENTS.md` (A-3) — committed `34546478`
- ✅ **P0-4** Build/branch fingerprint + `models_creatable` (C-1) — committed `cab13b99`
- ✅ **P1-3** Screen digest `mode=active` — follows the displayed surface (B-4) — implemented this session, pending commit
- ✅ **P1-5** `GET /ports` static port map + live routing flags incl. mouse Q4 (C-3, D-1) — implemented this session, pending commit
- ✅ **P2-1** Mouse status `routing` field + `mouse` aspect in `inspect_state` (D-1, D-2) — implemented this session, pending commit

Still open: P1-1 (B-1), P1-2 (B-2/E-1), P1-4 (C-2, atm-branch side), P2-2,
P2-3 (A-4), P2-4 (D-3), P3-1..P3-3. The body of each file below remains the
pre-fix snapshot; per-gap status lives in the gap-analysis summary matrix and
per-item ✅ marks in recommendations.md.

## Status update — 2026-09-15 (parity round)

The P1-3 / P1-5 / P2-1 surfaces were replicated to **all four endpoint-parity
interfaces** (CLI, Lua, Python — WebAPI/MCP were already done), per the
automation parity rule (same information from the same core sources:
`Screen::GetActiveSurfaceRAMPages`, `PortDecoder::getPortMapEntries`,
`PortDecoder::GetMouseRoutingState`):

- ✅ CLI: `digest --active` (prints the derived `Mode:`/pages), new `ports`
  command (static map table + live routing block), `mouse status` `Routing:` line
- ✅ Lua: `screen_digest(nil,nil,nil,"active")` (+ `active_surface`), new
  `ports_map()`, `mouse_status()` carries `routing = {ports_decoded, note}`
- ✅ Python: `screen_digest(mode="active")` (ValueError on bad mode), new
  `ports_map()`, `mouse_status()` carries `routing` — compile-verified in an
  `ENABLE_PYTHON_AUTOMATION=ON` build
- ✅ Docs updated: `command-interface.md` (digest `--active`, `ports` row,
  §3.3 parity-table row, `mouse status` routing, `state ports` planned-row
  note), `cli-interface.md`, `lua-interface.md`, `python-interface.md`,
  control-interfaces `README.md`
- ✅ Bug found & fixed while live-verifying: `Screen::GetVideoModeName` had no
  cases for `M_ZX128`/`M_PENTAGON128K`/`M_SCORPION`, so 128k/Pentagon/Scorpion
  machines reported `video_mode: "Unknown"` — including the P0-1 machine
  identity payload. Regression tests: `Screen_VideoModeName_Test`.

All of the above is implemented and verified (zero warnings, full 20-shard
suite green, live CLI/Lua/WebAPI smoke on 128k + SCORPION) — pending commit.

## Status update — 2026-09-15 (P1-2 design)

📝 **Design drafted (not implemented):**
[port-tags-paging-design.md](port-tags-paging-design.md) addresses E-1 / B-2
(P1-2) via a tagged port registry — decoder-registered ports carry semantic
tag(s) (memory / ROM / screen / sound with per-soundcard members), the
decoder owns tag-indexed collections, and `/state/paging` (latches + banks)
is assembled from them with full WebAPI/MCP/CLI/Lua/Python parity. See its
§11 for the rollout sequence.

✅ **Design approved + Phase 1 implemented (2026-09-15):** the design review
approved with four minor refinements (constexpr tag operators, default member
initializers, `ReadPagingLatch` naming, decoded-keys dictionary — all folded
into the doc; §10 questions resolved). Phase 1 landed core-only, no surface
change: `PortTag`/`PagingLatch` enums, tagged rows in `getPortMapEntries()`,
tagged `RegisterPortHandler` overload, `ReadPagingLatch` + the tag query
methods, `PortDecoder_PortTag_Test` (13 cases). Phases 2–3 landed later the
same day — see the next bullet.

✅ **Phases 2–3 implemented (2026-09-15) — tagging wired to every
automation surface:** WebAPI `GET /state/paging` (+ OpenAPI), MCP `paging`
and new `ports` aspects, CLI `paging` command + `ports` **Tags/Latch**
columns, Lua/Python `paging_state()` + `ports_map()` `tags`/`latch` keys,
`/ports` rows carrying `tags`+`latch` everywhere, §5.2 ROM
`role`/`name`/`signature` on all paging surfaces. All names decode through
three core single-source serializers
(`PortTagSetToStrings`/`PagingLatchToString`/`DecodePagingLatch`) plus
`ROM::GetROMPageRole` — the per-surface copies were deleted. Tests grew to
19 cases. Control-interfaces + OpenAPI + MCP docs updated.

📝 **§5.2 added (2026-09-15): ROM page identification** — recognized
signatures and naming for easy visual identification of ROM bank rows in
`/state/paging`: `name` (content, from the existing `ROM::_signatures`
SHA-256 catalog) vs `role` (the per-model layout slot, moving into core as
`ROM::GetROMPageRole`); a role/name mismatch is the one-glance
"wrong ROM loaded" signal. Wired into §5 example, §6 parity rows, §8
`RomIdentification_Test`, §9 acceptance, §11 Phase 2.

## Files

| File | Content |
|:--|:--|
| [current-state.md](current-state.md) | Inventory of what exists today: WebAPI endpoint groups, MCP tools/resources, runtime features, machine model table, peripheral implementation status, branch topology. |
| [gap-analysis.md](gap-analysis.md) | The 18 findings, grouped A–F by triage workflow stage, each with evidence (`file:line`, verified on master 2026-09-14) and impact. |
| [recommendations.md](recommendations.md) | Prioritized remediation plan (P0–P3) with concrete endpoint/tool proposals, response schemas, and acceptance criteria. |
| [triage-workflows.md](triage-workflows.md) | Before/after walkthroughs for the concrete triage scenarios named in the analysis (ATM black screen, mouse-on-configs, MoonSound bring-up). |
| [port-tags-paging-design.md](port-tags-paging-design.md) | Design for P1-2 / E-1 / B-2: tagged port registry (`PortTag` bitmask + per-soundcard tags, `PagingLatch` live bindings) on the decoders, decoder-owned index collections, and the tag-driven `/state/paging` endpoint (incl. §5.2 ROM page identification — `role`/`name`/`signature` per ROM bank) with full parity. |

## Executive summary

The emulator core is ahead of the automation layer. The core already knows about
ATM video modes (`VideoModeEnum`), ATM/Profi paging registers (`pFF77`, `aFE`,
`p1FFD`, `pDFFD`), per-model port decoders, and a mouse input funnel with TTD
journalling — but the WebAPI/MCP surface that agents use to *triage* was built
around 48K/128K semantics and reports none of this. The `atm` branch adds
~7.6k lines of machine bring-up work (port decoders, configs, video-mode test
suites) with **zero** changes under `core/automation/`.

The five highest-impact gaps:

1. **No machine identity.** `GET /emulator/{id}` — the source for the MCP
   `machine` aspect — returns no model, RAM size, video mode, or peripherals.
2. **Silent 48K fallback on create.** Requesting a model that cannot be
   initialized (every ATM/ZX-Evo/Profi request on master) returns `201` with a
   default 48K machine and no indication of the substitution.
3. **Screen state APIs are hardcoded to standard mode.** `video_mode` is the
   literal string `"standard"`; the ATM/Profi/TSConf mode machinery in the core
   is invisible (`/video/beam` is the only exception).
4. **No port-decode introspection.** Porttrace decode rules exist only for
   Pentagon128; there is no endpoint that answers "which devices respond to
   which ports on this machine right now" — the central question for ATM port
   and Kempston-mouse-routing triage.
5. **Peripheral observability is 2 of N.** The `DeviceState` report pattern
   exists (FM, FDC) but GS/Covox are placeholders, MoonSound has no surface and
   its design has no automation section at all.

Remediation is small relative to impact: the P0 items are field additions and
error-path fixes in existing handlers; the P1 items are reporting endpoints
over state the core already holds. Details and schemas in
[recommendations.md](recommendations.md).

## Scope and non-goals

**In scope:** the automation surface only (WebAPI REST, MCP tools/resources,
and the agent-facing documentation that feeds them). Core emulation
correctness (e.g. the ATM16 renderer stub) is referenced where it constrains
the API but is tracked by the `atm` branch work, not proposed here.

**Non-goals:** GUI (unreal-qt) debugger surface; CLI/Lua/Python parity (the
WebAPI is the single source of truth that MCP fans out to — fixing it first
fixes MCP); performance work.

## Verification method

All `file:line` citations were verified on the master working tree on
2026-09-14 with full-file reads and `rg` searches; branch-state claims come
from `git diff --stat master...atm`. Where a fact could not be re-verified
(e.g. runtime behavior of the `atm` branch), the document says so explicitly.
