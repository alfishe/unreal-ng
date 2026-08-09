# Audio Sources UX — Phase 2 Design

**Status:** Proposed

**Date:** 2026-07-17

**Scope:** UX design for per-device audio source support in the Audio Settings
dialog, and its contract with the recording subsystem (Phases 2–3 of the
[recording plan](README.md#implementation-status)).

---

## 1. Motivation

Phase 2 of the recording plan ("Audio Device API") exposes per-device and
per-channel audio buffers in the core. This document designs the user-facing
side: how sources appear, how the user auditions and balances them, and how
the same vocabulary carries into recording (single-source and multi-track).

### User stories

1. *As a musician*, I want to hear only the AY chip (beeper muted) to check a
   tune's mix before recording it.
2. *As a chiptune archivist*, I want to solo AY channel B to verify which
   channel carries the melody before a channel-split recording.
3. *As a TurboSound composer*, I want independent control of chip 1 and
   chip 2 (today the UI silently controls chip 1 only).
4. *As a user recording video*, I want the recorded audio to match what I
   currently hear (mutes/solos/volumes respected), or explicitly the full
   mix, and I want to know which one I'm getting.
5. *As any user*, when no emulator is running I still want to see the dialog
   and understand why it's inactive (already shipped: banner + disabled
   controls pattern).

---

## 2. Current state

`AudioSettingsWidget` (Tools → Audio Settings) today:

```
┌─ Audio Settings ─────────────────────┐
│ ┌─ AY / TurboSound ────────────────┐ │   Controls apply to chip 0 ONLY.
│ │ Stereo Mode: [ABC ▾]             │ │   Chip 1 of TurboSound is
│ │ Chip Model:  [AY-3-8910 ▾]       │ │   invisible to the user.
│ │ [x] FIR Filter (HQ mode)         │ │
│ │ [x] Punch Enhancement            │ │
│ │ Room Simulation: [Off ▾]         │ │
│ └──────────────────────────────────┘ │
│ ┌─ Channel Mixer ──────────────────┐ │   Chip 0 channels A/B/C only.
│ │ [ ] Mute A  ────────▮──  100%    │ │
│ │ [ ] Mute B  ────────▮──  100%    │ │
│ │ [ ] Mute C  ────────▮──  100%    │ │
│ └──────────────────────────────────┘ │
│ ┌─ Master Volume ──────────────────┐ │   Only two device faders exist:
│ │ AY:     ────────▮──  100%        │ │   AY (both chips together) and
│ │ Beeper: ────────▮──  100%        │ │   Beeper.
│ └──────────────────────────────────┘ │
│ ┌─ Beeper (1-bit) ─────────────────┐ │
│ │ [ ] Lowpass Filter               │ │
│ │ [x] Punch Enhancement            │ │
│ └──────────────────────────────────┘ │
└──────────────────────────────────────┘
```

Core reality: `SoundManager` owns `Beeper` and `SoundChip_TurboSound`
(= 2 × `SoundChip_AY8910`). COVOX / General Sound / MoonSound are declared
in enums but not implemented. `RecordingManager::SelectAudioSource()` stores
a source choice that the capture path ignores — recording always receives
the master mix.

---

## 3. Proposed UX

### 3.1 Structure: a "Sources" section replaces "Master Volume"

The dialog gains a device-oriented **Sources** section at the top — one row
per physical device actually present on the emulated machine, following
mixer-console conventions (M/S/fader/meter). Below it, each device that has
its own DSP gets a dedicated section — also shown only when that device is
present.

**Modularity rule:** the UI is built entirely from what the machine has.

- A device **not present** → no row, no section. Nothing greyed, no clutter.
- A device **present** → exactly one row in the Sources mixer.
- A device **present with dedicated DSP** → one row *plus* a dedicated
  section below (Beeper lowpass/punch, AY stereo/model/filter, a future
  COVOX section with its own controls, etc.).

Adding a new device type to the core is therefore one well-defined change:
register it with the Sources builder, and optionally provide its section
widget.

**Example — TurboSound machine (no COVOX):**

```
┌─ Audio Settings ────────────────────────────────────────┐
│ ┌─ Sources ───────────────────────────────────────────┐ │
│ │        ●  M  S   Volume              Meter          │ │
│ │ Beeper ●  [ ][ ] ─────────▮─── 100%  ▁▃▅▂           │ │
│ │ AY 1   ●  [ ][ ] ─────────▮─── 100%  ▂▅▇▅           │ │
│ │ AY 2   ○  [ ][ ] ─────────▮─── 100%  ····           │ │
│ └─────────────────────────────────────────────────────┘ │
│ ┌─ AY / TurboSound ────────────  Chip: [AY 1 ▾] ──────┐ │
│ │ Stereo Mode: [ABC ▾]   Chip Model: [AY-3-8910 ▾]    │ │
│ │ [x] FIR Filter   [x] Punch   Room: [Off ▾]          │ │
│ │ ┌─ Channels ────────────────────────────────────┐   │ │
│ │ │ A  [M][S]  ─────────▮───  100%                │   │ │
│ │ │ B  [M][S]  ─────────▮───  100%                │   │ │
│ │ │ C  [M][S]  ─────────▮───  100%                │   │ │
│ │ └───────────────────────────────────────────────┘   │ │
│ └─────────────────────────────────────────────────────┘ │
│ ┌─ Beeper (1-bit) ────────────────────────────────────┐ │
│ │ [ ] Lowpass Filter   [x] Punch Enhancement          │ │
│ └─────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
```

**Same dialog on a machine that also has COVOX** — Sources gains one row,
and a COVOX section appears:

```
┌─ Sources ───────────────────────────────────────────┐
│ Beeper ●  [ ][ ] ─────────▮─── 100%  ▁▃▅▂           │
│ AY 1   ●  [ ][ ] ─────────▮─── 100%  ▂▅▇▅           │
│ AY 2   ○  [ ][ ] ─────────▮─── 100%  ····           │
│ COVOX  ●  [ ][ ] ─────────▮─── 100%  ▃▅▂            │  ← added row
└─────────────────────────────────────────────────────┘
┌─ COVOX (8-bit DAC) ─────────────────────────────────┐  ← added section
│ [ ] DC-offset removal   Offset trim: [────▮──]      │
└─────────────────────────────────────────────────────┘
```

A bare 48K Spectrum shows only **Beeper** and **AY 1** rows and their two
sections — the dialog always matches the machine in front of you.

Row anatomy, left to right:

| Element | Meaning |
|---|---|
| Name | Device name; "AY 1"/"AY 2" appear only when TurboSound present |
| ● / ○ | Activity dot: filled when the device produced non-silence in the last ~500 ms |
| **M** | Mute — removes the device from the master mix |
| **S** | Solo — only soloed devices are audible (DAW semantics, see 3.3) |
| Volume | Per-device fader, 0–100 % (replaces the old "Master Volume" rows) |
| Meter | Peak meter, updated ~10 Hz from the device's frame buffer |

Absent devices are simply not shown — there are no greyed placeholder
rows. Rows and sections are built (and rebuilt on emulator switch) from
the live `SoundManager` device list, so the dialog always reflects the
current machine. (See open question #4, now resolved in this direction.)

### 3.2 TurboSound chip selector

The AY group gets a `Chip: [AY 1 ▾]` selector (hidden when only one chip).
Stereo mode, chip model, and the channel mixer below apply to the selected
chip. This fixes today's silent chip-0-only behavior — story 3.

### 3.3 Solo semantics (device rows and channel rows)

Standard DAW rules, applied per layer:

- Any solo active → only soloed items in that layer are audible.
- Solo overrides mute display-wise but does not clear it: un-soloing
  restores the previous mute states (solo is non-destructive).
- Multiple solos allowed (solo A + solo B = both audible).
- Device layer and channel layer compose: solo on "AY 1" then solo on
  channel B yields "AY 1 channel B only".
- Active solo anywhere is flagged in the section header ("Sources — SOLO")
  so a forgotten solo is discoverable.

### 3.4 Recording contract

The Audio Settings dialog is the **monitoring** domain. The recording widget
remains the **capture** domain but reuses the same source vocabulary:

- The recording audio-source dropdown offers: **"What you hear"** (post
  mute/solo/volume mix — default), **"Full mix"** (everything, ignoring
  monitor mutes), or a single named source ("Beeper", "AY 1", "AY 1 ch B",
  ...).
- While recording, the Sources section shows a red ● REC badge on captured
  sources; monitor changes during recording affect capture only in
  "What you hear" mode, and the UI says so in a tooltip.
- The future Multi-Track dialog (Phase 3) presents the same named sources
  as its per-track source pickers.

### 3.5 States

- **No emulator:** existing pattern — warning banner, everything greyed.
- **Emulator switch:** rows and chip selector rebuild from the new context;
  monitor state (mute/solo/volumes) is per-instance runtime state.
- **Device inactive:** row enabled but activity dot hollow; meters idle.

---

## 4. Core API requirements (drives Phase 2 implementation)

The UX above needs exactly this from the core — this list *is* the Phase 2
work order:

| Need | API sketch | Exists? |
|---|---|---|
| Per-device frame buffers | `SoundManager::getDeviceBuffer(AudioSourceType)` → descriptor | ❌ (only master + AY sum) |
| Per-device peak for meters | `SoundManager::getDevicePeak(AudioSourceType)` or computed UI-side from buffer | ❌ |
| Device presence query | `SoundManager::hasDevice(AudioSourceType)` | partial (`hasTurboSound`) |
| Per-device mute/solo/volume applied in the mix | extend `SoundManager` mixing (beeper/AY volumes exist; add mute/solo, per-chip split) | partial |
| Per-chip AY control | `getAYChip(index)` | ✅ |
| Per-channel mute/volume | `SoundChip_AY8910` channel API | ✅ (chip 0 wired only) |
| Per-channel rendering for capture | channel-split buffers on `SoundChip_AY8910` | ❌ |
| "What you hear" vs "full mix" capture taps | second mix bus or capture-time re-mix in `RecordingManager` | ❌ |

Suggested implementation order (each step independently shippable):

1. **M1 — Chip selector + AY 2 wiring** (pure UI + existing API).
2. **M2 — Sources rows with mute/volume** (extend SoundManager mixing;
   replaces Master Volume group).
3. **M3 — Solo + activity dots + meters** (per-device buffers, peaks).
4. **M4 — Recording source routing** ("what you hear"/"full mix"/single
   source; makes `SelectAudioSource()` real — completes Phase 2, opens
   Phase 3).
5. **M5 — Channel solo + channel-split buffers** (enables ChannelSplit
   recording mode later).

---

## 5. Open questions

1. **Persistence:** should monitor state (mutes/volumes) survive restarts
   (QSettings per machine model), or reset each session? Proposal: persist
   volumes, reset mutes/solos.
2. **Meters cost:** peak scan of each device buffer per UI tick is cheap
   (~7k samples/frame/device), but should be skipped while the dialog is
   hidden.
3. **"What you hear" during turbo mode:** capture should still receive
   monitor-mix samples even when host audio output is muted — needs the
   mix to run independently of the audio callback.
4. **COVOX/GS rows — resolved:** hide devices not present on the current
   machine (see §3.1 modularity rule). No greyed placeholders; the Sources
   list and device sections rebuild from the live `SoundManager` device
   list on emulator switch.
