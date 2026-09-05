# Turbo Tape Loading — Warp-Speed Signal Playback for Headerless Blocks

| Rev | Date | Description |
|-----|------|-------------|
| r1 | 2026-09-04 | Initial analysis and design. Triggered by the r12 headerless-rule wiring in `2026-09-01-tape-manager` (DIZZY_X blocks 2–4 now honestly report "no fast load") and the follow-up question: *we cannot hook the ROM routine for headerless blocks, but why can't we simply turn on turbo mode and disable it once loading finishes?* |

## TL;DR

- **Headerless blocks cannot be *instant-loaded*** by the LD-BYTES trap — not because of a missing feature, but because the trap's entire safety contract is "I know exactly what the ROM routine would have read and written", and a headerless block's consumer is an arbitrary custom loader that never calls the hooked routine and speaks an arbitrary protocol. There is nothing to hook and nothing to validate against.
- **But they *can* be loaded fast** the way the question proposes: unthrottled ("turbo") emulation while the signal path plays. Turbo mode changes only the wall-clock rate of emulation; every frame is still 69 888 T-states with bit-exact EAR timing, so the loader's view is **identical** to a real-speed load — same memory result, same frame count, same load duration *in machine time*. A 280 s custom-loader payload finishes in ~18–28 s of host time at the documented 10–16x turbo factor.
- **The only genuinely hard part is "disable it once we finished loading"** — knowing when an arbitrary loader is done. The codebase already owns three independent completion detectors built for exactly this: the read-gap watchdog (`_framesSinceLastRead > 150` → `pausePlayback()`), the ERR_NR break/error stop, and natural end-of-tape (`Ended`). The sustained EAR-polling resume even re-triggers playback — and therefore re-engages turbo — for every subsequent stage of a multi-stage loader.
- **What is missing is a small orchestrator**: engage turbo when signal playback starts, disengage on the existing tape-state transitions, never fight the user's manual turbo toggle. This document specifies it (`turbotape` feature, `TapeTurboController`, ~1 frame reaction latency, zero changes to `Tape`'s state machine).

## 1. Terminology — three meanings of "turbo" in this project

| Term | Meaning | Where |
|------|---------|-------|
| **Turbo mode** | Emulator speed feature: bypass frame pacing, run unthrottled. Machine timing per frame is unchanged. | `Core::EnableTurboMode` / `config.turbo_mode`, `docs/emulator/design/core/speed-control.md` |
| **Turbo block** | Tape content kind: TZX `$11` "turbo speed" block — custom pulse timings, nothing to do with emulator speed. | `FastLoadRejectEnum::NonStandardTiming`, tape catalog "speed profile" |
| **Turbo tape loading** (this design) | New feature: automatically engage *turbo mode* while the *signal path* is playing, so every block the trap cannot serve — headerless, turbo-timed, pulse streams, checksum-broken — still loads at warp speed. | Feature flag `turbotape` |

The feature deliberately reuses the emulator's turbo *mode* and applies to any signal-path playback, including TZX turbo *blocks*.

## 2. Why the LD-BYTES trap can never serve headerless blocks

The trap (`core/src/emulator/io/tape/tapefastload.cpp`, design `2026-08-30-fast-tape-loading`) is a **protocol-level replay**, not an acceleration. It fires only when:

1. PC lands on the fixed ROM entry `$0556` (LD-BYTES),
2. the bank at `$0000` byte-matches the known LD-BYTES prologue (`CheckROMSignature`),
3. the block at the consumption cursor is *vanilla*: ROM-standard timing profile, `$00`/`$FF` flag, exact expected length, XOR checksum valid (decline matrix rows 6–8).

It then performs the routine's memory and register side effects exactly as the ROM would (`ApplyLoadEffects`) — the differential test asserts byte-identical RAM against a real-speed load. The whole safety contract rests on **complete knowledge of one fixed protocol**: what the routine reads (pilot → sync → bytes → checksum) and what it writes (payload stores, IX/DE/AF postconditions, RET semantics).

A headerless block's consumer is a **custom loader**: code inside the loaded program, speaking whatever protocol its author invented:

- arbitrary bit timings (ROM tolerance ±15 % is *their* choice to break),
- arbitrary framing (no `$00` header / `$FF` data distinction, custom sync patterns, interleaved pilot and data),
- arbitrary or absent checksums, non-byte encodings (pure pulse streams — `FastLoadRejectEnum::PulseStream`),
- arbitrary destinations (bank switching mid-load, decompression on the fly — see `insult.tap`, `2026-08-30-fast-tape-loading` §9.5).

Such a loader **never calls `$0556`** — so the trap has no trigger — and even if we trapped its read loop we would have nothing to validate against: where the bytes go, how they are framed, and what "correct" means is per-loader knowledge. Rule 1b (r12) therefore rejects headerless blocks *statically*: whatever their encoding shape, the runtime can never take the fast path. Emulating loader families individually (Speedlock, Alkatraz, Bleepload, …) is per-loader reverse engineering — brittle and unbounded; rejected as a direction.

**Conclusion:** for headerless content the only honest fast path is the one that needs no protocol knowledge at all — *run the real machine faster than real time*.

## 3. The proposal analyzed: unthrottle while the signal path plays

### 3.1 Machine-time invariance — why it is mechanically sound

The tape subsystem generates its signal in the **T-state domain**, driven per CPU step:

- `MainLoop::OnCPUStep()` → `Tape::handleStep()` (`mainloop.cpp:306`) — every CPU step advances the pulse cursor (`edgePulseTimings`, offsets in T-states),
- the loader samples the result through `Tape::handlePortIn()` merging into ULA port bit 6,
- frame bookkeeping (`Tape::handleFrameEnd()`) counts *emulated frames*, never wall time.

Turbo mode (`MainLoop::RunFrame` without the absolute-deadline pacing, audio skipped unless `turbo_mode_audio`) removes only the **host-side wait between frames**. Each emulated frame is still 69 888 T-states of exactly the same machine. Consequently every emulation-visible quantity is invariant:

| Quantity | Real-speed load | Turbo load |
|----------|-----------------|------------|
| Memory contents after loading | X | **X (identical)** |
| Emulated frame count of the load | N | **N (identical)** |
| T-states, R register, interrupts seen | Y | **Y (identical)** |
| Load duration *in machine time* (what a program can measure) | D | **D (identical)** |
| Wall-clock duration | D | D ÷ turbo factor (~10–16x) |
| Audio | normal | silent (or skipped) |
| Host CPU | ~idle | one core at 100 % while engaged |

This is strictly stronger fidelity than the trap: the trap's instant load is *detectable in principle* (a program measuring frames-per-block sees ~0 — the reason §9.3 of the fast-tape design mooted a "real-duration pacing" mode for the trap); a turbo load is **undetectable from inside the machine**. Nothing a loader can do — timing checks, duration checks, border-stripe self-tests — can tell warp from real speed.

(The `SetSpeedMultiplier` path is *not* equivalent and is rejected for this purpose — §8.2.)

### 3.2 The real problem: "disable it once we finished loading"

For ROM-standard blocks, "finished" is a precise event (the trapped LD-BYTES invocation completes). For an arbitrary loader, "finished" is not a spec'd signal — and this is why the feature does not exist yet. But the tape subsystem already contains three independent, purpose-built completion detectors:

1. **Read-gap watchdog** — `Tape::handleFrameEnd()` increments `_framesSinceLastRead` every frame without a tape-relevant ULA read; at >150 frames (~3 s of *emulated* time) it freezes playback (`pausePlayback()` — deliberately *not* a terminal stop, so a multi-stage loader that pauses reading while it decompresses/bank-switches loses nothing). This exists precisely to detect "the loader exited" (`tape.cpp:728-742`).
2. **ERR_NR change** — ROM reports break/error immediately → `stopPlayback()` (`tape.cpp:718-726`). Covers SPACE-break aborts in ROM flows.
3. **Natural end-of-tape** — cursor past the last block → `TapePlaybackState::Ended`.

And the symmetric start-side detector already exists too:

4. **Sustained EAR-polling resume** — `_earPollsThisFrame ≥ 256` (non-joystick ULA reads; the keyboard scan's ~8/frame can never trip it) → `ResumePlaybackAfterPoll()` restarts or un-pauses playback for RAM-resident loaders that never touch the ROM `$0564` anchor (`tape.h:27-34`, `tape.cpp:544-547`). The ROM anchor auto-start (`PC == $0564`, `tape.cpp:593-596`) covers ROM flows.

In other words: **the tape object's state machine already moves through exactly the transitions an auto-turbo controller needs to observe.** Playing (from start / anchor / poll-resume) = "loading is happening"; Paused-by-watchdog / Ended / stopped / ERR_NR = "not loading".

### 3.3 Wall-clock expectations

Turbo factor per `docs/emulator/design/core/speed-control.md`: ~500–800 FPS on Apple M1 class hardware → **10–16x**. For DIZZY_X (r12 numbers): total 291.6 s of tape, blocks 0–1 (~11.3 s) are trap-instant anyway, blocks 2–4 are ~280 s of headerless signal → **~18–28 s of host time** with this feature, versus ~280 s today. Not instant like the trap — but the same order as a "please wait" screen, and with bit-exact results.

## 4. Scenario walkthroughs

### 4.1 Happy path — DIZZY_X (Partial verdict tape)

1. User starts the load (Tape Manager Play, or LOAD "" hitting the `$0564` anchor).
2. Blocks 0–1 (header + BASIC body, trap-shaped): LD-BYTES trap consumes them **instantly**, as today — turbo never engages because the trap path never plays audio (state stays Idle → controller idle).
3. The loaded BASIC runs the custom loader; it starts polling EAR in RAM → poll-resume starts signal playback of block 2 → controller **engages turbo** (≤1 frame later).
4. Blocks 2–4 decode through the real loader at 10–16x wall speed; loading stripes flash; audio silent.
5. Loader finishes, jumps into the game; tape is still "playing" but nobody reads it → 150 emulated frames (~0.2–0.3 s wall at warp) → watchdog freezes playback → controller **disengages turbo**. The game's first frames run at warp for those ~0.2 s — imperceptible — then at 50 Hz.

Net: ~20 s wall instead of ~290 s; final RAM byte-identical to a real-speed load.

### 4.2 Multi-stage loader (insult.tap family)

Between stages the loader stops reading EAR while it processes → the gap's first 150 emulated frames still run at warp (decompression, bank switching finish 10–16x faster too) → watchdog freezes tape → **turbo disengages** (with playback Paused there is no signal left to hurry). Next stage's polling resumes playback → **turbo re-engages** automatically. The engage/disengage cycling is free; the poll-resume machinery already moves the state machine exactly this way. (OQ-3 weighs "keep warp until the loader fully exits" instead.)

### 4.3 Interactive multi-load ("press key for part 2")

Loader finishes part 1, prints a prompt, waits for a key. No EAR reads → watchdog pauses tape → turbo off → the machine sits at 50 Hz, screen readable. User presses the key; part 2's loader polls EAR → playback resumes → turbo re-engages. The user never fights a warp-speed prompt.

### 4.4 User aborts / tape ends

- SPACE-break in a ROM flow → ERR_NR changes → `stopPlayback()` → turbo off.
- Last block consumed → `Ended` → turbo off.
- Manual Stop/Rewind/Seek/Eject in Tape Manager → state leaves Playing → turbo off.

### 4.5 User's own turbo usage

The user may legitimately toggle turbo mode by hand (existing menu action). The controller must never disable a turbo it did not enable — §6.2.

## 5. Why this was not the v1 fast-load design

The `2026-08-30` design chose the trap for its first iteration for reasons that remain valid — turbo tape loading **complements**, not replaces, it:

| | LD-BYTES trap | Turbo tape loading |
|---|---|---|
| Speed | instant (0 emulated frames) | 10–16x wall-clock (host-bound) |
| Host cost while active | none | one core at 100 % |
| Coverage | vanilla ROM blocks only | **every** signal-path block (headerless, turbo-timed, pulse streams, broken checksums) |
| Fidelity | exact postconditions, but instant (duration-detectable) | bit-exact and undetectable in machine time |
| Dependencies | ROM signature, decline matrix | existing turbo mode + tape state machine |
| Where it fails | headerless/custom (statically known) | nowhere new — worst case the loader never stops polling EAR (§7) |

The trap stays the fast path wherever it can serve; turbo picks up exactly the blocks rule 1b now marks "no — headerless".

## 6. Design v1

### 6.1 Component: `TapeTurboController`

New class beside the trap: `core/src/emulator/io/tape/tapeturbocontroller.{h,cpp}` (same placement pattern as `tapefastload`). Owned by `MainLoop`, **ticked once per emulated frame end, after `Tape::handleFrameEnd()`** so it always observes post-transition state. It is a pure observer of the existing tape state machine — v1 touches no `Tape` internals:

```cpp
class TapeTurboController
{
public:
    explicit TapeTurboController(EmulatorContext* context);
    /// Call once per frame after Tape::handleFrameEnd(). Evaluates the
    /// matrix below and drives Core::Enable/DisableTurboMode.
    void handleFrameEnd();
    bool IsAutoTurboActive() const;
private:
    bool _autoTurboActive = false;   // turbo we enabled — the only one we may disable
    bool _suppressedThisSession = false; // manual turbo touched it → stand down until playback ends
};
```

Transition matrix (all latencies ≤ 1 emulated frame ≈ 1.3 ms wall at warp / 20 ms at real speed):

| # | Observed condition | Guard | Action |
|---|--------------------|-------|--------|
| E1 | `GetPlaybackState() == Playing` (any origin: user Play, `$0564` anchor, poll-resume) | feature on, no manual turbo, not suppressed | `Core::EnableTurboMode(false)`; `_autoTurboActive = true` |
| E2 | Playing → `Paused` (watchdog freeze) | `_autoTurboActive` | `Core::DisableTurboMode()`; clear flag |
| E3 | → `Ended` / `Idle` (stop, eject, ERR_NR stop, image change) | `_autoTurboActive` | same as E2 |
| E4 | feature toggled off mid-flight | `_autoTurboActive` | same as E2, immediate |
| E5 | user enables manual turbo any time | — | controller stands down without disabling (manual owns; clear `_autoTurboActive`) |
| E6 | user disables manual turbo during a Playing session | — | set `_suppressedThisSession` — the user just rejected warp; no auto re-engage until playback leaves Playing |

Engagement on E1 is unconditional over block content — it does **not** consult `TapeFastLoadPlan`. Rationale: the plan is advisory by contract ("honesty contract", `tape.h` §5.8), and any signal-path playback is by definition content the trap is not serving *right now* (a runtime decline — e.g. checksum-invalid — lands on the signal path exactly like a headerless block, and deserves warp just as much).

### 6.2 Ownership: never fight the user

- The controller only ever disables a turbo **it** enabled (`_autoTurboActive` guard). The user's manual turbo toggle is untouchable.
- Any manual turbo interaction during a Playing session suppresses auto-turbo for the remainder of that session (E5/E6) — otherwise turning warp off would be impossible while a tape plays.

### 6.3 Feature flag

- `Features::kTurboTape = "turbotape"`, alias `ttape` — exactly the `kFastTape`/`ftape` pattern in `core/src/base/featuremanager.h`.
- `features.ini`: `[turbotape] state = on` — default **on**, matching fasttape's default-on philosophy (faster loading is the entire point; every disengage path is automatic, and the menu toggle is the escape hatch). OQ-1 records the alternative.

### 6.4 Surfaces (core → Qt → CLI → WebAPI parity)

| Surface | Change |
|---------|--------|
| Core | `TapeTurboController` + one call in `MainLoop::OnFrameEnd()` after `pTape->handleFrameEnd()` |
| Qt | Machine menu checkable **"Turbo Tape Loading"** — the Fast Tape Loading r3 pattern (menu action → signal → `MainWindow` handler → `setFeature`); `updateMenuStates()` syncs the check. No pause, no reset — takes effect at the next evaluation |
| CLI | feature toggle through the existing feature-manager surface (same as `fasttape`); `settings turbo` remains the *manual* turbo control, unchanged |
| WebAPI | rides `GET/POST /api/v1/emulator/{id}/features` (`features_api.cpp`) — no new endpoint; optional `turboActive` field in the tape state document for observability (OQ-4) |

### 6.5 Coexistence: TTD, recording, frame refresh, pause

- **TTD:** turbo does not invalidate sessions (only `SetSpeedMultiplier` does, `emulator.cpp:573-582`). Emulated frame counts of a load are identical, so the journal of a warp load is byte-for-byte the same size as a real-speed load. Scrubbing into a warp-loaded region behaves exactly like scrubbing into a real-speed load.
- **Recording:** the recording path explicitly supports turbo ("captures every emulated frame for correct timing", `mainloop.cpp:394`). An AVI of a warp load plays back as a fast-forward — correct and expected.
- **Frame refresh:** `NC_VIDEO_FRAME_REFRESH` is posted for every emulated frame (`mainloop.cpp:438`), so at 500–800 FPS consumers receive 10–16x more notifications. Verification item I-3: confirm Qt screen widgets coalesce/throttle paints (the wall-clock difference must not translate into 16x paint cost).
- **Emulator pause while auto-turbo:** no frames run; the flag stands; on resume warp continues until a tape transition ends it. No user-visible surprise (the pause *is* the interaction).

## 7. Risks, limitations, mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Loader never stops polling EAR after finishing (EAR-driven input, polling game loop) | watchdog never fires → warp persists into gameplay | end-of-tape still fires when the tape exhausts (`Ended`); manual escapes: feature/menu/Stop; possible future cap (OQ-3 variant) |
| Host burn / battery during warp | one core at 100 % while engaged | inherent to turbo mode; every normal path ends it automatically; engagement is bounded by tape content |
| Early program execution races at warp (last read → watchdog freeze ≈ 150 emulated frames) | ~0.2–0.3 s wall of warp-speed game intro | imperceptible; do **not** shrink the watchdog threshold — it is emulated-time by design and multi-stage loaders rely on its leniency |
| Frame-refresh flood at 500–800 FPS | UI paint overhead | I-3 verification; coalesce if needed |
| Warp surprises a user watching a loading screen draw | cosmetic | feature toggle; OQ-1 alternative (off-by-default / explicit warp-Play button) |

## 8. Alternatives considered

1. **Per-loader custom-loader traps** (Speedlock/Alkatraz/…) — rejected: per-loader reverse engineering, brittle against tape rips and variants, unbounded maintenance; the antithesis of the trap's single-protocol contract.
2. **`SetSpeedMultiplier` instead of turbo mode** — rejected for v1: it changes the emulated machine (2–16x bound), and it **invalidates TTD sessions** ("speed-multiplier-change"). Loader correctness under a dilated machine is *plausible* (tape edges live in the same T-state domain; `tape.cpp` even scales EAR-DAC placement by the multiplier) but unverified — it would need its own differential gate before ever shipping (OQ-2).
3. **Trap pacing mode** (`2026-08-30` §9.3 — make the *instant* trap consume realistic duration) — a different problem, orthogonal to this feature.
4. **Do nothing** — real-speed fallback stays (status quo after r12); rejected because the machinery for warp already exists and the user experience gap is the largest remaining tape friction.

## 9. Test plan

### 9.1 Unit — `TapeTurboController_Test` (`ClassName_Test` pattern, CUT pattern like `tapefastload_test`)

- Matrix rows E1–E6 against a real `Tape` object driven through its public API (`StartPlaybackAtCursor` / `pausePlayback` / `ResumePlaybackAfterPoll` / `stopPlayback`) plus a mockable turbo sink; assert engage/disengage/ownership/suppression per row.
- Feature off → never engages; feature toggled mid-flight (E4) → disengages within one tick.
- Watchdog path: run a Playing tape with no port reads for >150 frames → controller disengages exactly when `Tape` enters `Paused`.

### 9.2 Integration — differential (the money test)

Synthetic **short** headerless TAP (~2–3 s of machine time — DIZZY_X's 280 s is a live-gate fixture, not a CI one):

1. Run A: `turbotape` on; run B: off. Both drive the same real custom-loader shape (a tiny RAM poller reading EAR — the `tapeloading_integration_test` harness pattern).
2. Assert final RAM equal (differential contract, same as the trap's §12.2-2) and **emulated frame counts equal** (machine-time invariance, §3.1).
3. Assert wall-clock FPS ratio A ≥ 4x B (conservative floor; expected 10–16x) — proving the warp actually happened.

### 9.3 Integration — multi-stage resume

Two poll-bursts separated by a read gap >150 frames (poll-resume cycle): assert engage → disengage → **re-engage**, and that `_suppressedThisSession` is not set (no manual interaction happened).

### 9.4 Live gate

- DIZZY_X via WebAPI with the feature on: load completes, wall time ≪ 60 s (expect ~20 s), trap consumes blocks 0–1, signal path serves 2–4, final `GET /tape` state sane; Tape Manager title/state consistent.
- I-3: during warp, observe UI responsiveness and paint cost (Instruments/QML profiler or CPU% delta) at 500–800 FPS refresh rate.
- Full suite + zero-warning build (standard exit gate).

## 10. Open questions

- **OQ-1 — default on or off?** Proposed `on` (§6.3). Alternative: off by default + an explicit Tape Manager "Play (warp)" button next to Play, making warp a deliberate act for the nostalgia case (watching a loading screen draw at real speed). Decide after dogfooding.
- **OQ-2 — stacking multiplier × turbo** (dilated machine on top of unthrottle): potential ~40–100x+, but changes the machine and invalidates TTD; blocked on a dedicated differential gate proving loaders decode correctly at 4x/16x machine dilation.
- **OQ-3 — hold warp through processing gaps?** v1 disengages on watchdog pause (E2). Alternative: stay at warp until a *terminal* exit (Ended / ERR_NR / user stop), pausing only the tape. Trades battery/host burn for fewer warp/normal transitions on multi-stage loaders.
- **OQ-4 — observability:** `turboActive` in the WebAPI tape state document + a small warp badge in Tape Manager / status bar while `_autoTurboActive`.

## 11. References

- `docs/inprogress/2026-08-30-fast-tape-loading/design.md` — LD-BYTES trap, decline matrix §6.2, non-goals, §9.3 pacing note, §9.5 multi-stage loaders
- `docs/inprogress/2026-09-01-tape-manager/design.md` — §5.8 fast-load pre-analysis (advisory contract), r8/r10/r11/r12 (headerless display, pairing, rule 1b)
- `docs/emulator/design/core/speed-control.md` — turbo mode vs speed multiplier, FPS/CPU data
- `core/src/emulator/io/tape/tape.{h,cpp}` — playback state machine, read-gap watchdog, poll-resume, `$0564` anchor, consumption cursor
- `core/src/emulator/io/tape/tapefastload.{h,cpp}` — the trap this feature complements
- `core/src/base/featuremanager.h` — `kFastTape`/`ftape` flag pattern
- `core/automation/webapi/src/api/features_api.cpp` — the `/features` surface the flag rides

## 12. Revisions

### r2 — v1 implemented, gated and verified live (2026-09-04)

**Shipped exactly as §6 designed** — no matrix changes, no renames:

- `TapeTurboController` (`core/src/emulator/io/tape/tapeturbocontroller.{h,cpp}`), owned by `Core` beside `TapeFastLoad` (`_context->pTapeTurboController`), ticked from `MainLoop::OnFrameEnd()` immediately after `Tape::handleFrameEnd()` — every transition observed in the frame it happens.
- `turbotape` / `ttape` feature (default **on**, OQ-1 resolution), features.ini `[turbotape] state = on`.
- Surfaces: Qt Machine menu "Tur&bo Tape Loading" (checkable, synced in `updateMenuStates`), CLI `setting turbo_tape on|off` + tape info line, WebAPI `io_acceleration.turbo_tape` + `setting turbo_tape` + `turbo_tape` in the tape state document — plus the automatic `/features` enumeration.
- Tests: 10 unit (`TapeTurboController_Test`, full E1–E6 + destructor-releases-owned-turbo) + 3 integration (`TapeTurbo_Integration_Test`: live lifecycle engage → watchdog disengage → poll-resume re-engage; headerless tail under warp with E3 end-of-tape; E5/E6 ownership under the live MainLoop). Suite 1956/1956, zero warnings.

**Live gate (§9.4) — DIZZY_X_CHEFRANOV_VALENTIN.tap on 48K:** `LOAD ""` via keyboard API → whole 355.84 s tape (blocks 0–5, including all four headerless blocks the trap statically rejects) loaded in **~12 s wall-clock end-to-end** (~30x; the remaining ~341 s of signal ran in 8 s, ~43x), ending cleanly at `Ended`/cursor 6 with the machine back at 50 Hz. The original complaint — "we still load only basic and custom tape loader block, then fall out of fast load" — is resolved.

**Facts sharpened while testing (refine §3.2, worth keeping):**

- The ERR_NR completion detector fires only on *error* reports: the ROM stores report code − 1, so a successful `0 OK` leaves ERR_NR at $FF — unchanged from the value captured at playback start. A successful ROM load keeps the tape rolling (watchdog/editor scan territory), exactly like a custom loader.
- Test-authoring corollary that cost an afternoon of mystery: a Program header with autostart `0x0000` (not `$8000`) makes the ROM auto-RUN line 0 after loading → error report → ERR_NR stop consumes the in-flight block → `Ended` one frame before `ProgramLoaded` observably turns true. Synthetic TAP headers must use `$8000` (the sibling fast-load suite's convention).
- OQ-4 stays open: the *feature* is now observable everywhere, but a live `turboActive` runtime field (and UI badge) is still future work — the live gate proved warp indirectly via wall-clock ratio.
- Out-of-scope bug found during the live gate (pre-existing, separate fix): WebAPI `POST keyboard/type` with `tokenized:true` produces `LET OAD …` on 48K — keyword tokenization does not account for the editor's K-cursor key semantics; raw `keyboard/tap`/`combo` (e.g. `j` = LOAD, `sym_shift`+`p` = quote) works correctly.

### r3 — Tape Manager marks the turbo path (2026-09-05)

Hybrid loads (trap serves the ROM-standard prefix, warp carries the custom-loader tail) were invisible in the Tape Manager: the FAST column marked only trap-served rows (`⚡`), so the whole turbo-fed signal path read as plain real-time (`·`).

- `TapeUiSnapshot` gains the per-tick `turboTapeEnabled` flag (mirrors `fastTapeEnabled`), filled by the binding's frame-end hook from `Features::kTurboTape`.
- `TapeBlockTableModel` FAST column is now three-state: `⚡` trap-served · `⏩` signal path at warp (turbo tape on) · `·` signal path real-time. `SetTurboTapeEnabled()` repaints the column when the toggle flips mid-session; the flag survives `Rebuild()` so a catalog refresh cannot flicker it. Tooltips add a second line (`Turbo tape: yes — signal path, plays out at warp`) on `⏩` rows.
- Exemptions stay honest: trap-served rows never get the turbo marker (the trap never plays them), and `Unplayable` blocks keep `·` — no speed delivers them.
- Badge suffix in the Tape Manager (` · ⏩ turbo tape: signal path at warp` when on): the plan's verdict line counts the signal path as real-time, which would understate a hybrid load.
- OQ-4 scope note: this marks the *feature toggle*, not the `_autoTurboActive` runtime engagement — the live `turboActive` field/badge remains open.

