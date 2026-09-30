# Temporal Effects Manager

**Created:** 2026-09-27
**Status:** draft for review; step 1 (the `dlss` mode in the core, GUI dialog) implemented - section 7
**Requirements:** [requirements.md](requirements.md) R-28
**Replaces:** GUI-side `FrameHistory` / `TemporalEffectsDialog`
(`unreal-qt/src/widgets/framehistory.*`, `unreal-qt/src/ui/temporaleffectsdialog.cpp`)

---

## 1. Idea

All processing that combines several emulated frames into one displayed frame
lives in **one core component**, switched on by **one feature** and configured
by choosing a **mode** — from the simple whole-frame blend that exists today up
to ZX DLSS with its sub-modes.

It runs at the emulated frame end, before presentation, and writes into the
output framebuffer (R-25), so every consumer gets the result.

---

## 2. Modes

| Mode | What it does | Origin |
|---|---|---|
| `off` | output = raw frame | — |
| `blend` | whole-frame blend over 2–5 frames, equal or exponential weights, any mixer from the store | today's `FrameHistory`, moved into core |
| `adaptive` | per-pixel A,B,A / static-skip / period-3 test, 2- or 3-frame blend | the koval / Xpeccy algorithm ([prior-art §2](prior-art.md#2-the-koval-algorithm-in-detail)); a cheap baseline and comparison reference |
| `dlss` | ZX DLSS GigaScreen: segment classification, masks, motion compensation | this design |

`dlss` sub-modes:

| Sub-mode | Behavior |
|---|---|
| `observe` | analysis + metadata + overlays; picture unchanged |
| `blend` | runtime analysis, past frames only |
| `lookahead` | runtime analysis with predicted future frames (Look-Ahead Manager) |
| `auto` (default once packs exist) | recognized software → pack (hybrid or pack-only, as the pack says); otherwise `lookahead` if available, else `blend` |

---

## 3. Structure

```mermaid
flowchart TD
    FE[Emulated frame end] --> TEM{Temporal effects<br/>feature on?}
    TEM -- no --> ZERO[Nothing captured<br/>zero cost]
    TEM -- yes --> CAP[Capture raw frame<br/>+ plane B if the mode needs it]
    CAP --> HIST[(Shared history ring<br/>raw planes only)]
    HIST --> MODE{Mode}
    MODE -- off --> RAW[copy raw]
    MODE -- blend --> BL[Whole-frame blend]
    MODE -- adaptive --> AD[Per-pixel adaptive]
    MODE -- dlss --> DL[ZX DLSS pipeline]
    MX[Mixer store] --> BL
    MX --> AD
    MX --> DL
    LAM[Look-Ahead Manager] --> DL
    MM[Metadata Manager] <--> DL
    BL --> OUT[Output framebuffer]
    AD --> OUT
    DL --> OUT
    RAW --> OUT
```

- **One history ring** for all modes (raw planes, depth = the maximum any mode
  needs); cleared on mode change, reset, snapshot load, TTD seek, palette or
  machine change.
- **One mixer store** for all modes — the simple blend gains linear-light and
  the other mixers for free.
- **One backend choice** (scalar / SIMD + threads / GPU) shared by all modes.
- **Services on demand:** the Look-Ahead and Metadata managers are started
  only when the selected mode uses them; they have no separate switch for this
  use.
- Each mode is an implementation of one interface:

```cpp
class ITemporalEffect {
public:
    virtual ~ITemporalEffect() = default;
    virtual TemporalNeeds needs() const = 0;          // history depth, plane B, look-ahead, metadata
    virtual void reset() = 0;
    virtual void process(const TemporalFrameContext& ctx,   // history, predicted frames, meta
                         FramebufferDescriptor& output) = 0;
};
```

---

## 4. Configuration and surfaces

- **Feature flag:** `[temporaleffects] state = on|off, mode = off|blend|adaptive|dlss`
  in `features.ini`; sub-mode and parameters in the emulator settings.
- **GUI:** one "Temporal Effects" dialog replacing `TemporalEffectsDialog`:
  mode selector, per-mode parameters, mixer selector and parameters, backend,
  diagnostics toggles (class-map overlay, side-by-side).
- **Automation:** `GET/PUT …/temporal/mode`, `…/temporal/params`,
  `…/temporal/mixer`, plus the DLSS-specific capture and statistics methods
  (design-analysis §11). Same methods in MCP, CLI, Lua, Python.

---

## 5. Migration of the existing GUI blend

1. Implement `blend` mode in core with the same parameters (history 2–5,
   equal/exponential, decay) and the `srgb-mean` mixer.
2. **Parity test:** for the same input frames, core `blend` output equals the
   old `FrameHistory::getBlendedFrame` output.
3. Switch the GUI dialog to the core mode; keep the old settings keys readable
   and map them to the new ones.
4. Remove `FrameHistory`, `FrameHistoryGL` and `TemporalEffectsDialog` from
   `unreal-qt`.

Until step 4, the old GUI blend and the core temporal effects cannot be on at
the same time (double blending).

---

## 6. Tests

| Suite | Checks |
|---|---|
| `temporaleffectsmanager_test.cpp` | mode switching resets history; feature off captures nothing; needs() drive service start/stop |
| `blend` parity | equals old `FrameHistory` output for the same frames |
| `adaptive` | matches the koval reference behavior on synthetic A,B,A / period-3 / static inputs |
| All modes | run on every synthetic case and golden clip; the review report shows all modes side by side |

---

## 7. Step 1 as built (2026-09-30)

The `dlss` mode runs in the core; the GUI blend (`FrameHistory`) stays where
it is for now, and the two are never on together.

| Part | Where |
|---|---|
| Algorithms | `core/src/emulator/video/zxdlss/` (moved from `tools/verification/zxdlss/algo`; the tools link the core). Built-ins are registered explicitly in `registry.cpp`: the core is a static archive, and a self-registering object file would be dropped by the linker |
| Temporal effect | `core/src/emulator/video/temporaleffects.{h,cpp}` - `TemporalEffects`: a worker thread, one job per latched frame |
| Present queue | `Screen`: 12 slots (was 4), a serial per slot, `SetTemporalAlgorithm(name)` / `GetTemporalAlgorithm()` / `GetTemporalStats()`, `GetEffectivePresentDelayFrames()` |
| Audio | `SoundManager::setOutputDelayFrames(n)`: a whole-frame delay line after the recording and analyzer taps, before the DRC resampler |
| GUI | Tools -> Temporal Effects: Effect = Frame blending / ZX DLSS de-flicker, algorithm choice (default `mod-tpgwafsd`), live status |

**Per frame.** `Screen::LatchFramebuffer` (emulation thread, frame end) copies
the frame into its present slot, then hands the frame's plane B to the worker
and returns. The worker runs the algorithm; its output is the frame pushed
`delay()` frames earlier, and it writes that output over the frame's present
slot (found by serial). The emulation thread never waits for the algorithm.

**Delays.** While the effect is active the present queue serves the frame
`delay() + 1` frames back (the look-ahead + one frame for the worker to finish):
7 frames for `mod-tpgwafsd`, about 143 ms on a Pentagon. The configured A/V
delay (`[VIDEO] AVSyncDelayFrames`, auto = 2) already covers 2 of them; the
audio sent to the device waits the other 5 (`setOutputDelayFrames(7 - 2)`), so
picture and sound stay in sync. The delay line passes one frame of samples per
frame whatever its length - silence while it fills, the oldest frames dropped
when it shrinks - so the DRC ring occupancy and its controller see no change.

**Restarts.** The algorithm needs every frame in order. It restarts (and the
queue shows raw frames until its look-ahead refills) after a TTD seek
(`FlushAndPresentFramebuffer`), a frame size change, a new algorithm, or a
worker 3 frames behind.

**Palette.** Every frame carries the emulator's live palette
(`Screen::GetRGBAPalette16`, `FrameInput::palette`); the algorithm mixes in it and
rebuilds its mix tables when it changes. It has no palette of its own. Clips
exported for the tools carry it in `clip.json` ("palette16"); older clips have it
recovered from their RGBA frames.

**Not a ZX screen.** Plane B exists only with the features `zxdlss` +
`screenhq` and a ZX raster (352 x 288, Pentagon overscan 384 x 304); otherwise
the effect is inactive and says why (the dialog switches both features on).

**Automation.** One status report and one switch (`core/automation/temporalstatus.h`)
on every surface: CLI `video temporal [status|list|off|<algorithm>]`; WebAPI
`GET /api/v1/emulator/{id}/video/temporal` and `PUT` (or `POST`) with
`{"algorithm": "<name>"|""}` (400 with the valid names on an unknown one);
Lua / Python `video_temporal()` and `video_temporal_set(name)`; MCP
`capture_media` actions `temporal_status` / `temporal_set` (through the
WebAPI route, with a one-line summary). Status fields: `algorithm`, `active`,
`inactive_reason`, `video_delay_frames`, `video_delay_ms`,
`audio_extra_delay_frames`, `processed`, `written`, `late`, `restarts`,
`last_ms`, `average_ms`, `algorithms` (all but `raw` and `-ref`) and
`default_algorithm` (`mod-tpgwafsd`).

**Open before this reaches master:**
- the algorithm starts its threads on every parallel stage (no pool): cost to
  measure on an idle machine (the tool measured 3-16 ms/frame on a loaded one);
- the Temporal Blending menu check mark does not follow the dialog;
- a lower look-ahead would shrink the delay: research in
  `tools/poc/019-zxdlss-gigascreen/out/` (causal lookahead 0-2).
