# Sound Device Registry & Source Architecture

**Status:** Proposed
**Date:** 2026-07-17
**Companion:** [Audio Sources UX](../recording/audio-sources-ux.md) — the UI
this architecture powers. Its "Core API requirements" table is satisfied by
this design.

---

## 1. Goal

One uniform device model that:

- drives the Audio Settings **Sources** mixer (rows/sections built from the
  live device list — the UX doc's modularity rule),
- gives the recording subsystem per-device buffers (Phase 2) and later
  multi-track capture (Phase 3),
- makes adding a sound device a single well-defined change: implement the
  device, register it, optionally ship a UI section widget.

## 2. Current state

- `SoundDevice` base class exists (`sounddevice.h`) with the right shape:
  t-state-driven `update(tact, sample)` + `frameStart/frameEnd` and an
  internal sample buffer. `Beeper` uses it.
- `SoundChip_TurboSound` (2 × AY8910) lives outside that pattern with its
  own buffer/decimation pipeline.
- `SoundManager` hardwires exactly two devices and mixes them with two
  volume fields; no presence enumeration, no per-device mute/solo.
- `AudioSourceType` enum (recordingmanager.h) already names every device
  we plan: Beeper, AY1-3, COVOX, GeneralSound, Moonsound, per-AY-channels.
- Legacy config already parses device flags: `covoxFB, covoxDD, sd,
  saa1099, moonsound` (+ volumes) — machine presence can be config-driven
  from day one.

## 3. Design

### 3.1 Device descriptor and registry

`SoundManager` owns an ordered registry of present devices:

```cpp
struct AudioDeviceInfo
{
    AudioSourceType type;          // Identity, shared with recording
    std::string     name;          // Display name ("AY 1", "COVOX", ...)
    SoundDevice*    device;        // Owning pointer lives in SoundManager

    // Monitor state (runtime, per emulator instance)
    bool  mute = false;
    bool  solo = false;
    float volume = 1.0f;

    // Read-only status for UI
    float peak = 0.0f;             // Refreshed each frame (frameEnd)
    bool  activeRecently = false;  // Non-silence within last ~500 ms
};

// SoundManager API
const std::vector<AudioDeviceInfo>& devices() const;
AudioDeviceInfo* device(AudioSourceType type);          // nullptr if absent
const int16_t* deviceBuffer(AudioSourceType type) const; // frame samples
void setDeviceMute(AudioSourceType, bool);
void setDeviceSolo(AudioSourceType, bool);
void setDeviceVolume(AudioSourceType, float);
```

Registration happens in `SoundManager` construction based on the machine
config: Beeper and AY 1 always; AY 2 when TurboSound; COVOX when
`covoxFB`; others as they are implemented. Absent device → not in the
registry → no UI row, no capture source (matches UX doc §3.1).

### 3.2 Uniform buffer contract

Every device renders one emulated frame of interleaved stereo int16 at
`AUDIO_SAMPLING_RATE` into its own `AudioFrameDescriptor` — the format
`CaptureAudio()` and the mixer already speak.

- `Beeper` already does this.
- `TurboSound` renders into one buffer today; it registers as **two**
  devices (AY 1 / AY 2) by additionally exposing per-chip buffers
  (`getChipBuffer(idx)`), which its render loop fills anyway before
  summing. Cost: one extra buffer write per chip, no extra DSP.
- New devices (COVOX etc.) extend `SoundDevice` and get the contract for
  free.

### 3.3 Mixing with mute/solo/volume

`SoundManager::handleFrameEnd()` replaces its hardwired
`beeper + ay` sum with a registry loop:

```
soloActive = any(d.solo)
audible(d) = soloActive ? d.solo : !d.mute
out[i] = Σ audible(d) · d.volume · d.buffer[i]   (saturating add)
```

- Peaks are computed in the same loop (one pass, ~7k samples/device).
- The legacy `setAYVolume/setBeeperVolume` API delegates to the registry
  entries for compatibility.
- **Capture taps** (Phase 2 → 3):
  - "Full mix": sum with volumes but *ignoring* mute/solo.
  - "What you hear": exactly `out[]`.
  - Single source: `deviceBuffer(type)` directly.
  Recording re-mixes from device buffers at capture time, so turbo-mode
  and host-mute do not affect capture (UX doc open question #3: the mix
  above runs in `handleFrameEnd` regardless of the audio callback).

### 3.4 Channel-level layer (AY)

Channel mute/solo/volume stay on `SoundChip_AY8910` (already implemented
for mute/volume; solo added the same way). Channel-split capture buffers
(`getChannelBuffer(ch)`) come in milestone M5 and are only rendered while
a channel-split recording or channel solo is active (zero cost otherwise).

## 4. Device stubs and implementation order

Each planned device gets a stub now: a `SoundDevice` subclass with port
decoding documented, registered only when its config flag is set. A stub
renders silence; presence in the registry is the integration test that the
whole pipeline (row, meters, capture) works before the DSP exists.

| Device | Ports (config flag) | Complexity | Plan |
|---|---|---|---|
| **COVOX (mono)** | `#FB` write = unsigned 8-bit DAC (`covoxFB`) | Trivial — beeper-like, no clock | **Implement now** (§5) |
| **Stereo Covox** | `#B3`/`#FB` pairs, machine-dependent (`covoxDD`) | Trivial ×2 | Stub; implement after mono proves the path |
| **SounDrive** | `#0F,#1F,#4F,#5F` — 4 × 8-bit channels (`sd`) | Easy | Stub |
| **SAA1099** | `#FF` data/addr (`saa1099`) | Medium (chip emu) | Stub |
| **General Sound** | `#B3/#BB` + own Z80 subsystem (`gs`) | Large | Stub, out of near scope |
| **MoonSound (OPL4)** | `#C4-C7` (`moonsound`) | Large | Stub, out of near scope |

## 5. COVOX implementation sketch

The simplest real device — validates the registry end to end:

```cpp
class Covox : public SoundDevice   // + PortDevice
{
    // Port #FB write (Pentagon/Scorpion convention):
    void portDeviceOutMethod(uint16_t port, uint8_t value) override
    {
        // Unsigned 8-bit DAC, centered: 0x80 → 0
        float sample = (static_cast<float>(value) - 128.0f) / 128.0f;
        update(currentTState(), sample);   // SoundDevice does S&H render
    }
};
```

- **Timing:** like the beeper, output is a zero-order hold between CPU
  writes at t-state resolution; `SoundDevice::update()` already renders
  the held value into the frame buffer.
- **DC handling:** digi playback leaves the DAC at an arbitrary level —
  a `FilterDC` on the device output (same one the AY path uses), exposed
  as the "DC-offset removal" toggle in its UI section (UX doc §3.1).
- **Registration:** present iff `config.covoxFB`; volume seeds from
  `covoxFB_vol`.
- **Recording:** appears automatically as a capture source via the
  registry — no recording-side changes needed.

## 6. Milestones (supersedes UX doc M-list on the core side)

1. **R1 — Registry + mixing loop**: descriptor, enumerate, mute/solo/
   volume, peaks; Beeper + AY1 (+AY2 via per-chip buffers) registered;
   legacy volume API delegates. UI Sources section binds to it.
2. **R2 — COVOX**: implement per §5; first config-gated device; proves
   "add a device = register + section".
3. **R3 — Capture routing**: "what you hear" / "full mix" / single-source
   taps in RecordingManager (completes recording-plan Phase 2).
4. **R4 — Stubs**: Stereo Covox, SounDrive, SAA1099, GS, MoonSound
   classes with documented ports, registered behind config flags
   (silent), so UI/capture handle N devices generically.
5. **R5 — Channel split**: AY channel buffers + channel solo (opens
   ChannelSplit recording mode, recording-plan Phase 3 territory).

## 7. Open questions

1. Should the registry live in `SoundManager` (proposed) or a separate
   `AudioDeviceRegistry` owned by it? Proposal: inside SoundManager until
   a second consumer needs it standalone.
2. TurboSound as two registry devices shares one `SoundChip_TurboSound` —
   mute/solo of "AY 2" must gate its contribution inside the TurboSound
   sum. Cleanest: TurboSound reads the registry entries' audible/volume
   state during its own mix step.
3. Stereo Covox port map differs per clone family — pick Pentagon
   convention first? (Config flag names suggest the legacy Unreal port
   map; document per machine when implementing.)
