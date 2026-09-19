# Gap Analysis — Triage Surface for New Machines & Peripherals

**Verified:** master working tree, 2026-09-14. All citations read in full or
via targeted search during the analysis session. Findings are grouped by
triage workflow stage (A→F) and numbered for cross-reference from
[recommendations.md](recommendations.md).

Cross-reference to the 2026-09-10 ATM session
(`docs/inprogress/2026-09-10-atm-debugging/bug-report.md`):

| Bug-report item | Status on master 2026-09-14 | Covered by |
|:--|:--|:--|
| #1 Screen capture ignores ATM dimensions | Partially fixed — `screencapture.cpp:67-76` now reads `rasterDescriptors[fb.videoMode]` | B-2 |
| #2 Memory bank reporting `65535` for ATM | **Open** — `/state/memory` still 7FFD-centric | E-1 |
| #3 State/screen doesn't report ATM modes | **Open** — `video_mode` still literal `"standard"` | B-1 |
| #4 TTD seek doesn't restore framebuffer | Appears addressed — `timetravelmanager.cpp:1188-1202` re-renders after seek (border sync, `InitFrame`, `RenderOnlyMainScreen`) | — |
| #5 ATM16 rendering black screen | **Open on master by design** — `DrawATM16` stub; rendering work lives on `atm` branch | B-3 |
| #6 Model name reporting | Fixed — `Config::GetModelFullName` used in state APIs | — |

---

## A. Machine identity — the agent doesn't know what it's talking to

### A-1. `GET /emulator/{id}` reports no model

> ✅ **DONE (2026-09-14, commit `34546478`, P0-1):** `getEmulator` and the list
> response now report `model`, `model_full_name`, `ram_kb`, `video_mode`,
> `speed_multiplier`, `config_folder`; the MCP `machine` aspect picks them up.
>
> ✅ **Follow-up (2026-09-15):** the `video_mode` value itself was fixed at the
> source — `Screen::GetVideoModeName` had no cases for `M_ZX128` /
> `M_PENTAGON128K` / `M_SCORPION`, so those models answered `"Unknown"`
> since P0-1 landed. Regression tests: `Screen_VideoModeName_Test`.

`getEmulator` returns exactly `id, state, is_running, is_paused, is_debug`
([lifecycle_api.cpp:263-268](../../../core/automation/webapi/src/api/lifecycle_api.cpp)).
No `model`, no RAM size, no config folder, no video mode, no speed multiplier.

The MCP `machine` aspect is a verbatim pass-through of this endpoint
([mcp-tools.cpp:659-666](../../../core/automation/mcp/src/mcp-tools.cpp)), so
`inspect_state aspects=["machine"]` — the intended one-call triage snapshot —
cannot answer "is this ATM710 with 512K or a 48K?". Model identity is instead
scattered: `/state/screen` and `/video/beam` carry `model`, `/state/memory`
implies it, `/emulator/models` only lists possibilities.

**Impact:** every multi-model triage session starts with a guess; results are
attributed to the wrong machine class. This also breaks the "kempston mouse on
different configs" scenario at step one — the config is invisible.

### A-2. Silent 48K fallback on create

> ✅ **DONE (2026-09-14, commit `34546478`, P0-2):** create/switch now fails
> loudly with 400 + the exact manager reason instead of substituting a 48K
> machine; success echoes the resolved model.

`createEmulator` falls back to `manager->CreateEmulator(symbolicId)` (default
model) whenever `CreateEmulatorWithModel[AndRAM]` returns nullptr — i.e. for
unknown model names, invalid RAM sizes, **or models that cannot initialize**
([lifecycle_api.cpp:180-204](../../../core/automation/webapi/src/api/lifecycle_api.cpp)).
The 201 response contains only `id, state, symbolic_id`
(lifecycle_api.cpp:219-223) — no resolved model, no warning.

On master, `ATM710`/`ATM3`/`ATM450`/`TSL` requests **always** take the
fallback path: `Emulator::Init` fails because the port decoder factory throws
([portdecoder.cpp:87-96](../../../core/src/emulator/ports/portdecoder.cpp))
and the derived config folders `data/configs/atm710` etc. do not exist
(config.cpp:562-567). The agent receives `201` and believes it is testing an
ATM machine while probing a 48K.

`EmulatorManager::CreateEmulatorWithModelAndRAM` already knows the exact
failure reason (unknown model vs. RAM not supported vs. Init failure —
emulatormanager.cpp:242-255) but only logs it; the reason never reaches the
HTTP response.

**Impact:** misattributed triage results; wasted sessions debugging "ATM
behavior" that is actually 48K behavior. This is the single most dangerous
gap because it fails silently.

### A-3. Stale model list in agent-facing docs

> ✅ **DONE (2026-09-14, commit `34546478`, P0-3):** `AGENTS.md` now lists the
> 16 authoritative short names + a runtime-authoritative note pointing at
> `GET /emulator/models`.

`AGENTS.md` advertises models `PENTAGON, 48K, 128k, PLUS2, PLUS2A, PLUS3,
SCORPION, ATM1, ATM2, ATM3, PROFI`. The authoritative table
([config.h:45-63](../../../core/src/emulator/config.h)) has no `PLUS2` /
`PLUS2A` entries at all, and the ATM short names are `ATM450` / `ATM710` /
`ATM3` — not `ATM1` / `ATM2`. Combined with A-2, an agent following the docs
requests `ATM2`, gets a silent 48K, and files bugs against the wrong machine.

**Impact:** direct misdirection of every doc-following agent; also poisons
regression comparisons that assume the doc list is real.

### A-4. No peripheral / capability introspection

`GET /settings` exposes only `io_acceleration` (fast_tape, turbo_tape,
fast_disk) and `disk_interface` (trdos_present, trdos_traps)
([settings_api.cpp:58-76](../../../core/automation/webapi/src/api/settings_api.cpp)).
Nothing reports:

- mouse fitment: `[INPUT] Mouse=`, `[INPUT] Wheel=` (the mouse status endpoint
  exposes `present`/`wheel_enabled` derived from them, but nothing can read or
  change the underlying fitment);
- AY chip variant / TSFM mode, covox variant, SAA1099;
- sound mixer volumes (incl. future `MoonSoundVol`), `MoonSound` enable;
- speed multiplier (only visible as `frequency_multiplier` inside
  `/video/beam`'s `frame_timing` block);
- attached peripherals list with their ports.

**Impact:** "peripheral misbehaves" triage cannot distinguish *not fitted*
from *fitted and broken* — the most common first fork in peripheral triage —
without host-side config file access that agents don't have.

---

## B. Video-mode triage (ATM modes, Profi, TSConf)

### B-1. Screen state endpoints hardcode 48K/128K semantics

- `getStateScreen` computes `is_128K` from `MM_SPECTRUM128 || MM_PENTAGON ||
  MM_PLUS3` only — ATM3/710/450, PROFI, TSL, SCORP all fall into the "48K"
  branch with hardcoded `active_ram_page: 5`; `display_mode` is the literal
  string `"standard"` ([state_screen_api.cpp:73-94](../../../core/automation/webapi/src/api/state_screen_api.cpp)).
- `getStateScreenMode` returns `video_mode: "standard"`, `resolution:
  "256×192"` unconditionally; the 128K branch repeats the same three-model
  check (state_screen_api.cpp:210-234).

The core has the truth: `Screen::GetVideoMode()` returns the full
`VideoModeEnum` including `M_ATM16/M_ATMHR/M_ATMTX/M_ATMTL/M_PROFI/M_TS16/
M_TS256/M_TSTX`, `Screen::GetVideoModeName()` renders names ("ATM16", …,
screen.cpp:940-996, 1329-1345), and `/video/beam` already reports
`video_mode` + full raster geometry correctly. Bug-report #3 called this
inconsistency out; it is still unfixed.

**Impact:** the screen endpoints actively mislead during the exact scenario
they'd be used for ("why is my ATM screen black" — endpoints say everything
is standard/normal). OCR/screenshot must be correlated with `/video/beam`
manually to learn the mode.

### B-2. Mode-selecting paging registers are invisible

ATM mode selection lives in `pFF77` bits (ATM710/ATM3) and `aFE` bits 5-6
(ATM450); Profi uses `pDFFD`; Scorpion shadow/monitor uses `p1FFD`; ATM3
additionally consults `pEFF7`. All are plain fields in `EmulatorState`
([platform.h:899-922](../../../core/src/emulator/platform.h)) and are already
checkpointed by TTD (`machinestatehash.cpp:105`, `ttdcheckpoint.cpp:148`) —
but **no WebAPI endpoint reports them**. `/state/memory` reports `p7FFD`
derived bits only (see E-1).

**Impact:** for "ports, screen modes" triage the agent cannot see the
machine's paging/mode latches without reading guest RAM or running porttrace
backwards. The natural first question — "what did the software write to
#FF77 and is the decode honoring it" — has no read surface.

### B-3. Rendering stubs make mode observation the only triage signal (master)

On master every extended-mode renderer is a no-op (`DrawATM16` et al.,
screen.cpp:1231-1298). That is `atm`-branch work, not an automation gap per
se — but it defines the triage contract: until rendering lands, agents triage
ATM video **through state, not pixels** — which makes B-1/B-2 blockers for
the workflow. Screen capture dimensions were already fixed to follow the mode
(screencapture.cpp:67-76), so capture of a stub-rendered frame is
consistently black — a fact the API doesn't distinguish from "mode not
active".

**Impact:** without B-1/B-2 fixed, an agent on master cannot distinguish
"mode active but renderer stubbed" from "mode never activated by software" —
the two competing hypotheses of bug-report #5.

### B-4. Screen digest hardcodes pages 5/7; no active-surface mode

> ✅ **DONE (2026-09-14, P1-3):** `/state/screen/digest?mode=active` now
> derives the bank list from the current video mode via
> `Screen::GetActiveSurfaceRAMPages` (ATM hardware modes hash the 7FFD-selected
> bit-plane pair `{videoPage-4, videoPage}`; ZX modes keep pages 5/7); the
> response carries an `active_surface` block and explicit `banks=`/`start,end=`
> still override.
>
> ✅ **Parity (2026-09-15):** CLI `digest --active`, Lua
> `screen_digest(nil,nil,nil,"active")`, Python `screen_digest(mode="active")`.

`/state/screen/digest` defaults to RAM pages `[5, 7]` for 128K-class models
(again the three-model check) and page 5 otherwise
([state_screen_api.cpp:617-621](../../../core/automation/webapi/src/api/state_screen_api.cpp)).
ATM/TSConf video surfaces are not at fixed pages 5/7 (they follow the paging
window), so the digest — the MCP workhorse for "did the screen change?"
polling (`screen_digest` aspect, `capture_media`) — hashes the wrong memory
on these machines and the `changed` flag loses meaning.

**Impact:** silent false-negatives in change detection on the exact machines
being triaged; regression comparisons across models become meaningless.

---

## C. Port-level triage (ATM ports, Profi, ZX Evo)

### C-1. Master cannot instantiate the machines under triage

> ✅ **DONE for the fingerprint half (2026-09-14, commit `cab13b99`, P0-4):**
> `/emulator/status` (and `emulator_manage server`) now expose version, git
> branch/commit, build type and `models_creatable`. The machine creatability
> itself is `atm`-branch work and stays as described below.

`GetPortDecoderForModel` throws for `MM_TSL, MM_ATM3, MM_ATM710, MM_ATM450,
MM_GMX, MM_KAY, MM_QUORUM, MM_LSY256, MM_PHOENIX`
([portdecoder.cpp:56-97](../../../core/src/emulator/ports/portdecoder.cpp));
`Core::Core` propagates (core.cpp:365-380). Combined with the missing config
folders (A-2), **any triage of ATM/ZX-Evo must happen on an `atm`-branch
build** — and nothing in the API tells the agent which build it is talking
to. There is no version/branch/capability field anywhere in the lifecycle
responses.

**Impact:** agents routinely run against `cmake-build-release` binaries that
may be stale or from another branch, then misattribute branch-specific
failures to the machines. A build fingerprint would close this class of
confusion (see recommendations P0-4).

### C-2. Porttrace decode rules exist only for Pentagon128

`getPortTraceDecodeRules()` is virtual with an empty default
([portdecoder.h:233](../../../core/src/emulator/ports/portdecoder.h)); the
only override is `PortDecoder_Pentagon128`. Without rules, trace events carry
`decodeRuleIndex = kNoTable`, losing the mapping from raw address → decoded
device that makes traces diagnostic. Session info names only 7 models —
everything else reports `modelName: "Unknown"`
([portdecoder.cpp:423-433](../../../core/src/emulator/ports/portdecoder.cpp)).

The event flag set is otherwise excellent (`wasDecoded/hadHandler/
beta128Gated/handledInline/cfTrdosActive/viaLegacyBasePath`,
portdecoder.cpp:392-409) — the Scorpion decoder already demonstrates
gating-attribution (border-latch reattribution, portdecoder_scorpion256.cpp).

**Impact:** on the `atm` branch (where ATM decoders exist) port traces will
be raw address streams without device attribution — precisely the feature
needed to debug "ATM ports don't respond". The new decoders landed without
the decode-rule tables the trace subsystem expects.

### C-3. No static port-map introspection

> ✅ **DONE (2026-09-14, P1-5):** `GET /api/v1/emulator/{id}/ports` returns the
> per-model static map (`PortDecoder::getPortMapEntries`: `port/mask/match/
> device/gate`, fitment-conditional mouse and Beta128 rows, registered-handler
> rows deduped) plus a `live` block (`trdos_active`, `mouse_ports_decoded`,
> `mouse_routing_note`, `shadow_monitor_paged` — Scorpion-only, null elsewhere).
> ATM rows land with the atm-branch decoders (P1-4).
>
> ✅ **Parity (2026-09-15):** CLI `ports` command and Lua/Python `ports_map()`
> expose the same rows + live block from the same source.

There is no endpoint answering "which port ranges does this machine decode,
to which devices, under which gating conditions?" An agent must run guest
code and infer from porttrace output plus hardware knowledge. This is also
the generalized form of the mouse design's open question **Q4**
(`ports_decoded`): whether `#FADF/#FBDF/#FFDF` reads currently route to the
mouse — considering `CF_TRDOS` gating, shadow-monitor paging, and registered
peripherals — is invisible to `/mouse/status`
(`docs/inprogress/2026-09-12-kempston-mouse/automation-interfaces.md` §0.4).

**Impact:** every port-conflict triage (ATM #FF77 vs peripheral mirrors,
Profi #DFFD, ZX-Evo port space) pays a discovery tax in tokens and time, and
relies on the agent's hardware memory being correct.

---

## D. Peripheral state & observability

### D-1. Mouse status lacks routing information (open Q4)

> ✅ **DONE (2026-09-14, P2-1):** `/mouse/status` now carries
> `routing.ports_decoded` + `routing.note` from
> `PortDecoder::GetMouseRoutingState`, which probes the canonical buttons port
> through the model's virtual `IsPort_KempstonMouse` gate (Scorpion TR-DOS
> trigger / Shadow Monitor mirror deviations honored; the Shadow Monitor latch
> alone keeps canonical `#xxDF` — only the five exact Beta low-byte mirrors
> move to the FDC). Design Q4 is closed.
>
> ✅ **Parity (2026-09-15):** CLI `mouse status` `Routing:` line, Lua/Python
> `mouse_status()` `routing` field — status queries only, as on WebAPI.

`MouseStateSnapshot` reports `available` (device exists), `present` (fitted;
otherwise "guest reads floating bus on the mouse ports" warning), `wheel_enabled`,
counters, buttons, wheel, the three port bytes, pending click, TTD journal
support ([mouse_api.cpp:80-116, 407-423](../../../core/automation/webapi/src/api/mouse_api.cpp)).
It does **not** report whether the current machine state actually routes the
mouse ports — TR-DOS session open, decoder gating, or a registered peripheral
claiming the address can all hide the device while `present` stays true.

**Impact:** "kempston mouse on different configs" triage cannot distinguish
*device not fitted* from *fitted but shadowed* — two different bugs with
different fixes. The design doc explicitly left this open.

### D-2. No `mouse` aspect in `inspect_state`

> ✅ **DONE (2026-09-14, P2-1):** `mouse` is in the aspect enum; it fans out to
> `GET /mouse/status` and the summary reports fitment, counters and
> `ports decoded/shadowed` + the gate reason.
>
> ✅ **Parity note (2026-09-15):** CLI/Lua/Python status surfaces now carry the
> same routing answer (see D-1), closing the parity asymmetry for this gap.

The aspect enum is closed (mcp-tools.cpp:571-572): machine, registers,
memory, disasm, stack, breakpoints, memory_banks, screen_ocr, screen_image,
screen_digest, timing, rom, audio_ay, audio_fm, fdc. Mouse state is not
composable into the one-call snapshot; the agent must make a separate
`mouse_input action=status` call and mentally join it.

**Impact:** minor per-call, but it breaks the "single snapshot before/after a
repro step" pattern that makes `inspect_state` effective, and sets the
precedent that new peripherals (MoonSound) also won't get aspects.

### D-3. Audio device state coverage is 2 of N; MoonSound has nothing

- `DeviceState::Fm/FmChip/Fdc` ([state_device_api.cpp:74-107](../../../core/automation/webapi/src/api/state_device_api.cpp))
  are the right pattern: full chip decode (registers, timers, operators,
  envelopes, key-on) with per-chip routes and MCP aspects.
- `state/audio/gs` and `state/audio/covox` return `"not_implemented"`
  placeholders ([state_audio_api.cpp:474-499](../../../core/automation/webapi/src/api/state_audio_api.cpp)).
- MoonSound: no core, no endpoints, no MCP aspect. `AudioSourceType::Moonsound`
  is a placeholder in the recording manager and multitrack dialog. The
  integration TDD (`2026-09-13-0217-opl4-unreal-ng-integration.md`) covers port decoding,
  lifecycle, mixing, gain staging, TTD, UI — and contains **no
  automation/observability section at all** (a grep for
  webapi|mcp|automation matches nothing substantive in the document).

**Impact:** when MoonSound lands as designed, triage will have no way to see
whether the guest is programming the chip (register state, mixer taps, ROM
sample activity) — the same blindness the 2026-09-10 session had for video
modes. Retrofitting observability after device implementation is exactly how
the current A/B/E gaps accumulated.

---

## E. Memory-model triage

### E-1. Bank reporting is 7FFD-centric and wrong for extended models

`/state/memory` reports `ram.bank0..3` via `GetRAMPageForBankN()`,
`paging.ram_bank_3 = p7FFD & 0x07`, and static bank annotations ("Screen 0
location") ([state_memory_api.cpp:75-92, 159-195](../../../core/automation/webapi/src/api/state_memory_api.cpp)).
For ATM-class paging the `GetRAMPageFromAddress` reverse mapping returns
`MEMORY_UNMAPPABLE (65535)` for banks 1-3 — bug-report #2, still open. The
MCP `memory_banks` aspect inherits all of this verbatim.

No endpoint dumps the complete paging state (`p7FFD + p1FFD + pDFFD/pFDFD +
pFF77 + aFE/aFB + pEFF7` + per-bank page/type/source), even though every one
of those fields is a plain `EmulatorState` member that TTD already
checkpoints (B-2 evidence).

**Impact:** memory triage on ATM/Profi/Scorpion produces values that look
like broken mappings but aren't (or hide real ones); agents burn time
chasing reporting artifacts. One unified `/state/paging` endpoint would
collapse this entire class.

### E-2. `rom` aspect dumps bytes but identifies nothing

> **📝 Status update (2026-09-15):** partially overtaken by events. Core
> already carries a SHA-256 → title catalog (`ROM::_signatures`, rom.cpp)
> with cached per-page digests, and WebAPI `/state/memory/rom` reports
> `signature`/`title` per page. What is still missing — headless parity —
> is now designed in
> [port-tags-paging-design.md](port-tags-paging-design.md) §5.2:
> `role`/`name`/`signature` on every `/state/paging` ROM bank row
> (CLI/Lua/Python included), with `ROM::GetROMPageRole` as the single
> layout-table source.

`inspect_state aspects=["rom"]` fetches `/state/memory/rom` — a page dump
(mcp-tools.cpp:795-801). There is no signature matching against known ROM
sets (ATM3/ATM710/ATM450/Profi/TSConf/Scorpion/ProfROM...). "Did the right
ROM even load?" — step one of every boot triage — requires the agent to know
offsets/checksums itself.

**Impact:** boot-loop triage on new machines re-derives ROM identification
per session; wrong-ROM-path bugs (cf. the `StripProfRomQuadrantSuffix` class
of issues, config.cpp:265-267) are found late.

---

## F. Knowledge & workflow layer

### F-1. MCP resources are machine-agnostic

The six resources are: keyboard-layout, basic-reference, z80-isa,
trdos-commands, memory-map (**48K/128K only**), emulator-state. There is no
per-machine resource describing ATM port maps, the #FF77 mode table, Profi
paging, ZX-Evo/TSConf port space, or (future) MoonSound ports/registers.
Agents carry this knowledge in-context or hallucinate it.

**Impact:** every session pays the hardware-knowledge tax again; correctness
of triage depends on the model's priors rather than repo-curated facts.
These are static markdown files — the cheapest gap in this report to close.

### F-2. No triage recipe / machine self-test

The primitives exist (digest, `run_frames`, porttrace, TTD, OCR), but there
is no guided flow ("boot model X → verify ROM signature → run N frames →
digest + OCR → compare against known-good") and no `machine_selftest`-style
tool. The MCP server `instructions` briefing and `docs/features/mcp/` don't
cover machine bring-up triage either.

**Impact:** slower sessions and inconsistent methodology across agents;
nothing encodes what the 2026-09-10 session learned procedurally.

---

## Summary matrix

| # | Gap | Severity for triage | Effort to fix | Status (2026-09-14) |
|:--|:--|:--|:--|:--|
| A-1 | No model in `GET /{id}` / `machine` aspect | High | Small | ✅ Done — P0-1, `34546478`; `video_mode` value fix 2026-09-15 |
| A-2 | Silent 48K fallback on create | **Critical** | Small | ✅ Done — P0-2, `34546478` |
| A-3 | Stale AGENTS.md model list | Medium | Trivial | ✅ Done — P0-3, `34546478` |
| A-4 | No peripheral/capability introspection | High | Medium | Open — P2-3 |
| B-1 | Screen endpoints hardcoded standard | High | Small | Open — P1-1 |
| B-2 | Paging/mode registers invisible | High | Small | Open — P1-2 |
| B-3 | Renderer stubs (master) define state-only triage | Context | atm branch | Open — atm branch |
| B-4 | Digest pages 5/7 hardcoded | Medium | Small | ✅ Done — P1-3 (`mode=active`); parity on CLI/Lua/Python 2026-09-15 |
| C-1 | No build fingerprint; machines non-creatable on master | Medium | Small | ✅ Fingerprint done — P0-4, `cab13b99`; creatability open (atm branch) |
| C-2 | Porttrace rules only Pentagon128 | High (atm branch) | Medium | Open — P1-4 |
| C-3 | No port-map introspection (incl. mouse Q4) | High | Medium | ✅ Done — P1-5 (`GET /ports`); parity `ports`/`ports_map()` 2026-09-15 |
| D-1 | Mouse routing not reported | High (mouse scenario) | Small-Medium | ✅ Done — P2-1 (`routing` field); parity 2026-09-15 |
| D-2 | No `mouse` aspect | Low | Trivial | ✅ Done — P2-1 |
| D-3 | DeviceState 2 of N; MoonSound unplanned | High (future) | Design now | Open — P2-2/P2-4 |
| E-1 | Bank reporting 7FFD-centric | High | Medium | Open — P1-2 |
| E-2 | No ROM identification | Medium | Medium | Open — P3-2 (paging-surface half designed: §5.2) |
| F-1 | No per-machine resources | Medium | Small | Open — P3-1 |
| F-2 | No triage recipe/self-test | Medium | Medium | Open — P3-3 |
