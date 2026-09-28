# Cumulative Plan — All Unfinished Work

**Created:** 2026-09-16, from a full audit of `docs/inprogress/` cross-checked against code and git.
**Last re-audit:** 2026-09-27 (every row re-verified against code and git at `47c40db2` + working tree;
all folders classified — see the audit log).
**Maintenance rule:** this is the single priority-ordered view across every folder marked `TODO.md`.
When an item lands (or priorities shift), update the folder's `TODO.md` and this file in the same
change. Folders marked `DONE.md` are finished and deliberately not listed here.

**Priorities:** **T1** = do next (high value, small effort) · **T2** = high value, medium-large
effort · **T3** = cheap enablers / opportunistic · **T4** = deferred with rationale (revisit when
a trigger fires).

**Numbering:** row numbers are stable IDs referenced from folder `TODO.md` files — they are never
reused or renumbered. Rows are listed in priority order within each tier; new rows take the next
free number. Finished rows move to [Retired rows](#retired-rows).

---

## Sequencing rationale (2026-09-27)

A lot of new scope arrived in one week: TSConf, ZX-Poly, NeoGS + GS debugger, video debug
translation, TTD v2, the core perf review, devtools/roadmap visions. Ordered by these rules:

1. **Defects in shipped features first.** A critical thread race in the GS control endpoint and a
   deterministic tape derail are both small, well-understood fixes (#44, #5).
2. **Finish what is in flight before starting new things.** The beam refactor for #42 landed
   (`a0a12def`, `7ed8f2a9`); closing #42a also closes #3.
3. **Foundations before new machines.** Every new device with its own RAM or video mode adds work
   to the TTD migration (#40) and the video mappers (#42). Do the TTD v2 **V0 + V1 memory
   regions** and the **#42 mapper interface** before TSConf (#41), ZX-Poly (#43) and NeoGS
   (#45), so these machines plug into the new model instead of adding to the migration work.
4. **The expression evaluator (#6) is the most-wanted shared piece.** Conditional breakpoints,
   `run_until_condition`, DeZog fast conditions, the trigger engine (roadmap), the devtools
   program (#51) and the NedoOS struct DSL all consume it.
5. **Only one new-machine program at a time.** Pick between TSConf (#41: ancestor code
   half-ported, biggest user base) and ZX-Poly (#43: 1-week spike as a cheap decision gate).
   Recommendation: run the ZX-Poly spike opportunistically, commit to TSConf as the next
   machine program.

---

## T1 — do next (high value ÷ effort)

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 44 | **GS control-path bugs**: **BUG-1 (critical)** `POST /control/audio/gs` `send_command`/`send_data` call `gs->sendCommand()` → `flush()` on the drogon worker thread (`state_audio_api.cpp:750-756`, `soundchip_gs.cpp` send path), stepping the GS Z80 concurrently with the emulation thread; **BUG-2** `debug_api.cpp` register write uses `asUInt()` — a hex-string value is silently written as 0 | [2026-09-19-general-sound](2026-09-19-general-sound/) → [verification-findings-and-bugs.md](2026-09-19-general-sound/verification-findings-and-bugs.md) | Re-verified open in code 2026-09-27. Fix = marshal to the emulation thread (same pattern the MessageCenter/feature changes use). Also a prerequisite for the GS debugger (#45): same class of cross-thread hazard |
| 5 | **Non-standard loader tapes**: four tape-engine defects: **B1** ERR_NR watchdog false-stops on custom-loader writes to `$5C3A`; **B2** a stop consumes the in-flight block; **B3** the deck does not wait for a busy loader between blocks (fast loading off); **B4** ROM restart after a freeze replays the frozen block. **B6** keyboard reads keep the tape rolling through key prompts and music. Fix plan P1–P4: remove the ERR_NR stop, no consume, correct restart after freeze, the tape moves only while a loader listens (reads classified EAR/KEY/OTHER; park at the next pilot) | [2026-08-30-fast-tape-loading](2026-08-30-fast-tape-loading/) → [nonstandard-loader-investigation.md](2026-08-30-fast-tape-loading/nonstandard-loader-investigation.md), [loader-follow-design.md](2026-08-30-fast-tape-loading/loader-follow-design.md) | Investigated 2026-09-27 with a headless probe (8 tapes × 48K/128K × switch matrix) and a 16-emulator prior-art survey (none uses ERR_NR). B1+B2 proven causal for EMELYANOV, B3 for SAN-SAN. Several 2026-09-16 "failures" were key-wait prompts. Open, unexplained: KID__DR crash on Pentagon, TIMOFEY on 48K, HACKER_SHURIK on Pentagon with fast loading off. P1 done 2026-09-27. P2–P3 small, P4 medium. Requirement: a load paused for a key prompt or music resumes by itself when EAR polling returns |
| 42a | **Close P1-1**: `/state/screen` still returns the literal `"standard"` (`state_screen_api.cpp:82`) — `/state/screen/mode` is already mode-aware. The beam refactor landed: `Screen::DescribeBeam()` behind WebAPI/CLI/Lua/Python, `M_PMC` layout (`a0a12def`, `7ed8f2a9`) | [2026-09-27-video-debug-translation](2026-09-27-video-debug-translation/) (first slice of #42); [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (B-1) | Absorbs row #3 |
| 1 | **TTD docs truth pass** (G-8/G-9): `command-interface.md` §8, `webapi-interface.md`, `lua/python-interface.md` describe a partly imaginary TTD API; `docs/features/mcp/` has no TTD content; invalidation ordering rules buried in one design doc | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (TD-6) | Re-verified unchanged. Partly softened by `.recipe/analysis/ttd-*.md`, but the reference docs still mislead. Docs-only |
| 2 | **Finish MCP first-class TTD** (G-1/TD-1): `time_travel` has status, bookmarks and coverage probe/scan/summary; missing start/stop/seek/step/reverse/find-last/dump/load, a `ttd` aspect in `inspect_state`, briefing + `docs/features/mcp/` section | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (G-1) | Thin `ForwardCall` wrappers (`mcp-tools.cpp:1719+`). Do together with #1 |
| 40-V0 | **TTD v2 step V0 remainder — make v1 honest**: verify/fix the ZX-Evo page-255 sentinel, stale `_prevPageCache` after resume (B1), stale journal after load (B2), F3 | [2026-09-25-ttd-v2-migration](2026-09-25-ttd-v2-migration/) → [migration-trajectory.md](2026-09-25-ttd-v2-migration/migration-trajectory.md) | Already done: the `PeripheralId` table (unique ids 8–12), Scorpion `#1FFD` (`2013b47b`), CRC compare (`2d132fc2`), exact restore for every device (`8db7841f`). Correctness of a shipped feature; small |

## T2 — high value, medium-large effort

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 6 | **Expression evaluator + conditional breakpoints** (slices 1d/1e first): `bpcondition` engine, wiring into `Handle*`, hit counters (1f), ranges (1c), IRQ breakpoints, frontends (1h) | [2026-08-17-conditional-breakpoints](2026-08-17-conditional-breakpoints/); consumes [2026-08-26-expression-evaluator](2026-08-26-expression-evaluator/) and [2026-08-26-breakpoint-enhancements](2026-08-26-breakpoint-enhancements/) | Zero code yet (only `BreakpointRangeDescription` exists). The only remaining **P0** in the feature-parity matrix; the most-reused piece (see rationale 4). Phase 0 speed substrate landed (12–18× miss-path) with benchmarks as regression gates |
| 40 | **TTD v2 migration, V1–V5**: V1 memory regions (device RAM in the page store, per-piece chain cap instead of whole-RAM key frames: GS RAM, MoonSound wave SRAM, future NeoGS/TSConf/ZX-Poly), V2 device table, V3 determinism inputs in the file, V4 memory budget, V5 chunked checksummed versioned container + disk mode | [2026-09-25-ttd-v2-migration](2026-09-25-ttd-v2-migration/) → [migration-trajectory.md](2026-09-25-ttd-v2-migration/migration-trajectory.md) | The "merge profi/generalsound/moonsound" steps are moot (all merged). **V1 should come before #41/#43/#45** (rationale 3). User decisions still open in trajectory §4. Strategy context: [2026-09-21-roadmap](2026-09-21-roadmap/) |
| 42 | **Mode-aware video debug translation** (remainder after #42a): `IVideoMapper` per video family + `VideoMapService` (beam→where, beam→fetch, pixel→memory, memory→pixels) for ZX, AlCo, ATM, Profi HR; pixel inspector; text-mode OCR (the Qt beam widget already follows each mode's window, `a0a12def`) | [2026-09-27-video-debug-translation](2026-09-27-video-debug-translation/) → [design.md](2026-09-27-video-debug-translation/design.md) | Mapper interface before TSConf/ZX-Poly video work so their modes plug in natively. Background: [2026-09-22-atm-hires-border-and-addressing](2026-09-22-atm-hires-border-and-addressing/) |
| 38 | **Core performance and gating review** (~50 findings): follow the review's §I order — quick bit-exact fixes (D1, M4, M5, G4, F1/F3/F8, G2/G3, E2), re-land the lost Aug-04 optimizations, sound-device quiescence (idle GS/MoonSound/TSFM cost as much as active), catch-up sync, then gating hygiene (F2/F6, M7, G6) | [2026-09-24-core-performance](2026-09-24-core-performance/) → [review §I](2026-09-24-core-performance/unreal-ng-core-perf-and-gating-review.md) | E1 committed (`5f965a07`); related `ec7b091a`, `c07d485e` landed. Windows frame pacing (spin-before-deadline, affinity, EcoQoS) from [2026-09-15-frame-budget-triage](2026-09-15-frame-budget-triage/) folded in here. Idle-device cost grows with every sound card added, so do quiescence before NeoGS |
| 41 | **ZX-Evo TSConf machine (`TSCONF`/`TSL`)**: port decoder (creatability), banking, TSU tiles/sprites + VDU modes, DMA, SPI/SD + virtual FAT, Beta-128 vdos, AY/GS/Covox, movable INT + zclk turbo; debugging, TTD, automation parity | [2026-09-27-tsconf](2026-09-27-tsconf/) → [technical-design.md](2026-09-27-tsconf/technical-design.md) | **Recommended next machine program** (rationale 5); start after #40-V1 and the #42 mapper interface. Shares IDE with #13a and SD-SPI with NeoGS (#45): design each once |
| 13a | **ZX Profi 1024 remainder**: IDE (design done: [technical-design.md](2026-09-21-profi/technical-design.md) §14), launching the BIOS menu entries, hi-res timing evidence, RTC in TTD, Kempston joystick | [2026-09-21-profi](2026-09-21-profi/) → [TODO.md](2026-09-21-profi/TODO.md) | Branch merged; Covox/SoundRive (`fea02756`, `add05540`), RTC (`861778c3`), DOS latch (`f4e35ee6`), palette (`44dc5697`) landed. Build the IDE once for Profi + TSConf |
| 45 | **GS remainder + NeoGS + GS debugger**: NeoGS phases 0–1 (design at review round 4, "ready for phase 0", `7167d0e8`), later SD/flash; GS diagnostics-gaps proposal; GS/NeoGS card-CPU debugger (`IDebugTarget`, tight catch-up, firmware profiles) | [2026-09-19-general-sound](2026-09-19-general-sound/) → [neogs-tdd.md](2026-09-19-general-sound/neogs-tdd.md); [2026-09-27-gs-debugger](2026-09-27-gs-debugger/) | Classic GS done. Gated on #44 (thread-safe control path), #40-V1 (GS RAM as a region) and #38 GS quiescence (G1/G4) |
| 43 | **ZX-Poly platform port** (quad-Z80 lockstep Spectrum 128, 16-color no-clash modes 4–7): recommended route [quad-instance-architecture.md](2026-09-27-zxpoly/quad-instance-architecture.md): 4 stock Pentagon instances, load on master, full-state replication at entry point, host input gated to the master and replicated into the slaves at the same T, per-line screen composer, 4 aligned TTD sessions; prototype = tests T4–T6 (2–3 d, go/no-go), core ~6–7 wk in total, then Phase 3 adaptation tooling (~2 wk); Phase 4 Spec256 deferred | [2026-09-27-zxpoly](2026-09-27-zxpoly/) → [quad-instance-architecture.md](2026-09-27-zxpoly/quad-instance-architecture.md) | Design complete, awaiting the go-ahead for the prototype. The prototype runs in a worktree as core tests with no shared-code changes, and can run any time; hold Phases 1+ until TSConf lands (one machine program at a time). Spec256 deep-dive now exists: [2026-09-27-spec256](2026-09-27-spec256/) |
| 54 | **Spec256 256-colour mode support** (Z80_GFX lockstep shadow-execution, not a port mode): Phase R render-only (`SPEC256` model + ZIP/loader + codec + palette + view + capture, ~1–2 wk) → E1 write-mirror (~1–2 wk) → E2 lockstep cores (gated: ride the ZX-Poly scheduler if #43 lands) + TTD shadow-RAM region (needs #40-V1) | [2026-09-27-spec256](2026-09-27-spec256/) → [unreal-ng-integration-analysis.md](2026-09-27-spec256/unreal-ng-integration-analysis.md) | Research complete. R+E1 is self-contained and makes most of the ~47-title catalog viewable/playable; E2 deliberately deferred pending the #43 Track A/B decision. Two byte-order contradictions must be fixture-verified on day one |
| 12 | **Flux bridge — KryoFlux/Greaseweazle**: **B0** wire the shipped HFE/SCP loaders (`a637bfa7`) into `LoadDisk`/`SaveDisk` dispatch; **B1** KryoFlux stream import; **B2–B4** live Greaseweazle bridge; automation parity | [2026-09-02-universal-track-model](2026-09-02-universal-track-model/) → [flux-bridge.md](2026-09-02-universal-track-model/flux-bridge.md) | B0 is small and can be pulled into T3 on its own. B2+ is hardware-dependent, so do it when hardware is on the desk |
| 7 | **TD-5 TTD timeline summary**: `GET /ttd/timeline` with O(limit) downsampling | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) → `designs/ttd-timeline-summary-design.md` (G-2) | Design complete & reviewed; pairs with bookmarks + coverage (TD-7 landed `45acfca3`). Also feeds the Qt TTD scrubber |
| 4 | **TD-3 Phase 2 — `POST /memory/dump` + `TempFileTracker`** | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (G-6) | Unchanged; memory is the only RE extraction path without server-side file output |
| 9 | **P2-3 capabilities/settings introspection**: read-only `GET /capabilities` | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (A-4) | Unchanged. More valuable now that there are 4 sound cards and 9 creatable models |

## T3 — cheap enablers, opportunistic

| # | Item | Tracked in | Notes |
|:--|:--|:--|:--|
| 52 | **Unmerged branch audit**: decide merge/rebase/abandon for `frame-diagnostics` (5 commits: diagnostics, benchmarks, decimator perf), `ios-client` (13 commits incl. `core/embed/`), `disasm-table` (59 ahead), `visualizations` (36), `uns-snapshots` (28), `automation-batch-mode` (17, 881 behind), `crt-effects` (6) | (git) | Every week of drift raises the merge cost; a half-day triage decides which to rescue. `frame-diagnostics` feeds #38; `ios-client` is #47 |
| 8 | **P1-4 porttrace decode rules**: Profi session name landed; ATM710/ATM3/Pentagon1024 still report "Unknown", `getPortTraceDecodeRules` overridden only for Pentagon 128 | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (C-2) | Decoders are all on master now, so this is small. ATM450 has no decoder; TSL comes with #41 |
| 46 | **Debugger parity with Xpeccy**, Phase 1 navigation first (address history, marked addresses), then flags/IFF, stack, port watch | [2026-08-26-debugger-enhancements](2026-08-26-debugger-enhancements/) | Phase 1 is S–M and used every day in the Qt debugger |
| 14 | **P3-1 per-machine MCP resources**: `unreal://machine/atm-turbo`, `zx-evo` (profi done); later `pentagon1024`, `moonsound`, `tsconf` | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (F-1) | Static markdown; ATM is on master now, so write it from the atm-baseconf docs |
| 11 | **MoonSound remainder**: P2-2 automation (`DeviceState` report + control/inspection endpoints); D2 hardware check (HoldDrop reducer) | [2026-09-13-moonsound](2026-09-13-moonsound/) → [TODO.md](2026-09-13-moonsound/TODO.md) | Device, libopl4, TTD and tests landed. Do with #20 as one DeviceState pass |
| 20 | **P2-4 GS/Covox DeviceState reports** | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (D-3) | `devicestate.cpp` covers AY/TSFM/WD1793 only. Pair with #11 |
| 15 | **Triage recipes remainder**: tape-load ordering rule (G-7), optional `machine_selftest` | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (F-2) | Recipes half delivered by `.recipe/` (committed) |
| 34 | **Recipe library growth**: symbols/debugger + capture/recording recipes; keep machine/peripheral recipes in sync as #41/#45 land | [2026-09-22-agent-recipe-library](2026-09-22-agent-recipe-library/) | Content committed (31 files); growth only |
| 16 | **P3-2 remainder — ROM signature catalog growth + `rom` aspect CLI/Lua/Python parity** | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) (E-2) | |
| 19 | **Python automation build in CI**: `ENABLE_PYTHON_AUTOMATION=OFF` everywhere (`release.yml`, `cmake-ci.yml`, docker/windows) | (build infra) | Removes the recurring "not compile-verified" caveat on every Python surface change |
| 13 | **Fast disk loading v2 — `$3D13` service trap** | [2026-09-16-fast-disk-loading](2026-09-16-fast-disk-loading/) → [design.md](2026-09-16-fast-disk-loading/design.md) | r2 (FDC-integrated `INI` trap + timing compression) landed `ba04e597`; loading is already fast, so v2 is a marginal gain. Downgraded from T2 |
| 18 | **Tape manager P3 (CSW) + P7 polish** | [2026-09-01-tape-manager](2026-09-01-tape-manager/) | No `loader_csw` yet (only TZX block $18) |
| 17 | **DeZog user guide** | [2026-08-27-dezog-integration](2026-08-27-dezog-integration/) | Final close-out item |
| 39 | **AY voicing listening pass**: listening profiles, A/B vs `d90421bb`, punch-with-flat decision (R1) | [2026-09-25-ay-tone-voicing](2026-09-25-ay-tone-voicing/) | Implemented and committed `13f4bf16`. User listening, no code |
| 37 | **TSFM TTD listening verification** | [2026-09-10-turbosound-fm](2026-09-10-turbosound-fm/) → [ttd-fm-state-gap.md](2026-09-10-turbosound-fm/ttd-fm-state-gap.md) | TSFM v4 blob with timed writes + exact-restore corpus landed (`6ed6d4c0`), AY restores exactly too. Only a listening pass across a scrub is left; do it with #39 |
| 47 | **`ios-client` branch landing (`core/embed` C API + iOS host)** | [2026-09-19-ios-integration](2026-09-19-ios-integration/) | Media upload already on master (`3250b1af`). The embed layer is the prerequisite for #50; decide via #52 |
| 53 | **ATM verification gaps** (six small items: ATM710 `#FF` readback, ATM3 font RAM upload, plain-ZX palette routing, …) + Pentagon 1024 SZX `EFF7` chunk | [2026-09-15-atm-baseconf-highres-ports](2026-09-15-atm-baseconf-highres-ports/) → [verification-gaps-and-tests.md](2026-09-15-atm-baseconf-highres-ports/verification-gaps-and-tests.md); [2026-09-18-pentagon-1024-16color-mode](2026-09-18-pentagon-1024-16color-mode/) | Each is S; only matters for rare software |

## T4 — deferred (revisit on trigger)

| # | Item | Tracked in | Trigger / rationale |
|:--|:--|:--|:--|
| 22 | Schema-driven generation of CLI/WebAPI/Lua/Python/docs from one command schema | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Manual parity cost keeps growing (#42a just touched 4 surfaces for one feature). Revisit before the next large automation surface (TSConf SD media API) |
| 23 | WebSocket event push, `subscribe`, Lua/Python callbacks | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Prerequisite for #51 (devtools); polling sufficient until then |
| 51 | **Developer toolchain program**: `unreal-devd` daemon, `libunreal-debuginfo`, the debug protocol VS Code uses, language server, VS Code extension, trigger engine; NedoOS-aware debugging + struct DSL | [2026-09-21-devtools-roadmap](2026-09-21-devtools-roadmap/), [2026-09-17-nedoos-future-support](2026-09-17-nedoos-future-support/), [2026-09-21-roadmap](2026-09-21-roadmap/) | Design only, XL. Trigger = #6 (expressions) and #23 (event push) landed |
| 48 | Sound debug panel: oscilloscope + audio-quality analysis taps | [2026-09-16-sound-oscilloscope](2026-09-16-sound-oscilloscope/) | Draft design, goals not yet signed off. Revisit with #45 diagnostics |
| 49 | TUI debugger front-end (Unreal Speccy / TSConf text-mode debugger layouts, FTXUI) | [2026-09-24-tui-debugger](2026-09-24-tui-debugger/) | Spec + POC done (`815d1797`, `tools/poc/018-tui-debuggers`); Qt debugger covers the need |
| 50 | Unreal Engine 4/5 integration (emulators as in-world objects) | [2026-09-19-unrealengine-integration](2026-09-19-unrealengine-integration/) | Vision; blocked on the embed layer (#47) |
| 25 | TD-8 code half: `covered_from` window reporting (G-10) | [2026-09-14-automation-triage-gaps](2026-09-14-automation-triage-gaps/) | Fold into next TTD API touch (#2) |
| 26 | `get_framebuffer` raw, Python numpy wrapper | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | Digest/OCR/screenshot cover demand |
| 27 | Headless deterministic mode + RZX input record/playback | [2026-08-26-automation-gaps](2026-08-26-automation-gaps/) | TTD journals cover replay; RZX interop is the remaining value. Relates to #40-V3 |
| 28 | Realtime monitoring / segmentation widget | [2026-02-23-realtime-monitoring](2026-02-23-realtime-monitoring/) | Analysis + widget design done; no code |
| 29 | Interrupt analyzer, routine classifiers, beam-to-execution correlation | [2026-01-14-analyzers](2026-01-14-analyzers/) | Beam correlation becomes cheap once #42 lands |
| 30 | HUD Phase 4: detach `hud/core` | [2026-09-07-hud-layer](2026-09-07-hud-layer/) | Trigger = a second HUD client |
| 31 | Shared-memory coherency / high-performance bridge | [2026-08-27-shared-memory-coherency](2026-08-27-shared-memory-coherency/) | Only if HTTP loopback is proven a bottleneck |
| 32 | MP4/WebM recording, semantic frame diffing, FFT audio, symbol extras | [2026-08-17-mcp](2026-08-17-mcp/) (Phase 3) | Explicitly deferred |
| 33 | DiskManager, save modes, heatmaps | [2026-01-24-diskimage-modernization](2026-01-24-diskimage-modernization/) | UI/UX extras |

## Retired rows

| # | Item | Outcome |
|:--|:--|:--|
| 3 | P1-1 mode-aware screen state | `/state/screen/mode` mode-aware (`0194a50f`, `a42b4bdf`); the `/state/screen` remainder is merged into #42a |
| 10 | ATM/ZX-Evo branch merge | Merged `59e37f38`/`4ab0ce18` (2026-09-17); ATM710/ATM3 creatable; `DrawATM16` fully implemented (`screen.cpp:1556`, `ScreenAtm`); 50 Hz raster `47c40db2`. Leftovers → #53 (gaps), #41 (TSL), #40 (TTD registry) |
| 21 | Pentagon 1024 16-color mode | Landed `0194a50f` (decoder, `M_P16`, state reporting, TTD, 20 tests); SZX chunk → #53 |
| 24 | TD-7 coverage-index query | Landed `45acfca3` on all five surfaces |
| 35 | UDI weak-bit storage + FSE (VORON1) | Landed `27cb6940` |
| 36 | Qt TTD toolbar toggle + control widget | Landed `b4afd9a3` |

## Documentation debt

- Stale folder notes found by the 2026-09-27 audit: `2026-09-24-core-performance/TODO.md`
  (E1 "not committed") and `2026-09-25-ttd-v2-migration/TODO.md` ("nothing implemented") are
  fixed in the same pass; still open: `2026-09-16-fast-disk-loading/walkthrough.md` (describes r1, which r2 replaced),
  `2026-09-24-tui-debugger/__pycache__/` (stray).
- [2026-08-26-automation-gaps/feature-parity.md](2026-08-26-automation-gaps/feature-parity.md) — stale rows: TTD now full parity, symbolic disasm, screenshots, beam; only conditionals/hit-count/actions/BP-groups/`run_until_condition` rows still real.
- [2026-08-26-automation-gaps/action-plan.md](2026-08-26-automation-gaps/action-plan.md) — summary says Phase 1B/1C at 0; DeZog landed, MCP superseded-complete. Banner added 2026-09-16.
- [2026-08-17-mcp/2026-09-10-automation-reconciliation.md](2026-08-17-mcp/2026-09-10-automation-reconciliation.md) — TTD table predates the full TTD surface.
- Root-level legacy files (`ANALYSIS_SUMMARY.md`, `COMPREHENSIVE_ANALYSIS.md`, `cpu-optimization-*.md`, `implementation_guide.md`, `memory_management.md`, `ports_architecture_proposal.md`, `risk_assessment.md`, `sound_buffer_analysis.md`, `testing_strategy.md`, `architecture_overview.md`) — pre-date the folder convention; candidates for deletion or archiving.

## Audit log

| Date | Change |
|:--|:--|
| 2026-09-16 | Initial classification: 51 folders `DONE.md`, 18 folders `TODO.md`; stale status lines fixed in turbosound-fm and kempston-mouse READMEs; banner added to 2026-08-26 action-plan. Same pass verified tape-manager P2/P4/P5/P6 as landed (item #15 rescoped to CSW + polish) and TSFM/kempston-mouse/fdc-idle as DONE |
| 2026-09-16 | Flux bridge added as **T2 #11** (now #12) with full design in `2026-09-02-universal-track-model/flux-bridge.md`. Old T4 "HFE+SCP loaders" retired (`a637bfa7`) |
| 2026-09-16 | Fast disk loading added as **T2 #13**; fast-tape folder reopened via TODO.md — non-standard-loader ERR_NR false-stop added as **T1 #5** |
| 2026-09-16 | DeZog manual E2E user-verified incl. backward debugging — #17 rescoped to the user guide only |
| 2026-09-18 | Pentagon 1024 16-color screen mode design created (#21) |
| 2026-09-21 | Profi 1024 added as **T2 #13a**; `unreal://machine/profi` landed (part of #14) |
| 2026-09-22 | Agent recipe library published (`.recipe/`), added as **T3 #34** |
| 2026-09-22 | UDI weak-bit storage + FSE end-to-end design created, added as **T2 #35** |
| 2026-09-23 | TTD Qt toolbar toggle & control widget design created, added as **T2 #36** |
| 2026-09-25 | TTD v1 → v2 migration planned, added as **T2 #40** |
| 2026-09-27 | ZX-Evo TSConf machine design created, added as **T2 #41**; video debug translation design added as **T2 #42**; ZX-Poly port analysis added (briefly a duplicate `#40`, renumbered **#43** the same day) |
| 2026-09-27 | Spec256 deep research + integration analysis created, added as **T4 #54** (R+E1 self-contained; E2 gated on the #43 Track A/B decision); #43 Phase 4 now cross-links it |
| 2026-09-27 | **Full re-audit** of every row against code/git (`47c40db2` + working tree) and of all unmarked folders. **Retired** #3, #10, #21, #24, #35, #36. **New rows:** #44 GS control-path bugs (T1), #42a in-flight beam refactor (T1), #40-V0 TTD v1 honesty remainder (T1), #45 GS/NeoGS/GS debugger, #46 debugger parity, #47 ios-client landing, #48 sound oscilloscope, #49 TUI debugger, #50 UE integration, #51 devtools program, #52 unmerged-branch audit, #53 ATM/P1024 small gaps. **Re-tiered:** #13 T2→T3 (r2 landed), #11 T2→T3 (only automation left), #8 T2→T3 (decoders on master). Rows now in priority order within each tier; sequencing rationale added. Folders classified: DONE — trdos-autostart, atm-baseconf-highres-ports, atm-hires-border-and-addressing, 2026-08-17-mcp, ttd-qt-toolbar-widget, udi-weak-bit-storage, pentagon-1024-16color-mode, atm-debugging; TODO — frame-budget-triage, fast-disk-loading, sound-oscilloscope, nedoos-future-support, ios-integration, unrealengine-integration, roadmap, tui-debugger, gs-debugger |
| 2026-09-27 | #5 investigated: [nonstandard-loader-investigation.md](2026-08-30-fast-tape-loading/nonstandard-loader-investigation.md) (headless probe matrix, prior art from Fuse/SkoolKit/xpeccy-plus/BizHawk/ZXMAK2/Spectral and others). Row rewritten from one hypothesised cause to four confirmed/identified defects (B1–B4) plus three open items; fix plan P1–P4 |
