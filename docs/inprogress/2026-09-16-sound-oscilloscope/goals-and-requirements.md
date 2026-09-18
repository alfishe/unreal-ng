# Sound Oscilloscope / Sound Chip Debug Panel — Goals & Requirements

- **Status:** draft
- **Date:** 2026-09-17

This document states the problem, the goals, and the non-goals only. It does
not describe or assume any implementation, data structure, API shape, or
delivery phasing — those belong in the technical design document once these
goals are agreed. Each goal is written to stand on its own.

---

## Problem

There is currently no way to see what the sound chips are actually doing at
the signal level while debugging or verifying the emulator. AY digitizer
playback, beeper tricks, and FM voices are audible but otherwise invisible.
A bug in a digitizer player, a mixing/gain-staging regression, or a
"why does this loading tune sound wrong" report all have to be diagnosed
from disassembly and register dumps alone, with no way to see the resulting
waveform, line it up against the code that produced it, or check it
automatically for correctness.

---

## Goals (Functional Requirements)

1. **Exact-timing visibility into chip output.** It must be possible to see
   the effect of an individual AY / beeper / Covox output-affecting write as
   a level change, with timing precision fine enough to distinguish
   individual samples in an 8–24 kHz digitizer stream, instruction by
   instruction.
2. **Review after the fact, not just live.** It must be possible to capture
   and scroll back through several minutes of prior sound-chip activity
   (e.g. a full demo part, or a whole tape load) without needing to have
   already been watching, paused, at the exact moment the activity of
   interest occurred.
3. **Correlate waveform with code.** It must be possible to identify, for
   any observed level change, which CPU instruction (program counter,
   T-state) caused it.
4. **Cover the whole sound stack, generically.** The capability must cover
   every sound-producing device the emulator has — Beeper, Covox, both AY
   chips, and TurboSound FM today — and any sound chip added later (a
   PCM/sample player, General Sound, Moonsound/OPL3, or anything else), not
   one chip debugged in isolation while others stay unobservable. Adding a
   new sound chip must not require bespoke integration work for this
   capability each time.
5. **Zero cost when unused, bounded and known cost when used.** There must
   be no measurable cost on the emulation hot path when this capability is
   inactive. Whatever cost it adds while active must be known and bounded,
   not assumed.
6. **Interoperability with external waveform tooling.** It must be possible
   to export a captured trace — up to the full duration retained under
   goal 2, not only a short excerpt — into a standard interchange format
   (e.g. VCD) so it can be opened in an external waveform viewer and shared
   as evidence of a problem, or examined with tools this capability doesn't
   itself provide, independent of this emulator. The export must carry
   **multiple simultaneous, time-aligned signals in one file** — every
   source, and both sides of a processing stage where relevant — not one
   channel at a time: this is what lets an external tool, not a UI this
   capability would otherwise have to build itself, actually show
   cross-source and pre-/post-stage relationships side by side. Extended
   from the use-case exercise below (use cases 8, 9, and 11): the original
   wording didn't say export must cover the whole retained capture, nor
   that it must be multi-signal rather than single-channel — without the
   latter, goals 7's stage-attribution need and goal 9's cross-source
   comparison would have no way to be visually inspected at all, since
   goal 9's own Non-Goal explicitly declines to build a dedicated
   visualization UI in favor of this export path.
7. **Automated, scriptable signal-quality analysis.** It must be possible to
   check "is this emulation producing correct audio" programmatically,
   without a human looking at a picture — covering: incorrect PSG/FM
   emulation behavior (e.g. wrong duty cycle, envelope curve, or volume
   table), hiss / noise-floor regressions, clicks and glitches (unexpected
   signal discontinuities introduced anywhere in audio processing), and
   pre-/post-mix problems (clipping, incorrect gain staging, incorrect
   panning). This must be reachable through the emulator's existing
   automation interfaces (WebAPI / MCP), not only through a human-operated
   UI. It must cover two distinct use patterns:
   - **On demand:** analyze a specific, requested window of activity.
   - **Continuous / unattended:** run for an extended or open-ended session
     (e.g. a long automated playthrough) and flag problems as they occur,
     without requiring the entire session's audio to have been retained in
     order to do so.
8. **Regression comparison against a reference capture.** It must be
   possible to compare a newly captured signal against a previously stored
   ("known-good") reference capture and report where and by how much they
   differ, in both time and frequency domain — catching regressions that
   are technically clean (no clipping, no clicks, no added noise) but still
   wrong, such as an incorrect volume table or an altered mix balance that
   changes the sound without tripping any absolute-quality check. Producing,
   curating, or versioning the set of reference captures a project
   considers "known-good" is not part of this requirement (see Non-Goals).
   Added from the use-case exercise below (use case 9) — not covered by
   goals 1–7 as originally stated.
9. **Cross-source drift and whole-capture trend analysis.** It must be
   possible to analyze an entire captured piece — not just a short window —
   for problems that only show up as a *trend over time* or as a
   *relationship between two sources*, rather than as a defect visible in
   any single instant: sound sources drifting out of sync or out of tune
   with each other over the course of a piece (e.g. one chip's effective
   pitch or timing gradually diverging from another's), and imbalanced or
   drifting relative amplitude between sources. This requires tracking
   frequency- and amplitude-domain measurements as a **time series across
   the whole capture**, and comparing that time series **between two
   concurrently-active sources** — distinct from comparing a source against
   an external reference (goal 8) or comparing one processing stage against
   another (goal 7's derived requirement). Added from the use-case exercise
   below (use case 11) — not covered by goals 1–8 as originally stated.

### Derived requirements

These follow necessarily from goals 7, 8, and 9 and are stated as
requirements on the solution, not as a proposed solution:

- The solution must be able to identify **which stage of audio processing**
  a defect was introduced at — e.g. distinguishing a defect already present
  in a chip's raw output from one introduced by DC filtering, by
  interpolation/decimation filtering, by another DSP effect, or by final
  mixing. This requires the ability to inspect and compare signal state at
  more than one point in the processing path, not only the final output.
- The solution must support both **time-domain** measurement (e.g. level,
  DC offset, clipping, abrupt discontinuities) and **frequency-domain**
  measurement (e.g. spectral content, harmonic distortion, noise floor).
- The continuous/unattended use pattern (goal 7) must not require storing an
  entire session's raw audio to be useful — memory/storage use for an
  open-ended session must be bounded regardless of session length.
- The solution must support comparing **two** captured signals (a live
  capture against a stored reference, or one pipeline stage against
  another) and quantifying their difference — not only characterizing a
  single signal in isolation (supports goal 8 in addition to the
  stage-attribution need above).
- The solution must be able to produce a **time series** of a measurement
  across a whole capture (e.g. estimated pitch or amplitude per time slice),
  not only a single aggregate value for the capture as a whole — otherwise a
  gradual drift (goal 9) averages out and disappears from the result.

---

## Non-Goals

- **Full-fidelity, always-on continuous audio capture as a standing
  background feature.** Any continuous capture this capability needs exists
  only while a specific check is actively requested — not running by
  default.
- **A general-purpose "what does it sound like" live listening scope for
  casual use, independent of debugging or analysis.** This is a diagnostic
  capability, not a listening feature; goal 1 does not require it.
- **Storing an entire unattended session's raw audio.** The continuous/
  unattended use pattern in goal 7 is explicitly required to avoid this
  (see Derived requirements above).
- **Unbounded interactive review history, or persisting it to disk.**
  Goal 2 asks for "several minutes," not unbounded history; extending
  interactive review beyond that, or persisting it beyond the current
  session, is out of scope until a concrete need for it is stated. Goal 6's
  export requirement is bounded by whatever goal 2 already retains — export
  does not imply a *longer* retained duration than goal 2 provides. If
  "the whole piece" (use case 11) ever needs to exceed goal 2's stated
  bound for some pieces, that is a change to goal 2, not something this
  document resolves by assumption.
- **Reverse (time-travel) search over historical audio.** Out of scope until
  a concrete need is stated; the continuous/unattended requirement in goal 7
  already covers catching a problem as it happens, which is a different
  need than searching backward through history after the fact.
- **Full-fidelity capture of continuous/analog-like sources (e.g. complete
  FM waveform fidelity).** An approximate/representative view is sufficient
  for the diagnostic purposes in goals 1–4; goal 7's correctness checks for
  FM are about detecting gross errors, not exact waveform reproduction.
- **Maintaining or curating a library of reference/"known-good" captures.**
  Goal 8 only requires the ability to compare two given captures; deciding
  what a project's approved reference set is, and keeping it up to date, is
  a test-infrastructure concern outside this scope.
- **Building a dedicated spectrogram/trend-visualization UI in this
  emulator.** Goal 9 requires the *measurement* (a time series that can
  reveal drift) to exist and be retrievable; visualizing it is covered by
  goal 6's export path into external tooling that already does this well,
  not by adding a new visualization surface here.
- **Building trace/analysis capability for non-audio subsystems** — e.g.
  floppy disk controller/drive signal timing, or CPU bus cycles — as part
  of this effort. Out of scope for delivery here; this is a sound chip
  debug panel. See "Extensibility beyond audio" below for why the
  architecture should not be built in a way that precludes reusing the same
  approach there later, without that reuse being something this effort
  delivers.

---

## Extensibility Beyond Audio (design constraint, not a delivered goal)

This capability's core mechanism — a timestamped event or sample recorded
at a hot-path dispatch point, tied to the shared T-state clock, kept in a
compressed long-duration store, and exportable as one or more named tracks
in a standard interchange format — is not intrinsically an audio concept.
Two other parts of this emulator have discrete, hot-path state transitions
of the same shape, and the interchange format goal 6 requires (VCD) has
digital hardware signal tracing as its *original* purpose, not audio:

- **Floppy disk controller/drive timing** (the WD1793 controller): index
  pulses, step pulses, and command state transitions are already tracked
  internally with T-state timestamps. The same event shape this capability
  defines for a sound-chip port write applies equally well to an index
  pulse or a step command.
- **CPU bus cycles**: instruction-fetch/memory/IO cycle timing is exactly
  what VCD was designed to dump in the first place — this direction of
  reuse is, if anything, more natural than the audio use case that
  motivated this document.

**What this requires of the design (not a new goal to deliver):** the
event/tap data model and the multi-track exporter must not be built in a
way that is intrinsically audio-specific — e.g. hardcoding "level" as an
audio amplitude, or "source" as a sound-chip-only identifier. A source
should be identifiable generically enough that a future, separate effort
could register a non-audio source through the same mechanism without
redesigning it. **Building that non-audio support is explicitly not part of
this effort's delivery** (see Non-Goals above) — this section only
constrains how goal 4's "generically" and goal 6's export are built, so
that door isn't accidentally closed.

---

## Use Cases → Requirements Traceability

Each row is a concrete scenario this capability must handle, mapped to the
goal(s) it exercises. Where a scenario needed a requirement that didn't
already exist, the goals above were extended rather than leaving the gap
implicit — use case 9 is why goal 8 exists.

| # | Use case | Goals exercised | Notes |
|---|---|---|---|
| 1 | Step through a digitizer effect instruction-by-instruction while paused, to see exactly what each output write produces. | 1, 3, 4 | |
| 2 | Investigate a "the music sounds wrong somewhere in this demo" report after the run has already finished, with no prior pause point. | 2, 4 | |
| 3 | Confirm a multi-source tune (AY + beeper + Covox + FM layered) renders correctly across every source at once, not just the loudest one. | 4 | |
| 4 | Leave the capability available throughout an unrelated CPU-debugging session without slowing the emulator down. | 5 | |
| 5 | Hand a captured trace to another developer, or open it in an external waveform viewer, as evidence of a bug. | 6 | |
| 6 | Run a scripted, one-shot audio-quality check against a specific test case as part of a build/CI step after a sound-code change. | 5, 7 (on-demand) | |
| 7 | Run an unattended, hours-long automated playthrough across a library of test programs and be told about any audio glitch introduced, without a human watching and without recording every run's full audio. | 5, 7 (continuous/unattended) | Confirms the bounded-memory requirement in goal 7's continuous pattern is load-bearing, not incidental. |
| 8 | Given a reported crackling sound, determine whether it originates in the chip's own output or was introduced by a specific downstream processing stage (e.g. only appears with one particular effect enabled). | 6, 7 (derived: stage attribution) | Answering "which stage" by eye, not only as a programmatic result, needs the pre- and post-stage signals exported as two aligned tracks in one file (goal 6's multi-signal requirement) — added goal 6 here on review. |
| 9 | After refactoring a chip's internal emulation (e.g. its volume table or envelope generator), confirm the audio output hasn't silently changed compared to before the refactor — even though nothing clips, clicks, or adds noise. | 8 | Not covered by goals 1–7 as originally stated: those detect absolute-quality problems, not "did this change from a known-good baseline." This use case is why goal 8 was added. |
| 10 | Debug a fast-loader routine's beeper bit-timing against the exact instruction stream producing it. | 1, 3, 4 | |
| 11 | Export a full demo or music piece's captured audio (or view it directly) for thorough offline analysis: checking for sources drifting out of sync or out of tune with each other, imbalanced amplitude between sources, hiss, ticks, glitches, and other frequency/temporal issues across the whole piece. | 6, 7, 9 | Exposed three gaps: (a) goal 6 didn't say export must cover the *whole* retained capture, not just a short excerpt — extended; (b) nothing covered comparing two sources against *each other* for drift, or tracking a measurement as a trend over a whole capture rather than one window's aggregate — added goal 9; (c) cross-source and pre-/post-stage relationships (goal 9, and goal 7's derived stage-attribution need) can only be inspected externally if the export carries multiple aligned signals at once, not one channel per export — goal 6 extended to require this. Also surfaced a tension, deliberately left open rather than silently resolved: "the whole piece" could exceed goal 2's "several minutes" bound for some pieces — see the note on that bound under Non-Goals. |

---

## Feasibility Materials

This section records evidence, found in the current codebase, that each
goal is achievable without inventing capability from nothing. It states
facts about what already exists; it does not propose how the new
capability should be built.

- **Goals 1 & 3 (exact-timing visibility, code correlation):** the sound
  chips already dispatch through explicit, single-purpose port-write
  handlers (the AY8910 chip's output-port handler, the beeper's port-write
  handler), and the emulator's instruction-stepping primitives (single-cycle
  step, run-N-T-states, step-over, step-out) already route through the same
  per-instruction hook used to advance sound-chip state. This means the
  information needed to answer "what changed, and which instruction caused
  it, at what T-state" is already produced at a single, well-defined point
  in the existing code — it does not need to be reconstructed or inferred.
- **Goal 4 (whole sound stack, generically):** the sound subsystem already
  has a uniform per-chip/per-device registry (both AY chips, beeper, Covox,
  and the TurboSound/FM slot are all managed through one sound manager with
  a consistent per-device interface), and that registry's own source
  identifier already lists devices not yet implemented (a general-purpose
  sample player, an OPL3-class chip) as placeholders alongside the real
  ones. "Cover every device, including future ones" is therefore a matter
  of hooking this capability to a registry that is already built to grow,
  not integrating each chip one-off.
- **Extensibility beyond audio (design constraint):** not required for this
  effort, but grounded rather than speculative for at least one of the two
  named directions — the floppy disk controller already tracks index-pulse
  and step timing with T-state stamps internally, the same shape of
  information a port-write event captures for a sound chip, so the event
  model generalizing to it later would not need new invention. CPU bus
  cycle tracing is comparatively less grounded here (no existing per-cycle
  hook was identified in this pass) — stated as a plausible, not verified,
  extension.
- **Goal 5 (zero cost when unused):** the audio subsystem already contains
  lock-free, single-producer/single-consumer capture patterns used for
  near-zero-cost taps on the emulation hot path elsewhere in this codebase.
  This is existing, working precedent that a similarly cheap tap is
  achievable here, not an unverified hope.
- **Goal 6 (VCD interoperability, including multi-signal export):** a VCD
  (Value Change Dump) writer module is already present in the codebase's
  third-party sources, currently unused. Its API is already built around
  registering any number of independently named signals (each with its own
  hierarchical scope and name) and recording value changes against each one
  on a shared timestamp — multi-track export is what this module's API is
  for, not an extension goal 6 would need to add on top of it. The
  interoperability target in goal 6 already has a working implementation
  available to build on rather than needing a new one written or a new
  external dependency added.
- **Goal 7, frequency-domain requirement:** an FFT library is already
  vendored in the codebase, and is already used in production code for a
  comparable purpose (detecting a signal's base frequency from a sample
  buffer). Frequency-domain analysis is therefore not a new class of
  capability for this codebase.
- **Goal 7, time-domain requirement:** a general-purpose audio-helper module
  already exists with time-domain utilities (DC-rejection filtering, sample
  format conversion) that a broader set of time-domain measurements is a
  natural extension of.
- **Derived requirement — multiple pipeline inspection points:** the audio
  signal path already passes through several distinct, separately-invoked
  processing stages on its way from chip output to final mixed output
  (per-chip rendering including its own interpolation/decimation filtering,
  a DC-blocking filter, a post-processing effects stage, and a final
  mixing/gain stage), each already implemented as a separately callable
  unit rather than one monolithic function. Inspecting signal state between
  named stages is therefore not blocked by the existing structure of the
  code.
- **Derived requirement — bounded-memory long-duration review (goal 2):** a
  prior, unrelated capability in this codebase already solved "compressed,
  scrubbable review of long-duration activity with bounded memory" for a
  different kind of event stream, with **measured** (not estimated)
  compression ratios and per-unit CPU cost. This is existence proof — not
  merely a hopeful estimate — that goal 2's requirement (minutes-scale
  review, bounded memory) is achievable at reasonable, previously-measured
  cost.
- **Goal 7, WebAPI/MCP reachability:** the emulator already has a mature
  automation surface (a WebAPI and an MCP tool layer) with an established
  pattern for exposing debug and analysis operations as callable, scriptable
  actions. Goal 7's requirement to be reachable via automation, without a
  human operating a UI, already has a channel to be exposed through.
- **Goal 8 (regression comparison):** weaker grounding than the items
  above, stated honestly — no dedicated "compare two captures" capability
  exists yet. It requires no new class of measurement, though: it is the
  same time-/frequency-domain functions goal 7 already grounds, run once
  per capture and diffed, rather than a new analysis technique. This is a
  natural extension of already-available building blocks, not existence
  proof the way the other bullets are.
- **Goal 9 (cross-source drift, whole-capture trend):** partially grounded.
  Every AY chip is already individually addressable through a uniform
  accessor on a shared T-state clock, so two sources can already be
  observed on one common time base — the structural precondition for
  comparing them against each other rather than only in isolation. What is
  *not* already present is any existing "track a measurement as a time
  series and compare two such series" capability; like goal 8, this is a
  natural extension of goal 7's per-window measurement functions (run
  repeatedly across a capture and kept as a sequence), not something this
  codebase already does elsewhere.

---

