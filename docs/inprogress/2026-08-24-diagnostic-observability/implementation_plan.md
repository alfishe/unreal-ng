# Port Diagnostic Recorder (PDR) Implementation Plan

Based on the research and documentation in `docs/inprogress/2026-08-24-diagnostic-observability/`, this plan outlines the technical steps to implement the low-overhead, structured `PortDiagnosticRecorder` to solve the silent-failure issues with peripheral emulation (such as TurboSound AY drops and BetaDisk gating).

## Document Authority

The four design documents were written iteratively and an earlier draft diverged from the final design. To remove ambiguity:

| Topic | Authoritative document |
|---|---|
| Event struct (`PortTraceEvent`, 24 bytes, full `uint8_t decodeRuleIndex`, `deviceId`) | `use-cases.md` |
| Filtering (two-layer include/exclude with compound rules) | `recording-control.md` |
| Session lifecycle, export formats, transports | `automation-and-export.md` |
| Problem statement, infrastructure audit | `findings.md` (its early `PortDiagnosticEvent` sketch and `watchPort()` filter API are **superseded** — see the superseded notices inside that file) |

## Feature Gate (Runtime, via FeatureManager — NO compile-time flags)

> [!IMPORTANT]
> The PDR is gated **exclusively at runtime** through the existing `FeatureManager` (`core/src/base/featuremanager.h`, `features.ini`). There is **no CMake option and no `#ifdef`** — the code is always compiled in, like `kMemoryTracking`, `kCallTrace`, and `kTimeTravel`. (An earlier draft proposed an `ENABLE_PORTTRACE` compile definition; that is withdrawn.)

- New feature: id `kPortTrace = "porttrace"`, alias `kPortTraceAlias = "pt"`, category `kCategoryDebug`, description "Structured port I/O trace recorder (ring buffer of IN/OUT events)". Persisted in `features.ini` like every other feature, default `off`.
- Follows the established cached-flag pattern (`Memory::UpdateFeatureCache`, `TimeTravelManager::UpdateFeatureCache`): `PortDecoder::UpdateFeatureCache()` caches feature-enabled AND recorder-capturing into a single `bool _portTraceCache` (plain member, written only on feature change / start / stop from the control path, read on the emulator thread).
- Feature OFF → the recorder object is not instantiated (`_portTrace == nullptr`), buffer memory is not allocated. Hot-path cost is one cached-bool test + never-taken, perfectly predicted branch — the same cost class as the existing `ttdProbe.IsArmed()` check that already sits in `OnPortOutComplete`. This is the same "zero-cost when off" contract every other runtime feature in the codebase honors; `DebuggerHotpathBench` verifies it stays unmeasurable.
- Toggling the feature at runtime cascades through the standard `FeatureManager::setFeature` → `onFeatureChanged` → `UpdateFeatureCache` chain; enabling instantiates the recorder lazily, disabling stops capture and releases the buffer.
- The capture site is a single inline helper (no macro, no preprocessor):

```cpp
// In OnPortInComplete / OnPortOutComplete:
if (_portTraceCache) [[unlikely]]
    _portTrace->record(/*isOut=*/..., rawPort, value, pc, disposition);
```

- `kDebugMode` is **not** required — the PDR works in normal run mode; it is deliberately independent of the debug-memory interface.
- Transports always register their commands; when the feature is off they respond "porttrace feature is disabled — enable with `feature porttrace on`".

## User Review Required

> [!IMPORTANT]
> **Hook signature change.** `OnPortInComplete` / `OnPortOutComplete` currently receive only `(rawPort, value, pc)` — the decoded port, decode rule, gate disposition, and handler attribution are locals inside each subclass's `DecodePortIn/Out` (see `portdecoder_pentagon128.cpp:146-194`). The hooks will be extended to receive a `PortDecodeDisposition` struct (below). This touches all 7 model subclasses, but the edits are mechanical (pass the already-computed locals through). This keeps a **single capture point** and is preferred over per-subclass recording calls.

> [!IMPORTANT]
> **Single-event invariant.** Exactly one `PortTraceEvent` is recorded per Z80 I/O operation, at the `OnPort*Complete` hook. Inline handlers (`Port_7FFD_Out`, `Port_BFFD_Out`, …) do **not** push their own events — they only contribute the `wasHandledInline` disposition flag that flows into the single hook-level record. (An earlier draft of this plan said inline blocks should push events directly; that would double-record and is withdrawn.)

> [!TIP]
> **Buffer capacity is configurable** (default 1,048,576 events = 24 MB — unfiltered capture runs at 50k-200k events/s, so small buffers wrap within seconds; ~3,500 port ops/frame unfiltered means the old 4096 default held barely one frame). Overflow mode is selectable: `ring` (evict oldest — default) or `stop-when-full` (keep the start of the run — essential for boot-sequence debugging). See `automation-and-export.md` §1.

## Proposed Changes

### Core Emulator (C++ Layer)

#### [NEW] `core/src/emulator/ports/portdiagrecorder.h`
#### [NEW] `core/src/emulator/ports/portdiagrecorder.cpp`
- Define `PortTraceEvent` POD struct (24 bytes, per `use-cases.md`) and `PortDeviceId` enum.
- Define `PortDecodeDisposition` — the small struct subclasses fill during decode and pass to the completion hooks:

```cpp
struct PortDecodeDisposition
{
    uint16_t decodedPort;     // 0x0000 = unmapped
    uint8_t  decodeRuleIndex; // index into portMasksMatches[]; 0xFF = no match / fallback
    PortDeviceId deviceId;    // resolved at dispatch time
    bool     wasDecoded;
    bool     hadHandler;
    bool     wasBeta128Gated;
    bool     wasHandledInline;
    bool     cfTrdosActive;   // CF_TRDOS state at event time (gate diagnosis, use case 1.5)
};
```

- Define `FilterDimension`, `FilterRule`, `CompoundFilterRule`, and `FilterSet` per `recording-control.md` (compound include rules are part of the initial design, not a follow-up).
- Implement `PortDiagnosticRecorder` using the existing `RingBuffer<PortTraceEvent>` (`common/ringbuffer.h`).
  - **Locking is honest, not lock-free**: `RingBuffer` takes a `unique_lock` on a `shared_mutex` per push. Uncontended (single producer = emulator thread; readers are occasional control-plane queries) this is ~20-50 ns and acceptable — the hot-path bench (below) verifies it. If the bench ever fails, the fallback is a dedicated SPSC buffer, which is a separate component, not a claim we make now.
  - Filter reconfiguration uses `std::atomic<std::shared_ptr<const FilterSet>>` — no raw-pointer swap, no "grace period" deletion (the earlier raw `FilterSet*` exchange sketch was a use-after-free hazard and is withdrawn).
  - Configurable capacity + overflow mode (`Ring` / `StopWhenFull`), settable only while stopped.
  - Session control (`start`, `stop`, `pause`, `resume`, `clear`) per `automation-and-export.md`.
- Implement `saveToFile()` for JSON, CSV, and raw Binary export. The JSON/binary headers embed the model's **decode rule table** (`{mask, match, port}` per rule) and `tStatesPerFrame`, so saved traces are self-describing and `porttrace_convert.py` never drifts from the C++ tables.

#### [MODIFY] `core/src/emulator/ports/portdecoder.h`
#### [MODIFY] `core/src/emulator/ports/portdecoder.cpp`
- Add `std::unique_ptr<PortDiagnosticRecorder> _portTrace` (nullptr while the feature is off), cached `bool _portTraceCache`, `getPortTraceRecorder()` accessor, and `UpdateFeatureCache()` participation.
- Extend `OnPortInComplete` / `OnPortOutComplete` signatures with `const PortDecodeDisposition&`. The hook records the single event via the `if (_portTraceCache) [[unlikely]] _portTrace->record(...)` inline check.
- **TTD symmetry fix (use case 6.5)**: while extending `OnPortInComplete`, add the missing `RecordIoRead` counterpart mirroring the existing `RecordIoWrite` at `portdecoder.cpp:242` (one line; independent of the PDR but same edit site). Coordinate with TTD owner on journal format.
- **Legacy base path (Ghost Byte visibility, use case 4.1)**: the base `PortDecoder::DecodePortIn/Out` legacy path calls `PeripheralPortIn/Out` directly *without* invoking `OnPort*Complete` (`portdecoder.cpp:128`, `:197`). A double read through that path would be invisible to the PDR — and the double read is precisely the bug we want to catch. Resolution, in order of preference:
  1. Audit whether any model still routes through the legacy path; if none, delete it.
  2. If it must stay, add the same guarded `record()` call there with a disposition flag `viaLegacyBasePath` (reuse flags bit 6), so both reads of a ghost pair are visible and distinguishable.

#### [MODIFY] All 7 `core/src/emulator/ports/models/portdecoder_*.cpp`
- `decodePort()` returns `DecodeResult { uint16_t port; uint8_t ruleIndex; }` instead of bare `uint16_t` (3 bytes by value; called exactly twice per I/O op).
- `DecodePortIn/Out` fills a `PortDecodeDisposition` from its existing locals (decoded port, gate outcome, dispatch branch taken) and passes it to the extended `OnPort*Complete`.
- Inline handling blocks set `wasHandledInline` in the disposition; they do **not** call the recorder.

#### [MODIFY] `core/src/base/featuremanager.h` / `.cpp`
- Register `kPortTrace` / `kPortTraceAlias` / description / `kCategoryDebug` and default-off state.

#### [MODIFY] `core/src/emulator/ports/portdecoder.h` — `PortActivitySummary` (cheap, ships first)
- The frame-scoped `PortActivitySummary` counters from `findings.md` are a ~30-minute change with immediate payoff ("unmapped OUTs this frame: 8" is the signal to reach for the full PDR). It moves from Phase 4 to Phase 1. It is gated by the same `porttrace` runtime feature (counters only increment when the feature is on).

### Automation Layer (Symmetry Mandate)

All transports are always compiled in and always register their commands; when the `porttrace` feature is off they respond "porttrace feature is disabled — enable with `feature porttrace on`".

#### [NEW] `core/automation/cli/src/commands/cli-processor-porttrace.cpp`
- Interactive CLI commands: `port-trace start/stop/pause/resume/clear/dump/save/status`.
- Filtering commands: `port-trace include ...`, `port-trace exclude ...`, `port-trace preset ...`, `port-trace filter show/clear` (compound-rule syntax per `recording-control.md`).
- Configuration: `port-trace config capacity <n>`, `port-trace config overflow ring|stop`.

#### [NEW] `core/automation/webapi/src/api/porttrace_api.cpp`
#### [MODIFY] `core/automation/webapi/src/openapi_spec.cpp`
- RESTful endpoints under `/api/v1/emulator/{id}/profiler/porttrace/` (session control, filter JSON, config, events retrieval) per `automation-and-export.md` §3.

#### [NEW] `core/automation/python/src/bindings/python_porttrace.h`
#### [NEW] `core/automation/lua/src/bindings/lua_porttrace.h`
- Direct `pybind11` / `sol2` bindings per `automation-and-export.md` §4-5.

### Offline Tools

#### [NEW] `tools/porttrace/porttrace_convert.py`
- Standalone converter/analyzer per `automation-and-export.md` §7.
- Reads the decode-rule table from the trace header (no hardcoded per-model masks) for `--analyze-strictness`.

---

## Implementation Phases (authoritative task breakdown)

### Phase 0: Runtime Feature Gate & Plumbing — DONE (2026-08-25)
- [x] `core/src/base/featuremanager.h/.cpp`: registered `kPortTrace = "porttrace"`, `kPortTraceAlias = "pt"`, `kPortTraceDesc`, category `kCategoryDebug`, default `off`
- [x] `PortDecoder::UpdateFeatureCache()` caching into `bool _portTraceFeatureCache`; wired into `FeatureManager::onFeatureChanged`; cache additionally primed in `Core` right after decoder creation (the decoder is created after features.ini loads, so a persisted porttrace=on would otherwise not take effect until the next toggle)
- [x] Lazy recorder lifecycle: feature on → instantiate `_portTrace`; feature off → stop capture, release buffer, `_portTrace = nullptr` (covered by `PortTrace_Test.FeatureToggleInstantiatesAndReleasesRecorder`)
- [ ] Transport behavior with the feature off: commands respond "porttrace feature is disabled — enable with `feature porttrace on`" (lands with Phase 2 transports)

### Phase 1: Port Diagnostic Recorder (Core) — DONE except noted (2026-08-25)
- [x] `PortTraceEvent` struct (24-byte layout, incl. `cfTrdosActive` bit 5 and `viaLegacyBasePath` bit 6) in `core/src/emulator/ports/portdiagrecorder.h`
- [x] `PortDeviceId` enum + `PortDiagnosticRecorder::ResolveDeviceId()` (decoded port → enum; `Custom` for other registered devices; `#1F` attributes to WD1793 — Kempston shares the port)
- [x] `PortDecodeDisposition` struct (decodedPort, decodeRuleIndex, wasDecoded, wasBeta128Gated, wasHandledInline, viaLegacyBasePath; hadHandler + cfTrdosActive resolved centrally in the hook)
- [x] `PortDiagnosticRecorder` on existing `RingBuffer<T>`. **Found and fixed a real pre-existing `RingBuffer::push` bug**: on the fill-up push it advanced `_head` prematurely, and on eviction it overwrote the *newest* slot instead of the oldest, corrupting FIFO order after wrap-around (also affected the TR-DOS analyzer, its only other user)
- [x] Configurable capacity (default 1,048,576 = 24 MB; raised from 65,536 after field use showed unfiltered rates of 50k-200k events/s) + `Ring` / `StopWhenFull` overflow modes, changeable only while stopped
- [x] Session control: `start`/`stop`/`pause`/`resume`/`clear`, `eventCount`, `capacity`, `totalProduced`, `totalEvicted`, `totalFiltered`, `wasAutoStopped`
- [x] Filters: `PortTraceFilterRule` (all set conditions AND — one struct covers both simple and compound rules via `std::optional` fields) + `PortTraceFilterSet` (include = OR of compounds, empty = all; exclude = flat OR, always wins)
- [x] Live filter reconfiguration — implemented with a `shared_mutex`-guarded filter (read under shared_lock per event) instead of `atomic<shared_ptr>`: portable, equally safe, and the recorder already takes a shared_mutex per push
- [x] Presets: `all`, `ay-only`, `fdc-only`, `no-fdc`, `outs-only`, `ins-only`, `unmapped` (`no-fe`, `sound`, `paging` deferred to Phase 2 with the transports)
- [x] `std::unique_ptr<PortDiagnosticRecorder> _portTrace` + `getPortTraceRecorder()` on `PortDecoder` (nullptr unless feature on)
- [x] `OnPortInComplete` / `OnPortOutComplete` extended with `const PortDecodeDisposition& disp = {}`; single guarded `RecordPortTrace()` call in each
- [x] `decodePortEx()` → `DecodeResult { port, ruleIndex }` in Pentagon128 (`decodePort()` delegates; Pentagon512 inherits). The other 5 models use if-chain decoders with no mask table — they report `PortTraceRule::kNoTable`, which faithfully represents their decode style
- [x] All models fill `PortDecodeDisposition` per dispatch branch; inline handlers set `wasHandledInline` only — single-event invariant holds (verified by tests)
- [x] Legacy base-path audit: unreachable in practice (every model overrides `DecodePortIn/Out`), but kept for API compatibility — instrumented with the guarded record call + `viaLegacyBasePath` flag so a Ghost-Byte pair through it is visible
- [ ] TTD symmetry fix (use case 6.5): `RecordIoRead` in `OnPortInComplete` — NOT done; requires a TTD journal format extension, coordinate with TTD owner before adding
- [x] `PortActivitySummary` frame-scoped counters in `PortDecoder` (gated by the feature; resets on frame advance; gated accesses counted separately from unmapped)
- [x] Unit tests: `core/tests/emulator/ports/porttrace_test.cpp` — 23 tests covering feature gating/lifecycle, single-event invariant (inline + unmapped + mirror-aliased), Beta128 gate + `cfTrdosActive`, handler attribution, compound include / exclude-wins / unmapped-only / live-reconfig / presets, pause-resume-stop, both overflow modes, config rejection while capturing, `getLast`, activity summary, decode-rule attribution. (Multithreaded filter-swap stress test still to add.)
- [x] Drive-by robustness fixes surfaced by testing: null-`pCore` guards in `PeripheralPortIn/Out` no-handler warning paths, null-`_memory` guard in `GetPCAddressLocator`, and a `core/tests/CMakeLists.txt` guard against adding `core/recording` twice (configure failed with `ENABLE_RECORDING=ON` before)

**Test run 2026-08-25** (`core/tests/build`, ENABLE_RECORDING=ON): 1714 passed, 13 failed — all 13 pre-date this work and touch none of it: 10 are the uncommitted WIP TurboSound-routing tests (the motivating bug), plus `FileHelper_Test.IsAbsolutePath_IsUNCPath` and `KeyboardInjection_Integration_test.TapKey_SingleCharacter`. All 23 PortTrace tests and all 18 pre-existing PortDecoder tests pass.

### Phase 2: Retrieval & Transport (Symmetry Mandate) — DONE (2026-08-25)
- [x] CLI `core/automation/cli/src/commands/cli-processor-porttrace.cpp`: `port-trace` (alias `porttrace`) — `start|stop|pause|resume|clear|status|dump [N]|save <path> [json|csv|bin]`; responds "porttrace feature is disabled — enable with 'feature porttrace on'" when the feature is off
- [x] CLI filtering: `port-trace include port FFFD direction out` (compound: conditions AND within one rule), `exclude ...`, `filter show|clear [includes|excludes]`, `preset <name>`
- [x] CLI config: `port-trace config capacity <n>`, `port-trace config overflow ring|stop`
- [x] WebAPI `core/automation/webapi/src/api/porttrace_api.cpp`: start/stop/pause/resume/clear/status/events/filter(GET+POST)/config/save under `/api/v1/emulator/{id}/profiler/porttrace/`; `events?limit=` defaults to unlimited (Hazard #136); feature-off → 409 with actionable message
- [x] WebAPI OpenAPI spec (`openapi_spec.cpp` + `openapi/openapi_porttrace.inc`)
- [x] Python bindings `core/automation/python/src/bindings/python_porttrace.h` (registered from `python_emulator.h`): lifecycle, `porttrace_include(port=..., direction=...)` kwargs = AND / separate calls = OR, `porttrace_events[_last|_since]()` as dicts, `porttrace_status()`, config, presets, `porttrace_save()`
- [x] Lua bindings `core/automation/lua/src/bindings/lua_porttrace.h` (registered from `lua_emulator.h`): same surface, tables in/tables out (`porttrace_include({port=0xFFFD, direction="out"})`)
- [x] `saveToFile()` in core (`PortDiagnosticRecorder`): JSON / CSV / Binary; headers embed `tStatesPerFrame` + the model's decode-rule table (`PortDecoder::getPortTraceDecodeRules()`, overridden by Pentagon128 to export its mask/match table). Binary layout is static_assert-pinned to the Python struct `"<QIHHHBBBBxx"`
- [x] Export tests: `PortTrace_Test.ExportAllFormats` (all three formats + binary header fields + embedded rule table) and `PortTrace_Test.FilterDescription`; `tools/porttrace/porttrace_convert.py --selftest` round-trips binary/JSON/CSV; converter additionally verified against real C++-generated artifacts (all 3 formats parse identically)

### Phase 3: Cross-Model Verification Tests
- [ ] `CrossModelDecodeTest` parameterized across all 7 models, all 65536 addresses, under **pinned machine states** (at minimum TR-DOS on/off — `decodePort()` is state-dependent in some models)
- [ ] Divergence allow-list mechanism (first run is an audit producing the allow-list, not a green test)
- [ ] `PeripheralRegistrationTest` per model: reset + standard registration → every expected port has a handler
- [ ] Integrate both into CI as regression guards

### Phase 4: Offline Tools & UI — converter DONE (2026-08-25), UI pending
- [x] `tools/porttrace/porttrace_convert.py`: readers (json/csv/bin, auto-detected by magic/extension), writers (json/csv/markdown/text), `--summary` (direction/device/port histograms, unmapped raw addresses, gated count, decode-rule distribution), filters (`--filter-port/device/direction/pc/unmapped`), `--selftest` (binary+JSON+CSV round-trip)
- [x] `--analyze-strictness`: single-bit near-miss analysis of unmapped events driven by the decode-rule table **from the trace header** — reports every candidate rule with the exact address line that would need to be dropped from the mask (no hardcoded per-model masks in Python)
- [x] `tools/porttrace/porttrace_capture.py`: one-command capture driver over the WebAPI — enables the `porttrace` feature, applies preset/compound filter rules and buffer config, captures for `--duration` (or until Enter with `--wait-key`), saves the canonical JSON server-side, and converts to any of json/csv/text/markdown/bin via `porttrace_convert`. **Verified live** (2026-08-25): against a running Pentagon instance via the automation binary — 1984 real boot events captured unfiltered with correct device/rule attribution, and a filtered AY-OUTs-only session (184 kept / 1160 filtered at capture)
- [ ] Debugger status panel reads `PortActivitySummary` once per frame (core side ready — `PortDecoder::getActivitySummary()`; UI wiring pending)
- [ ] Optional: port heatmap visualization in the memory viewer
- [ ] Optional: capture trigger ("start recording on first unmapped event / when PC hits X") layered on the filter machinery, pairs with `stop-when-full`

---

## Verification Plan

### Automated Tests
- `DebuggerHotpathBench` (and IO emulation tests) with three configurations:
  1. Feature `porttrace` off (default) — target: unmeasurable vs. baseline (< 1 ns/op; single cached-bool test + never-taken branch — same contract as every other runtime feature).
  2. Feature on, capture stopped — same target as (1).
  3. Feature on, capturing, `preset all` — target: < 100 ns per I/O op including ring-buffer push under `shared_mutex`.
- Unit tests: single-event invariant (one event per I/O op incl. inline-handled and unmapped ports), overflow modes, filter compound semantics, live filter swap under concurrent push, export round-trip (JSON/CSV/bin → converter → identical events), feature on/off toggling mid-run (recorder instantiation/release, no crash, no leak).
- WebAPI/CLI endpoint tests, including the "feature is disabled" response when `porttrace` is off.

### Manual Verification
- Arm the PDR via CLI and boot a TR-DOS disk with `overflow=stop`. Verify `WD1793_*` and `Beta128_System` events carry correct `wasBeta128Gated` / `cfTrdosActive` disposition, and the boot start is retained.
- Arm the PDR and play a TurboSound file. Verify the `(0xFFFD, 0xFE/0xFF)` chip-select writes interleave with `0xBFFD` data writes without drops or duplicates (one event per OUT — no inline double-record).
- With `porttrace` off in `features.ini`, confirm `port-trace` commands report the feature as disabled, no recorder is instantiated (no buffer allocation), and emulation speed matches baseline. Toggle `feature porttrace on` at runtime and confirm capture works without restart.
