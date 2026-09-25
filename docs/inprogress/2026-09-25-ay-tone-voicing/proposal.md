# Proposal: bring back the `4c49fb6` AY bass balance with a "voicing" stage

**Origin:** analysis from an earlier agent session (2026-09-25), originally in Russian. Translated
here and expanded with references, commit IDs and independent checks.
**Status:** input to the design; the review and the final design are in
[ay-tone-voicing-tdd.md](ay-tone-voicing-tdd.md).

How to read this file:

- The **translated text** is the original argument, kept faithful to the source.
- **Detail** blocks were added in translation: code locations, commit IDs, math, and whether each
  number was reproduced.
- **Part II** (§7–§14) extends the proposal into a complete feature: EQ profiles, core pipeline wiring, settings and
  persistence, feature gates, GUI clients (unreal-qt, videowall) and automation.

---

## 1. The question

> After the DC filter on AY/TS was fixed, the bass became brutal, even without punch. Do the
> filters need recalculating? How do we get back the subjective sound of commit `4c49fb6`?

**Detail: commit references**

| Commit | Date | What it is |
|:--|:--|:--|
| `4c49fb629d43` (`4c49fb6`) | 2026-09-23 | *fix(sound): wire SoundDrive mode-1 ports, fix mono centering and mode-switch clicks.* The **reference sound**. It is not a filter commit; it is just the last build the user listened to before the change. |
| `d90421bbeed3` (`d90421bb`) | 2026-09-24 | Covox/SoundDrive refactor. It is the parent of `45812176` and the last commit with the old AY filter. **Better A/B reference than `4c49fb6`** (same AY filter, closer code). |
| `4581217615720` (`45812176`) | 2026-09-24 | *fix(sound): replace the AY moving-average DC remover with a one-pole coupling high-pass.* **The change being reported.** Adds `FilterDCBlocker` and switches `SoundChip_AY8910` to it at 5 Hz. |
| `4c0b7153d9c2` (`4c0b7153`) | 2026-09-24 | TSFM: continuous FM timeline, board output coupling. It uses `FilterDCBlocker` for the FM input coupling (`kTsfmFmCouplingHz`) and does not touch the AY/SSG path. |
| `ec66d3bc51d8` (`ec66d3bc`) | 2026-09-24 | Mixer sample counting in T-states. Adds `FilterDCBlocker::settle()` and flushes stale output on LQ→HQ/resume. No effect on tone. |
| `6ed6d4c0b2f8` (`6ed6d4c0`) | 2026-09-25 | Timed AY writes, polyphase decimator, exact TTD restore. The decimator is a low-pass near Nyquist and does not affect the bass band. |
| `e543f7f2` | 2026-07-14 | *Native clock AY rendering with matched FIR decimator.* Introduced `AudioCharacterChain` and the AY punch preset, with punch **on** from the start. |
| `508c1b7c` | 2026-07-14 | *Separate AY/beeper audio chains, extended room levels.* Split the single chain into AY and beeper chains; AY punch stayed on. |
| `12808df2` | 2023-06-05 | *Pending sound changes.* Introduced `FilterDC` (the moving-average DC remover). |

So in the AY bass band, `4c49fb6` → HEAD differs **only** by `45812176`. The other sound commits in
between change timing, decimation or the FM path.
(`git log 4c49fb6..HEAD -- core/src/emulator/sound core/src/common/sound`)

## 2. Short answer (translated)

> No: the filters inside the chip don't need recalculating, and the old filter shouldn't come back
> either. The old `x − MA₁₀₂₄` coloured the sound by itself, and the new 5 Hz coupling simply
> removed that colouring. To get the `4c49fb6` sound back, reproduce that colouring as a separate,
> switchable stage. I measured exactly what changed and fitted a filter to the old frequency
> response.

**Detail: the two filters**

- **Old** `FilterDC<double>` (`core/src/common/sound/filters/filter_dc.h`, removed from the AY
  in `45812176`): `y = x − mean(last 1024 samples)` at the AY generator rate of
  218.75 kHz (1.75 MHz / 8, `SoundChip_AY8910::GENERATOR_RATE`). Window length is 1024 / 218 750 = 4.68 ms.
  Its transfer function is `H(f) = 1 − MA(f)`, where MA is a 1024-tap boxcar with nulls at
  multiples of fs/N = 213.6 Hz. Below the first null the boxcar's phase lag makes `1 − MA` rise above
  1, and that is the hump. Well below it, `1 − MA` falls like a high-pass.
- **New** `FilterDCBlocker` (`core/src/common/sound/filters/filterdcblocker.h`), a one-pole RC
  high-pass `y[n] = a(y[n−1] + x[n] − x[n−1])`, `a = RC/(RC+dt)`, at
  `OUTPUT_HIGHPASS_HZ = 5.0` (`soundchip_ay8910.h`). It is flat to within 0.1 dB above ~30 Hz.

## 3. What changed compared with `4c49fb6` (translated)

> Measured with the same filters that the chip uses at 218.75 kHz:
>
> - **Low fundamentals got louder:** +4.7 dB for a square wave at 41 Hz (E1), +2.5 dB at 55 Hz and for
>   envelope-sawtooth bass.
> - **A 1.5–1.9 dB dip in the 110–160 Hz range.** The old filter had a +2 dB hump there, and now it is
>   gone. The bass got lower and heavier, not tighter.
> - **The main cause is volume "barrels".** The AY DAC is unipolar, so every per-frame volume step is
>   a jump in the DC level. The old filter removed that jump within 4.7 ms. The new 5 Hz HPF passes it
>   as an infrasonic thump. Energy in the 1–35 Hz band relative to the rest of the spectrum went from
>   −17.3 dB to −3.3 dB. On headphones or with a subwoofer, that is the "brutal" bass.
> - **Peaks, on the other hand, went down by 2–5 dB.** The old filter acted almost like a
>   differentiator: on a steady square wave it produced overshoots at the edges.

**Detail: independent reproduction** (analytic responses plus time-domain simulation,
2026-09-25; tables in [TDD §2.2](ay-tone-voicing-tdd.md#22-independent-check-of-the-numbers))

| Claim | Reproduced? | Our number |
|:--|:--|:--|
| +4.7 dB at 41 Hz | ✅ | old filter −4.69 dB, new −0.06 dB |
| +2.5 dB at 55 Hz | ✅ | old −2.50 dB, new −0.04 dB |
| hump +2 dB, 110–160 Hz | ✅ | old peaks at +2.00 dB, 139 Hz |
| 1–35 Hz energy −17.3 → −3.3 dB | ✅ in shape | Synthetic 110 Hz square with a stepping volume every 20 ms: old −21.4 dB, new −6.5 dB. Absolute values depend on the test signal; the ~15 dB jump matches (the proposal measured +14 dB). |
| peaks 2–5 dB lower | ✅ | 41 Hz square: old 0.999, new 0.594 (−4.5 dB); 110 Hz: 0.972 → 0.536 (−5.2 dB); 220 Hz: unchanged |

A useful way to see it: the "brutal bass" is **not** mainly louder notes. It is the
infrasonic thump from volume steps that the old filter hid. Small speakers can't reproduce it,
which is why it shows up mostly on headphones and subwoofers.

## 4. How to get the `4c49fb6` sound back (translated)

> The closest match to the old curve (smoothed to 1/3 octave, 20 Hz – 1 kHz) is:
>
> - **1st-order HPF at 64.2 Hz**, plus
> - **peaking EQ at 106.9 Hz, +3.06 dB, Q = 1.0**.
>
> Mean error 0.39 dB, maximum 1.3 dB. On test signals the per-band levels match the old ones to within
> 0.5 dB, and the infrasonic share of the "barrels" also comes back (−17.6 dB).
>
> If something simpler is wanted, there are two weaker options. A single HPF at 55 Hz gives 1.24 dB
> mean error but no hump. A 2nd-order HPF at 39 Hz with Q = 0.62 gives 0.99 dB.

**Detail: checks and coefficients**

- Fit error with **no** smoothing: mean 0.34 dB, max 1.5 dB (20 Hz – 1 kHz, 400 log-spaced
  points). That is consistent with the smoothed 0.39 / 1.3 dB.
- Infrasonic ratio with the fit: −21.7 dB against the old filter's −21.4 dB on the same synthetic
  signal (proposal: −17.6 vs −17.3). The fit brings it back to within 0.3 dB either way.
- Peak level with the fit is back at the old filter's level (41 Hz: 1.014 vs 0.999), so it also
  brings back the edge overshoot, and headroom needs checking (TDD §4.8).
- Above ~210 Hz the old filter had ±0.2–0.5 dB comb ripple (boxcar nulls every 213.6 Hz). The fit
  leaves it out on purpose.
- Formulas (bilinear HPF1 + RBJ peaking biquad) are in TDD §4.1. The fit targets the analog-ish
  curve, so it can run at the core rate (44.1–192 kHz) instead of 218.75 kHz. All its features are
  far below Nyquist.

## 5. Where to put it (translated)

> - **A separate "voicing" stage in `AudioCharacterChain`**, at core rate, in `_ayChain0/1`, before
>   punch. The punch presets were tuned by ear on the already-trimmed bass, and the punch envelope
>   reacts to the full range. With voicing in front of punch, the presets stay valid. If voicing is
>   off, punch will probably need retuning.
> - **Only for AY and the SSG in TurboSound.** The beeper and FM never went through the old
>   `FilterDC`, so leave them alone.
> - **Keep the 5 Hz in the chip model.** It is the physical coupling. Without it the comb filtering
>   and the delayed step at the exit of the averaging window come back.

**Detail: code locations**

- Chains: `SoundManager::_ayChain0/_ayChain1` (`core/src/emulator/sound/soundmanager.h`). They are
  set up in the `SoundManager` constructor (AY punch preset, punch **on**) and run in
  `SoundManager::handleFrameEnd` on `getChipBuffer(0/1)`, **only when `isHQActive()`**. They are also
  reset every time HQ comes back (`_chainsBypassed`). **The review changed the placement because of
  this** (TDD §0, change 1).
- Punch preset `AY`: `edgeBlend 0.03`, `transBoost 0.1`, release 0.9995 (44.1 kHz referenced),
  `core/src/common/sound/filters/audio_character_chain.h`. The claim that it was "tuned on the trimmed
  bass" matches history: punch has been on for AY since `e543f7f2` (2026-07-14), and the old
  `FilterDC` was in place from `12808df2` (2023) to `45812176`.
- TSFM SSG: `TsfmChip::ssg` is the same `SoundChip_AY8910` class, so it gets the same 5 Hz
  blocker and the same chip buffers → `_ayChain0/1`. The FM streams (`getFmBuffer`) go through
  `_fmChain0/1` (punch/room off, TSFM design §7.2).
- The "delayed step" issue: the BW Demo music start, where the player's R13 write restarts an old
  envelope for ~0.3 ms. With `x − MA`, that burst came back as a step one window (4.68 ms) later
  (`45812176` commit message).

## 6. Which option is "right" (translated)

> With a coupling of a few hertz, a real board also sends those thumps to the line output. The
> familiar old balance is more like how people heard the music through a TV or a small amplifier. So
> I'd make voicing with presets: "Flat" (like the hardware) and "Legacy 4c49fb6", and maybe later
> "TV speaker".

**Detail: hardware basis**

- `45812176` notes the board's SSG coupling is even lower (~0.34 Hz). 5 Hz was chosen as the
  point below which lower cutoffs add no 30–150 Hz content on real music, only slow DC wander that
  costs mix headroom. So "Flat" is, if anything, slightly *less* bass-heavy than the real line
  output.
- A TV speaker or small amplifier rolls off steeply below ~100–150 Hz and cannot reproduce
  infrasonic thumps at all. The old filter's response happens to be a milder version of that. The
  eventual `tv` preset (TDD §10, phase 3) would be a steeper version of the same idea.

---

# Part II — Extended proposal: from filter to feature

The original analysis stops at "which filter, and where". The sections below add everything else
needed to ship it: EQ profiles, the core pipeline wiring, settings and persistence, feature gates,
GUI clients, and automation. They already include the corrections from the review
([TDD §0](ay-tone-voicing-tdd.md#0-verdict-does-the-proposal-make-sense)); where one differs
from the original text, it says so. The TDD has the full detail, and each section links to it.

## 7. EQ profiles

### 7.1 Profile model

A profile is a fixed target curve made of at most three sections, applied in this order:

```
high-pass (order 1 or 2)  →  peaking EQ (optional)  →  low-pass (order 2, optional, reserved for tv)
```

Profiles are rows in a constant table in `filtervoicing.h`. Adding a profile means adding one row
and one enum value. No code path changes.

| Field | Type | Meaning |
|:--|:--|:--|
| `id` | string | Stable ID for settings, ini, WebAPI, QSettings (`flat`, `classic`, `tv`) |
| `aliases` | string list | Also accepted on input (`legacy` → `classic`) |
| `label` / `tooltip` | string | Plain-language UI text |
| `hpfOrder`, `hpfHz`, `hpfQ` | 0/1/2, Hz, Q | 0 = no high-pass. Q only for order 2 |
| `peakHz`, `peakDb`, `peakQ` | Hz, dB, Q | `peakDb = 0` = no peak |
| `lpfHz`, `lpfQ` | Hz, Q | 0 = no low-pass |
| `visible` | bool | Shown in UIs and `allowed` lists. `tv` stays hidden until tuned. A hidden ID is **rejected** on every user-facing input (ini, WebAPI, CLI, Lua, Python, QSettings): otherwise WebAPI would accept `tv` and silently play `flat`. |

### 7.2 Profiles

| ID | UI label | Curve | Status | Purpose |
|:--|:--|:--|:--|:--|
| `flat` | Flat (like real hardware line out) | none; exact bypass, output bit-identical to master | v1 | Accurate output, analysis, people who want the thumps |
| `classic` | Classic (softer bass, as in earlier versions) | HPF1 64.2 Hz + peak 106.9 Hz / +3.06 dB / Q 1.0 | v1, **default** | The `4c49fb6` balance (§4) |
| `tv` | TV speaker | starting point for listening: HPF2 ~130 Hz Q 0.7 + LPF2 ~6 kHz Q 0.7 | phase 3, hidden | How the music sounded on a TV set |

Reference fits that are **not** profiles: HPF1 55 Hz (1.24 dB mean error, no hump) and HPF2
39 Hz Q 0.62 (0.99 dB). They are pinned in tests only, so the analysis stays reproducible.

**No user-editable "custom" EQ in v1.** The goal is to restore one known balance, not to ship an
equaliser. The table-driven model keeps a later custom profile cheap. It would be one more row
whose values come from settings.

### 7.3 Scope of profiles

- Applied to: AY-3-8910 / YM2149 (the legacy TurboSound pair) and the SSG half of TSFM. Both come
  out through `ITurboSoundDevice::getChipBuffer(0/1)`.
- Not applied to: beeper, Covox/SoundDrive, TSFM FM (`getFmBuffer`), and later General Sound and
  MoonSound. None of them ever went through the old `FilterDC` (§5). The class can be reused if
  one of them needs a profile later.
- One profile per emulator instance, the same for both AY chips.

## 8. Core pipeline wiring

**Changed from the original text (§5):** voicing does **not** go inside `AudioCharacterChain`.
The chain runs only in HQ and is reset whenever HQ comes back
(`SoundManager::handleFrameEnd`, `_chainsBypassed`), so the bass would change whenever Sound HQ is
toggled or turbo mode ends. The stage keeps the order the proposal asked for (before punch) but
lives outside the chain.

```
SoundChip_AY8910::updateMixer()  @218.75 kHz
   └─ FilterDCBlocker 5 Hz              (unchanged, the physical coupling)
   └─ decimator → core rate
getChipBuffer(0/1)
   └─ FilterVoicing  _ayVoicing[0/1]    NEW: runs in HQ and LQ
   └─ AudioCharacterChain _ayChain0/1   punch → room, HQ only (unchanged)
mixer → recording/analyzer taps → DRC → device
```

| Aspect | Decision | TDD |
|:--|:--|:--|
| Class | `FilterVoicing`, header-only, `core/src/common/sound/filters/filtervoicing.h` | §4.1 |
| Precision | `double` coefficients and state; int16 in/out with saturation; denormal flush | §4.1 |
| Coefficients | bilinear HPF1/HPF2, RBJ peaking/low-pass, fully normalised by `a0`, feedback terms subtracted. Re-derived in `SoundManager::applyCoreRate()` for 44.1–192 kHz. State is **kept** across a rate change (the chains reset; the difference is intentional, since clearing would pass a step) | §4.1, §4.3 |
| When it runs | sound on and synthesis not suppressed. Independent of `soundhq` and of the turbo LQ override | §4.5 |
| `flat` | early return. Output is bit-identical to today | §4.5 |
| Reset | on `SoundManager::reset()`. **Not** on HQ return. AY↔FM needs no hook: the device exists only in the `SoundManager` constructor, so a switch rebuilds the stack | §4.2 |
| Thread safety | setters write an atomic "requested profile"; the emulation thread applies it at the next frame boundary | §4.4 |
| Switching | the new filter is pre-rolled over a saved copy of the previous two unvoiced frames (so it starts warm, with no HPF start-up thump), then a one-frame (~20 ms) crossfade between old and new | §4.4 |
| TTD / snapshots | not machine state, not saved. A seek is a short HPF blip | §4.6 |
| Recording / analyzer | PCM recording and analyzer taps are post-mix, so they record what you hear. Use `flat` for accurate captures. **DSD native recording** taps the chips at 218.75 kHz upstream of voicing, so it is always flat (documented; optional phase 2 fix) | §4.7 |
| Headroom | `classic` brings back the old edge overshoot (41 Hz square peak 1.01 vs 0.59). Gated by a test | §4.8 |
| Cost | ~9 MAC per sample per channel; benchmarked | §4.9 |

## 9. Settings and persistence

### 9.1 The setting

| Name | Values | Default | Scope |
|:--|:--|:--|:--|
| `ay_voicing` | `flat`, `classic` (+ alias `legacy`), later `tv` | `classic` | per emulator instance, runtime-changeable |

Core API (`SoundManager`): `setAYVoicing(FilterVoicing::Preset)` (any thread) and
`getAYVoicing()`. The getter returns the **requested** value, so a WebAPI `GET` right after a `PUT`
already shows the new value, even though audio switches at the next frame boundary.

### 9.2 Where the value comes from (highest wins)

```
1. runtime change on this instance     (GUI control, WebAPI, CLI, Lua, Python, MCP)
2. frontend's saved user preference    (unreal-qt only, applied when the GUI creates the instance)
3. [SOUND] AYVoicing in unreal.ini     (per machine profile; headless / tests / MCP-only runs)
4. built-in default: classic
```

This follows the existing `targetCoreRate()` pattern (runtime > device > ini > default). Core
never reads QSettings; frontends apply their saved value through the same setter automation uses.

### 9.3 Storage locations

| Store | Key | Written by | Read by |
|:--|:--|:--|:--|
| `unreal.ini` (per machine config) | `[SOUND] AYVoicing=classic` | hand-edited only | `config.cpp` → `config.sound.ayVoicing` → `SoundManager` constructor |
| Qt: `QSettings(IniFormat, UserScope, "Unreal", "Unreal-NG")` | `Sound/AYVoicing` | audio settings widget | `MainWindow` on instance creation |

- Unknown values in any store: log a warning and use the default. The same rule as `[SOUND] CoreRate`.
- Shipped `unreal.ini` files do **not** get the key; the built-in default covers them. That also
  avoids whole-file diffs on the CRLF `pentagon128k/unreal.ini`.

### 9.4 Punch / room ride along (phase 2, optional)

Today `AYPunch`, `AYRoom` and `BeeperPunch` are set only from the Qt widget. They are neither
persisted nor automatable, and they are written from the GUI thread without synchronisation. The
extended proposal groups them with voicing in one value type, `SoundCharacterSettings` (TDD
§5.2). All four then get the same frame-boundary handoff, the same QSettings keys (`Sound/AYPunch`,
`Sound/AYRoom`, `Sound/BeeperPunch`) and the same automation settings (`ay_punch`, `ay_room`,
`beeper_punch`). Voicing does not depend on this step.

## 10. Feature gates

**No new `FeatureManager` feature.** Voicing is a user preference with several values, not an
on/off performance or debug toggle. Two more reasons: `PUT /feature/{name}` cannot set a mode, and
`features.ini` is saved but never loaded (`FeatureManager::loadFromFile` has no caller), so it
would not persist anyway. The "off switch" is the `flat` profile, which is an exact bypass.

How the existing gates apply:

| Gate | Effect on voicing |
|:--|:--|
| `sound` off | skipped (nothing is synthesised) |
| synthesis suppressed (turbo without audio request, not recording) | skipped, state kept |
| turbo LQ override while audible | **runs** |
| `soundhq` off | **runs** (only punch/room are skipped) |
| `recording` on | runs; the recording contains the voiced sound |
| videowall inactive tile (`sound` + `soundhq` off) | skipped automatically |

No compile-time switch: the code is small and always built.

## 11. GUI clients

### 11.1 unreal-qt

In `unreal-qt/src/debugger/widgets/audiosettingswidget.cpp`, AY group, above the existing
Punch/Room controls:

```
┌ AY / TurboSound ─────────────────────────────────────────────┐
│ Bass voicing: [ Classic (softer bass, as in earlier versions) ▾ ] │
│               ( Flat (like real hardware line out)             ) │
│ [x] Punch        ⓘ Punch was tuned for Classic voicing   (only shown with Flat) │
│ Room: [ Off ▾ ]                                               │
└───────────────────────────────────────────────────────────────┘
```

| Behaviour | Rule |
|:--|:--|
| Enabled state | always enabled while sound is on. **Not** greyed out when Sound HQ is off (punch/room still are) |
| On change | `pSoundManager->setAYVoicing(p)` and write `Sound/AYVoicing` |
| On widget refresh (`loadSettings`) | read `getAYVoicing()` from the bound instance, so a value set via WebAPI shows up |
| Instance created by the GUI | apply the saved preference before the first audio frame |
| Instance adopted (created via WebAPI/MCP) | **don't overwrite**; show its current value. The creator owns the setting |
| Prerequisite (phase 2) | `MainWindow` cannot tell these apart today: GUI-created instances (`mainwindow.cpp:1286`, `:2860`) and picked-up ones (`:3491`, `:4139`) all go through one `adoptEmulator(std::shared_ptr<Emulator>)`. Add `adoptEmulator(emu, EmulatorOrigin::CreatedByGui \| Adopted)` and apply QSettings only for `CreatedByGui` |
| Tooltip | "Real AY boards send very low bass and the thump of volume changes straight to the output. Classic trims them the way earlier Unreal versions did. Recordings use the same setting." |

### 11.2 unreal-videowall

The videowall has no audio settings panel, and only the active tile plays sound.

| Item | Rule |
|:--|:--|
| Source of the value | **the active emulator's config file**: `[SOUND] AYVoicing` in the tile's `unreal.ini`, else the `classic` default. No videowall settings file and no videowall UI for now |
| What you hear | always the active tile's config, because silent tiles skip voicing. Tiles with different configs may differ; the shipped inis have no key, so all tiles are `classic` in practice |
| Runtime change | possible via automation on the active tile (`ay_voicing`); not persisted |
| Code changes | none in `unreal-videowall/` for phases 1–2. A Sound menu with its own persistence is a possible later addition |
| Cost on silent tiles | none (`sound` off → voicing skipped) |
| Punch / room | core defaults, as today. Phase 2 may add menu entries |

### 11.3 Headless and tests

No frontend is involved, so the value comes from `[SOUND] AYVoicing` or the `classic` default.
Tests that check hardware-level audio set `flat` explicitly. `EmulatorTestHelper` does not force
a profile globally.

## 12. Automation surface

This follows the existing `audio_rate` setting end to end. That setting is already wired through
every automation module, so each module gets one more branch next to it.

| Surface | Addition |
|:--|:--|
| WebAPI (`settings_api.cpp`) | `settings.audio.ay_voicing` in `GET /api/v1/emulator/{id}/settings`; `GET/PUT /api/v1/emulator/{id}/settings/ay_voicing`; 400 + `allowed` list on unknown values |
| OpenAPI | `openapi_settings.inc`, `openapi_schemas.inc` (enum of visible profile IDs) |
| MCP | nothing to code: `search_api` / `invoke_api` pick it up from `openapi.json` |
| CLI (`cli-processor-settings.cpp`) | `setting ay_voicing [flat\|classic]`, shown in `settings` |
| Lua / Python (`lua_emulator.h`, `python_emulator.h`) | string get/set next to `audio_rate` |
| `.recipe/` | audio capture/analysis recipe: "set `ay_voicing=flat` for hardware-accurate spectra" |

Example:

```bash
curl -s "http://localhost:8090/api/v1/emulator/$EMU_ID/settings/ay_voicing" | jq .
# { "name": "ay_voicing", "value": "classic", "allowed": ["flat","classic"], "description": "..." }

curl -s -X PUT "http://localhost:8090/api/v1/emulator/$EMU_ID/settings/ay_voicing" \
  -H "Content-Type: application/json" -d '{"value":"flat"}' | jq .
```

## 13. Verification and rollout (summary)

| Phase | Content | Exit criterion |
|:--|:--|:--|
| 1 — core | `FilterVoicing`, `SoundManager` wiring with two-frame pre-roll + crossfade, `[SOUND] AYVoicing`, DSP + SoundManager tests (full audit list), benchmark, DSD-is-flat note | tests green, zero warnings; A/B listening vs. a `d90421bb` build on bass-heavy, "barrel" and TSFM tracks |
| 2 — surfaces | WebAPI/CLI/Lua/Python/OpenAPI (hidden presets rejected); Qt `EmulatorOrigin` + combo + QSettings + create/adopt rule; optional voicing on the DSD native tap; videowall: none (active emulator's config); optional punch/room settings | setting reachable from every surface; GUI keeps the choice across restarts |
| 3 — `tv` profile | tune by listening from the §7.2 starting point, then make visible | signed off by ear, tests extended |
| 4 — docs | permanent `docs/emulator/design/audio/ay-tone-voicing.md`, recipe line, `DONE.md` | links valid |

Key tests (full list in TDD §9):
- `flat` is bit-exact.
- `classic` matches the old `FilterDC` in harmonic-bearing 1/3-octave bands within ±0.75 dB below
  200 Hz and ±1.5 dB from 200 Hz to 1 kHz (the old comb ripple is not copied on purpose).
- Infrasonic ratio is back within 1 dB of the old filter.
- Same bass in HQ and LQ.
- Preset switch is click-free, measured by HF energy above 8 kHz plus an offline ideal-crossfade
  reference (this needs the pre-roll).
- Filter state is carried across the HQ return (checked via a state accessor, not audio).
- Note: tests run with `soundhq` off, so voicing is newly active in every `SoundManager` test;
  the audit list in TDD §9.3 covers them.
- TSFM FM is untouched.
- Headroom ≤ old peak + 0.25 dB.
- Hidden presets are rejected, and the getter returns the requested value.

---

## 14. Summary of the proposal

1. Keep `FilterDCBlocker` at 5 Hz in `SoundChip_AY8910` (physical coupling).
2. Add a table-driven voicing EQ for AY/SSG only. Profiles: `flat` (exact bypass), `classic`
   (HPF1 64.2 Hz + peak 106.9 Hz / +3.06 dB / Q 1.0, **default**), later `tv`.
3. Run it before punch, **outside** the HQ-only character chain, in HQ and LQ alike, with changes
   applied at frame boundaries and crossfaded.
4. Expose it as the per-instance setting `ay_voicing`. Resolution order: runtime → frontend
   preference → `[SOUND] AYVoicing` → `classic`. No new feature flag.
5. unreal-qt: dropdown in the audio settings, saved in QSettings, applied to instances the GUI creates
   (adopted instances keep their own value).
6. videowall: no settings of its own; it uses the active emulator's config file (`[SOUND] AYVoicing`).
7. Automation: WebAPI/OpenAPI, CLI, Lua, Python; MCP via the API router.

What the review kept and changed: [TDD §0](ay-tone-voicing-tdd.md#0-verdict-does-the-proposal-make-sense).
