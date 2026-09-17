# Cumulative Plan — All Unfinished Work

**Created:** 2026-09-16, from a full audit of `docs/inprogress/` cross-checked against code and git.
**Maintenance rule:** this is the single priority-ordered view across every folder marked `TODO.md`.
When an item lands (or priorities shift), update the folder's `TODO.md` and this file in the same
change. Folders marked `DONE.md` are finished and deliberately not listed here.

**Priorities:** **T1** = do next (high value, small effort) · **T2** = high value, medium-large
effort · **T3** = cheap enablers / opportunistic · **T4** = deferred with rationale (revisit when
a trigger fires).

---

## T1 — do next (high value ÷ effort)

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 1 | **TTD docs truth pass** (G-8/G-9): `command-interface.md` §8, `webapi-interface.md`, `lua/python-interface.md` describe a partly imaginary TTD API; AGENTS.md and MCP docs have zero TTD content; invalidation ordering rules buried in one design doc | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (TD-6) | Docs-only, small effort, stops misdirection of every doc-following agent — same failure class as the already-fixed P0-2/P0-3 |
| 2 | **Finish MCP first-class TTD** (G-1/TD-1): full `time_travel` action set (start/stop/seek/step/reverse/find-last/dump/load), `ttd` aspect in `inspect_state`, briefing + `docs/features/mcp/` section | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (G-1) | TD-4 already seeded the tool (status + bookmark actions); the rest are thin `ForwardCall` wrappers. MCP is the primary agent transport |
| 3 | **P1-1 mode-aware screen state**: `/state/screen` and `/state/screen/mode` still return literal `"standard"` (verified in `state_screen_api.cpp:79,215` on 2026-09-16); rewrite on `Screen::GetVideoMode()` + `rasterDescriptors` | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (B-1) | Last of the "endpoint actively lies during new-machine triage" class; P1-2 (paging) already landed, this completes the pair |
| 4 | **TD-3 Phase 2 — `POST /memory/dump` + `TempFileTracker`**: server-side binary dump with metadata-only response; session/transient(TTL)/pinned retention; startup sweep; path canonicalization | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (G-6, TD-3 §Phase 2) | Phase 1 (map/hexdump/sparse) committed `372c3840`; memory is the only RE extraction path without server-side file output |
| 5 | **Non-standard loader tapes fail deterministically** (ERR_NR watchdog false-stop): loader/unpacker writes to `$5C3A` trip the terminal stop **and** the in-flight-block consumption in `Tape::handleFrameEnd` → poll-resume restarts at the wrong block → deterministic derail; 5 of 7 DIZZY_X variants + echology affected, root cause evidenced (death points, real/warp-speed invariance, fasttape+turbotape exonerated) | [2026-08-30-fast-tape-loading](2026-08-30-fast-tape-loading/) → [TODO.md](2026-08-30-fast-tape-loading/TODO.md) | Small, well-isolated fix (gate the ERR_NR stop for non-ROM writers; reconsider block consumption on this path) + regression tests; repro harness already lives in `scratch/nonstd-loader-repro/` |

## T2 — high value, medium effort

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 6 | **Expression evaluator + conditional breakpoints** (slices 1d/1e first): `bpcondition` engine, wiring into `Handle*`, hit counters (1f), ranges (1c), frontends (1h); consumes [2026-08-26-expression-evaluator](2026-08-26-expression-evaluator/) design; unblocks [2026-08-26-breakpoint-enhancements](2026-08-26-breakpoint-enhancements/), DeZog fast conditions, `run_until_condition` | [2026-08-17-conditional-breakpoints](2026-08-17-conditional-breakpoints/) | The only remaining **P0** in the feature-parity matrix. Phase 0 speed substrate landed (12–18× miss-path) with benchmarks as regression gates |
| 7 | **TD-5 TTD timeline summary**: `GET /ttd/timeline` with O(limit) downsampling; design complete & reviewed | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) → `designs/ttd-timeline-summary-design.md` (G-2) | Pairs with bookmarks + range find-last into a coherent reverse-RE toolkit; zero new capture cost |
| 8 | **P1-4 porttrace decode rules**: Profi decoder on master now; ATM710/ATM3/ATM450/TSL with the atm-branch decoders; extend `getPortTraceSessionInfo` model names | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (C-2) | Without rules, port traces on new machines are raw address streams |
| 9 | **P2-3 capabilities/settings introspection**: read-only `GET /capabilities` (peripherals fitted, mixer volumes, speed) | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (A-4) | Collapses "not fitted vs fitted-and-broken" triage fork into one call |
| 10 | **ATM/ZX-Evo branch merge onto master**: port decoders, configs, renderers (bug #5 `DrawATM16` stubs), creatability | [2026-09-10-atm-debugging](2026-09-10-atm-debugging/) + atm branch | Largest single unblocker: P0-2/P0-4 made ATM triage honest, this makes it possible. Includes TTD registry Phases 2–5 ([2026-09-10-ttd-registry-integration](2026-09-10-ttd-registry-integration/)) riding the same branch |
| 11 | **MoonSound program**: P2-2 automation/observability section in the design **first**; then wide-mix + soft limiter (prerequisite D7), `libopl4`, device wiring, registry sources (D5), TTD (D9), recording, UI | [2026-09-13-moonsound](2026-09-13-moonsound/) | All design, zero code today. Writing P2-2 before implementation is explicitly the lesson from the A/B/E gap class |
| 12 | **Flux bridge — KryoFlux/Greaseweazle direct integration**: **B0** wire the already-shipped HFE/SCP loaders (`a637bfa7`) into `LoadDisk`/`SaveDisk` dispatch; **B1** KryoFlux stream import (loader); **B2–B4** live Greaseweazle bridge (`IFluxAdapter` → `BridgeDrive`, capture modes, TTD materialize-then-emulate, write path with safety interlocks); automation parity surface | [2026-09-02-universal-track-model](2026-09-02-universal-track-model/) → [flux-bridge.md](2026-09-02-universal-track-model/flux-bridge.md) | Biggest open item of the track-model work. WinUAE FloppyBridge is the proven prior art; Greaseweazle protocol is public domain (GPL-3.0-clean); KryoFlux DTC software stays offline-only (licence). HFE/SCP loaders themselves already landed — former T4 item retired |
| 13 | **Fast disk loading (TR-DOS `$3D13` service trap)**: ROM-hook sibling of fasttape — serve LOAD_FILE/READ_SECTORS/FIND_FILE/READ_DESCRIPTOR host-side from the universal track model; covers machine-code loaders doing `CALL $3D13` (~90 % of standard disk software) plus BASIC LOAD/RUN transitively via internal service calls; r0 design complete (ROM bytes verified, decline matrix, differential test plan) | [2026-09-16-fast-disk-loading](2026-09-16-fast-disk-loading/) → [design.md](2026-09-16-fast-disk-loading/design.md) | Phase 0 (private-stack return contract extraction from `trdos503.rom`) gates implementation; architecture mirrors the proven tape trap |

## T3 — cheap enablers, opportunistic

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 14 | **P3-1 per-machine MCP resources** (`unreal://machine/atm-turbo`, `profi`, `zx-evo`, later `moonsound`) | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (F-1) | Static markdown; kills the per-session hardware-knowledge tax. Cheap enough to write while reviewing the atm branch |
| 15 | **P3-3 triage recipes / canonical RE recipe**: "record-then-reverse" workflow incl. tape-load ordering rule (G-7 docs half, G-9); optional `machine_selftest` later | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (F-2, TD-8 doc half) | Encodes what the umt23x litmus session learned |
| 16 | **P3-2 remainder — ROM signature catalog growth + `rom` aspect CLI/Lua/Python parity** | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (E-2) | Recognition+naming half already shipped via `/state/paging` §5.2 |
| 17 | **DeZog user guide** (final close-out item): end-user doc for setting up DeZog + unreal — `tools/verification/dezog/README.md` currently serves as the launch.json reference | [2026-08-27-dezog-integration](2026-08-27-dezog-integration/) | Manual E2E with the VS Code extension passed 2026-09-16 — DeZog (zesarux launch mode) fully functional **incl. backward debugging** against TTD; protocol/adapter/server/tests/host all landed |
| 18 | **Tape manager P3 (CSW) + P7 polish**: CSW v1/v2 loader + fixtures (PZX optional), `tr()`/translation file, §8.3 visual nits, then move the docs out of `inprogress/` | [2026-09-01-tape-manager](2026-09-01-tape-manager/) | P1/P1b, P2 (TZX rewrite), P4 (seek/position), P5 (control planes), P6 (Qt window, r6) all landed — verified against code 2026-09-16 |
| 19 | **Python automation build in CI**: `ENABLE_PYTHON_AUTOMATION=OFF` in all build dirs (module builds CPython from source); recent Lua/Python surface work is only syntax-checked | (build infra; noted by TD-3/TD-4 verification) | Removes the recurring "not compile-verified" caveat |
| 20 | **P2-4 GS/Covox DeviceState reports** | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (D-3) | Do opportunistically with core sound work |

## T4 — deferred (revisit on trigger)

| # | Item | Tracked in | Trigger / rationale |
|:--|:--|:--|:--|
| 21 | Schema-driven generation of CLI/WebAPI/Lua/Python/docs from one command schema (old Phase 4) | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Manual parity replication cost is now real (2026-09-15 round ×4); grows with every new surface — revisit after T1/T2 |
| 22 | WebSocket event push, `subscribe`, Lua/Python callbacks (old 0.3.3–0.3.6) | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Polling + `inspect_state` currently sufficient |
| 23 | TD-7 dedicated coverage-index query (last 20% of G-5) | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) | 80% covered via TD-2 range find-last |
| 24 | TD-8 code half: `covered_from` window reporting (G-10) | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) | Low severity; fold into next TTD API touch |
| 25 | `get_framebuffer` raw, Python numpy wrapper (old 3.1.1/3.1.3) | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Digest/OCR/screenshot cover current demand; TD-3 may reduce need further |
| 26 | Headless deterministic mode + RZX-compatible input record/playback (old 3.2.x) | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | TTD input journals cover replay; RZX interop is the remaining value |
| 27 | Realtime monitoring / segmentation widget | [2026-02-23-realtime-monitoring](2026-02-23-realtime-monitoring/) | Analysis + widget design done; no code. Reassess against analyzer value (see #28) |
| 28 | Interrupt analyzer, routine classifiers, beam-to-execution correlation | [2026-01-14-analyzers](2026-01-14-analyzers/) | Analyzer manager + TRDOS analyzer landed; the rest are research designs |
| 29 | HUD Phase 4: detach `hud/core` into core + automation surface (`api.md` §6) | [2026-09-07-hud-layer](2026-09-07-hud-layer/) | Deferred by design; trigger = a second client (SDL3 player, screen-viewer) wants the HUD |
| 30 | Shared-memory coherency / high-performance bridge (gRPC, zero-copy) | [2026-08-27-shared-memory-coherency](2026-08-27-shared-memory-coherency/) + MCP Phase 3 | Research done; only if HTTP loopback proven bottleneck |
| 31 | MP4/WebM recording, semantic frame diffing, FFT audio, auto-reload/fuzzy/batch symbols | [2026-08-17-mcp](2026-08-17-mcp/) (Phase 3, 2026-09-10 gap report) | Explicitly deferred, mostly Low priority there |
| 32 | DiskManager, save modes, heatmaps | [2026-01-24-diskimage-modernization](2026-01-24-diskimage-modernization/) | Layout work superseded by universal track model (done); these are UI/UX extras |

## Documentation debt (fix alongside T1-1)

- [2026-08-26-automation-gaps/feature-parity.md](2026-08-26-automation-gaps/feature-parity.md) — stale rows: TTD now full parity, symbolic disasm, screenshots, beam; only conditionals/hit-count/actions/BP-groups/`run_until_condition` rows still real.
- [2026-08-26-automation-gaps/action-plan.md](2026-08-26-automation-gaps/action-plan.md) — summary says Phase 1B/1C at 0; DeZog landed (see its folder), MCP superseded-complete (13 tools). Banner added 2026-09-16.
- [2026-08-17-mcp/2026-09-10-automation-reconciliation.md](2026-08-17-mcp/2026-09-10-automation-reconciliation.md) — TTD table predates the full WebAPI/CLI/Lua/Python TTD surface.
- Root-level legacy files (`ANALYSIS_SUMMARY.md`, `COMPREHENSIVE_ANALYSIS.md`, `cpu-optimization-*.md`, `implementation_guide.md`, `memory_management.md`, `ports_architecture_proposal.md`, `risk_assessment.md`, `sound_buffer_analysis.md`, `testing_strategy.md`, `architecture_overview.md`) — pre-date the folder convention (2026-01); superseded by dated folders and permanent docs. Candidates for deletion or archiving on the next docs cleanup.

## Audit log

| Date | Change |
|:--|:--|
| 2026-09-16 | Initial classification: 51 folders `DONE.md`, 18 folders `TODO.md`; stale status lines fixed in turbosound-fm and kempston-mouse READMEs; banner added to 2026-08-26 action-plan. Same pass verified tape-manager P2/P4/P5/P6 as landed (item #15 rescoped to CSW + polish) and TSFM/kempston-mouse/fdc-idle as DONE |
| 2026-09-16 | Flux bridge (KryoFlux/Greaseweazle direct integration) added as **T2 #11** with full design in `2026-09-02-universal-track-model/flux-bridge.md` (WinUAE FloppyBridge prior-art analysis, GPL licence boundary, phases B0–B5). Old T4 "HFE+SCP loaders" retired — implemented in `a637bfa7`, only `LoadDisk`/`SaveDisk` dispatch wiring remains (folded into #11 phase B0). T3/T4 renumbered accordingly |
| 2026-09-16 | Fast disk loading ($3D13 TR-DOS service trap) added as **T2 #13** with r0 design in `2026-09-16-fast-disk-loading/design.md` (entry-table bytes verified across trdos/trdos503/trdos504t; 5.04TM/6.x decline patterns confirmed; service ABI from the 2026-01-21 forensics). Fast-tape folder reopened via TODO.md — non-standard-loader ERR_NR false-stop defect added as **T1 #5**. Rows renumbered; folder TODO cross-references updated in the same change |
| 2026-09-16 | DeZog manual E2E (user-verified): VS Code extension session fully functional incl. backward debugging — T3 #17 rescoped to the user guide only (TODO.md updated) |
