# Future Expansion & Transition: Reusing This Mechanism Beyond Audio

- **Status:** draft
- **Date:** 2026-09-17
- **Assumes:** [`architecture.md`](./architecture.md) and
  [`goals-and-requirements.md`](./goals-and-requirements.md) (§11 /
  "Extensibility Beyond Audio") in this directory.

## 1. Purpose

`architecture.md` §11 states a design constraint — the event model and the
exporter must not be built in a way that's intrinsically audio-specific —
without saying concretely how a future effort would exploit that, or how
much of the *testing* burden that future effort would actually inherit
versus have to redo. This document answers both questions, for real
candidate domains: a new sound chip, floppy disk controller/drive timing,
and CPU bus cycles.

While researching this, a major fact surfaced that changes the reuse story
from hypothetical to concrete: **a proven, shipped, tested sibling system
already exists in this codebase.** §2 covers it; §3 is a ledger of exactly
what transfers as literal code, what transfers as a validated pattern, and
what remains genuinely new work per domain; §4 is a migration playbook per
target domain; §5 is the testing strategy that keeps re-testing
proportional to what's new.

---

## 2. A Proven Sibling Already Exists: the Port Diagnostic Recorder (PDR)

`core/src/emulator/ports/portdiagrecorder.h`, backed by
`core/src/common/ringbuffer.h`, exposed via
`core/automation/webapi/src/api/porttrace_api.cpp` and the `porttrace`
action in `mcp-analysis.cpp`, with CLI/Python/Lua bindings alongside. Per
its own design docs (`docs/inprogress/2026-08-24-diagnostic-observability/`,
marked **complete**, verified 2026-09-10):

- **`PortTraceEvent`** (24 bytes): absolute T-state timestamp, frame
  number, raw and decoded port, PC, the data byte, a decode-rule index, a
  `PortDeviceId`, and a flags byte (direction, whether a device responded,
  whether a handler existed, Beta128-gating state, etc.).
- **`PortDeviceId`** already enumerates `AY_FFFD`, `AY_BFFD`, `Covox`,
  `WD1793_Status`/`Track`/`Sector`/`Data`, `ULA_FE` (keyboard/border/
  beeper/tape), `Beta128_System`, several memory-paging ports, and
  `Custom` for anything else registered.
- **Zero cost when off**: "runtime-gated by FeatureManager feature
  `porttrace`... the hot path costs a single cached-bool test in
  `PortDecoder`" — the same pattern `architecture.md` §6/§7 independently
  arrived at for the sound taps.
- **A generic ring buffer template**, `RingBuffer<T>`, used for the event
  store: configurable capacity (default 1,048,576 events ≈ 24 MB), two
  overflow modes (`Ring` = evict oldest, `StopWhenFull` = keep the start of
  a run for boot debugging), and a two-layer include/exclude filter system
  (`PortTraceFilterRule`, matching on device/port/direction).
- **Full session lifecycle** (`start`/`stop`/`pause`/`resume`/`clear`) with
  automation parity across WebAPI, MCP, CLI, Python, and Lua, following a
  "Common Controller" rule — all business logic lives in the core class,
  transports are thin proxies.
- **Already tested**: `core/tests/emulator/ports/porttrace_test.cpp`, and
  called out as verified in `DONE.md`.

**What PDR does *not* give this design for free**, stated precisely so the
reuse claim below isn't overstated:

1. **It records a port operation, not a resulting signal level.** A
   `PortTraceEvent` for `AY_BFFD` tells you a byte was written to the AY
   data port at a given T-state — it does not tell you what that write did
   to a channel's instantaneous output level, which depends on the chip's
   other live register state (volume, envelope, mixer). This is exactly the
   same gap `architecture.md` §5.3 already identified and rejected TTD's
   write journal over, for the identical reason — computing "level" is
   domain interpretation this design must still do itself.
2. **Its ring is mutex-protected, not lock-free.** `RingBuffer<T>` uses
   `std::shared_mutex` (`unique_lock` on write, `shared_lock` on read).
   That's an appropriate cost for an opt-in diagnostic feature at port-I/O
   rates, but it is not the sub-10 ns, lock-free SPSC discipline
   `architecture.md` §6 requires for the emulation-thread append path — see
   §3 below for exactly where this does and doesn't matter.
3. **It exports to structured data (JSON, over WebAPI/MCP/CLI), not to a
   waveform/interchange format.** No VCD or equivalent export exists
   anywhere in this codebase's diagnostic tooling today. Goal 6's
   multi-track exporter (`architecture.md` §5.6) remains genuinely new
   work, not a duplicate of something PDR already does.

---

## 3. The Reuse Ledger

| Component | Reuse literally | Reuse as a validated pattern | New work required per domain |
|---|---|---|---|
| Zero-cost-when-inactive gating | — | **Yes, already proven twice** (PDR's cached-bool test; this design's atomic-bool guard, independently arrived at). Any new domain should use the same shape without re-litigating it. | A cached flag check at that domain's own hot-path entry point. |
| Event ring on the **emulation-thread append path** | No — `RingBuffer<T>`'s `shared_mutex` doesn't meet the lock-free cost budget this design needs (§6). | The *design* of the lock-free SPSC ring (already precedented by `AudioRingBuffer`/`NativeAudioTap`, cited in `architecture.md` §6) is the pattern to reuse, not PDR's ring class. | A lock-free SPSC ring sized for the new domain's event rate — same shape as `architecture.md` §5.1's staging ring, new instance. |
| Event ring **anywhere off the emulation thread** (flush-thread-side buffers, `monitor` flagged-window logs, WebAPI-side staging) | **Yes** — `RingBuffer<T>` is a template; there is no cost-budget reason not to use it directly here. | — | Just instantiate it with the right `T`. |
| "Where do I hook a write/state-change" for AY, Covox, WD1793, or beeper | **Partially** — PDR already dispatches a structured, attributed, T-state-stamped event at exactly these ports today. A new domain's raw "did something happen" signal doesn't need rediscovering for these four. | The dispatch *point* (`PortDecoder`'s `OnPortInComplete`/`OnPortOutComplete`) is a candidate single hook site worth sharing rather than installing a second independent one beside it — see §4.2. | The domain-specific interpretation: turning a raw `(port, value)` into a computed level (audio) or a state transition (FDC). |
| Session lifecycle + automation parity (`start`/`stop`/filter/export, mirrored across WebAPI/MCP/CLI/Python/Lua) | Partially — the *shape* of `architecture.md` §5.7's `arm`/`disarm`/`capture`/`monitor` surface is close enough to PDR's `start`/`stop`/`pause`/`resume`/filter surface that growing PDR's own dispatcher, rather than building `analyze_audio` as an unrelated tool, is worth evaluating (§6). | The "Common Controller" rule itself (business logic in core, transports thin) — already this codebase's convention, not new to justify. | Whatever verbs a domain needs that PDR's don't already cover (`spectrum`, `compare`, `correlate` have no PDR equivalent — they're signal-analysis, not event-log, operations). |
| Compressed long-duration store (`architecture.md` §5.3) | No existing sibling found — TTD's `TTDCodecPageStore` was already evaluated and ruled out as a literal base (thread-model mismatch, §5.3). PDR doesn't compress; it just bounds capacity and evicts. | TTD's codec *primitives* (`ttdcompression.h`) remain the reusable piece, as already designed. | This stays genuinely new infrastructure — the one piece with no existing generic sibling. |
| Multi-track VCD export (`architecture.md` §5.6) | No existing sibling — confirmed nothing in this codebase's diagnostic exports produces VCD or an equivalent interchange format today. | `vcd_writer`'s own multi-signal API (already cited). | Genuinely new, per domain only in *which* signals get named tracks — the exporter itself is domain-agnostic once built. |
| Signal analysis helpers — time/frequency domain, correlation (`architecture.md` §5.5) | No existing sibling for the *analysis* math (PDR does event logging, not signal analysis). | `AudioHelper`/`simple_fft` (already cited) for anything sample-array-shaped; a digital-timing domain (FDC, CPU bus) would reuse the same time-series/correlation *machinery* with different metric functions (jitter/duty-cycle instead of pitch/THD). | The metric functions themselves are domain-specific and are the one part of "analysis" that must be written fresh per domain — small, focused functions, not new infrastructure. |

---

## 4. Migration Playbook Per Target Domain

### 4.1 A new sound chip (lowest effort — this is what §5.1/§5.8 already assume)

Already designed for in `architecture.md`: register the chip in
`SoundManager`'s `AudioSourceType`/device registry, hook its
output-affecting write into the shared `ScopeEvent` append path, and it
gets a UI tab (§5.8, already registry-driven) and trace/export/analysis
support with no new capture mechanism. **New work:** the chip's own
render/hook wiring and, if it has a distinct DSP pipeline shape, new tap
points analogous to §4's five (a chip with no DC filter or character chain
just has fewer taps, not a different tap *mechanism*).

### 4.2 Floppy disk controller/drive timing (WD1793) — now concretely grounded

This is materially easier than `goals-and-requirements.md`'s extensibility
section assumed, because PDR **already emits T-state-stamped, attributed
events for all four WD1793 ports** (`WD1793_Status`/`Track`/`Sector`/
`Data`) today, feature-gated and tested. The event-existence question this
domain would otherwise have to answer from scratch — "is there a
low-overhead hook that fires on FDC register access, with correct
timing?" — is already answered, in production code, with a passing test
suite.

What's still new, and it's the same shape of work sound already does for
AY: PDR's raw `(port, value)` doesn't tell you "the index pulse just
started" or "the head just stepped" — that interpretation lives inside
`WD1793` itself (`_lastIndexPulseStartTime`, `_indexPulseCounter`,
`processStep()`), not in the port trace. An FDC oscillogram needs a small
adapter that turns *that* internal state into the same `ScopeEvent` shape
(a discrete, T-state-stamped level/state change) sound already defines —
reusing §5.2's struct, §5.3's store, and §5.6's exporter unmodified, with a
new `source` namespace and a handful of FDC-specific derived metrics
(index-pulse period → rotation speed and jitter; pulse width; time from
command issue to completion) plugged into the same per-window/time-series
machinery §5.5 already built for pitch/amplitude.

### 4.3 CPU bus cycles — the one domain needing genuine new capacity analysis

Two different things could be meant by "CPU bus cycles," and they have very
different costs:

- **I/O bus operations (every `IN`/`OUT`)**: this is what PDR already
  captures, today, for every port — not just the four devices enumerated
  above. If the ask is "trace arbitrary port I/O as an oscillogram," the
  event source already exists; only the level-shaped `ScopeEvent` adapter
  and export wiring would be new, same as §4.2.
- **Full memory/M1 bus-cycle tracing (every T-state)**: this is genuinely
  different and would need its own version of `architecture.md` §3's rate
  analysis, not a reuse of its conclusion. §3's entire argument for
  event-driven-not-fixed-rate capture rests on port writes being *sparse*
  relative to the T-state clock (8–110 kHz against a 3.5 MHz clock, §3). A
  full bus trace has no such sparsity — most T-states have bus activity
  (fetch, memory read/write) — so an event-per-transition model here could
  push rates orders of magnitude past anything this design's cost tables
  (`architecture.md` §7) were built for. This would need either a
  decimation/windowing strategy (trace a bounded address range or a bounded
  duration, analogous to how `monitor` bounds its own footprint), or
  acceptance of a much smaller capture window than the minutes-scale
  history goal 2 targets for audio. No existing hook for full per-T-state
  bus tracing was found in this codebase — unlike §4.2, this direction is
  plausible, not grounded, and the rate math above is the reason it can't
  just inherit §3's conclusion by analogy.

---

## 5. Testing Strategy: Keeping Re-Testing Proportional

The principle: test the domain-agnostic kernel once, generically; require
each new domain adapter to pass a small, fixed contract, not to re-earn the
whole pipeline's confidence.

**Already covered, inherited for free if reused rather than re-implemented:**

- Ring-buffer correctness (overflow behavior, capacity, FIFO eviction) —
  `RingBuffer<T>` already has this from PDR's own test suite
  (`porttrace_test.cpp`), for anything off the emulation-thread hot path
  (§3's ledger).
- Session lifecycle and filter-rule correctness (`start`/`stop`/`pause`/
  `resume`, include/exclude matching) — same source, if a new domain's
  automation surface is grown as an extension of PDR's dispatcher rather
  than reimplemented (§6).
- Codec round-trip, compression-ratio sanity, and keyframe-index random
  access — once `architecture.md` §5.3's store and §9's Phase 2 tests land,
  a new domain reusing that store class inherits this without writing new
  codec tests, provided it doesn't reimplement the store.
- FFT correctness and base-frequency detection — already exercised by
  `AudioHelper`'s existing usage, prior to this project.

**Cannot be inherited, and shouldn't be — small and domain-specific by
nature:**

- **The hook-correctness test**: does the new domain's dispatch point push
  an event with the right timestamp and value? One focused test per new
  event source (e.g., "a synthetic index-pulse sequence produces
  `ScopeEvent`s at the right T-states" for WD1793) — this is inherently new
  per domain because the hook itself is new, but it's a handful of test
  cases, not a subsystem.
- **The metric-correctness test**: does the new domain's derived-metric
  function (pulse period, jitter, duty cycle — analogous to pitch/THD for
  audio) compute the right answer against a synthetic, known-truth input?
  Same scale of effort as the audio metric tests already planned
  (`architecture.md` §10).
- **Export content tests**: does the new domain's `source`/track naming
  show up correctly in a VCD export? A few assertions against the existing
  export test pattern (`architecture.md` §10's VCD export bullet), not a
  new export-format test suite.

**The net effect**: a new sound chip needs roughly what §9 Phase 1's own
hook tests already cost. FDC needs the same, *plus* it starts from an
already-tested event source (§4.2) rather than an unproven one. Only full
per-T-state CPU bus tracing would need new capacity/overflow testing
proportional to its own (currently unresolved) rate problem — everything
else in this ledger is either literal reuse or a small, bounded addition on
top of infrastructure that has already earned its confidence once.

---

## 6. Recommendation Back to the Architecture

This is a recommendation for the architecture owner to weigh, not a change
made unilaterally here: given §2's finding, it is worth revisiting whether
`architecture.md` §5.1's `ScopeTap` append should happen at the same
dispatch point PDR already instruments (`PortDecoder`'s port-complete
hooks) rather than as a second, independent hook installed next to it —
even if the sound event struct and ring stay separate from PDR's (they
need different fields and a stricter locking discipline, per §3's ledger).
Sharing the *dispatch point* — not PDR's struct, not its ring — is the one
architectural decision in `architecture.md` that this document suggests
reopening before Phase 1 implementation begins, because doing it after the
fact would touch the same hot-path call sites twice instead of once.
