# ZXM-MoonSound in unreal-ng — integration and mixing design

**Revision 1** (2026-09-13).
**Status:** design draft. Depends on `opl4-core-tdd.md` (the chip library) being implemented and tested first.
**Scope:** wiring `libopl4` into unreal-ng — port decoding, device lifecycle, configuration, the sound-device registry, mixing, gain staging, TTD, recording, UI surface.
**Companion:** TTD details for this device live in `opl4-ttd-integration-tdd.md`; §7 here is the summary and that document is authoritative where they differ.
**Out of scope:** chip behaviour and DSP internals (see the core TDD).

---

## 0. Requirements

| # | Requirement | Source |
|---|---|---|
| R1 | MoonSound is an **additive** device, not a replacement. It coexists with Beeper, AY/TurboSound (or TSFM), COVOX and the rest. | Hardware: it is a separate NemoBus/ZX-BUS card. |
| R2 | Enabled per machine via config. Not switchable at runtime. | Same pattern as TSFM and COVOX. |
| R3 | Available on every model that has an expansion bus in the emulator's model set. | It is a bus card, not a motherboard feature. |
| R4 | **Full TTD support**: chip state and wave RAM save/restore, replay-exact. | Project-wide requirement. |
| R5 | FM and PCM appear as **separate mixer sources** with independent mute, solo, volume, metering and recording. | Registry design; parity with FM1/FM2 in TSFM. |
| R6 | Adding MoonSound must not change the output of any existing device by a single sample when it is disabled. | Safe integration. |
| R7 | With MoonSound enabled and active, the master mix must not clip and must not change perceived level of the other sources. | Mixer requirement carried over from the AY/beeper work. |

---

## 1. Decisions

| # | Decision | Why |
|---|---|---|
| D1 | `SoundManager` owns an optional `SoundChip_Moonsound*`, constructed once in the `SoundManager` constructor when the config flag is set. | Same lifecycle as COVOX. No sound-stack rebuild exists and none is needed. |
| D2 | Config keys are the **already-present legacy keys** `[SOUND] MoonSound` and `[SOUND] MoonSoundVol`, which every shipped `data/configs/*/unreal.ini` already carries and which nothing currently parses. | They are there, they mean the right thing, and reusing them avoids a second key for the same concept. Unlike the `[AY] Chip=YM2203` case in TSFM, these keys are not already set to a value that would silently enable the device on every machine — `MoonSound=1` is the intent. |
| D3 | The device is split into **core** (always runs) and **output stage** (skippable), following the TSFM pattern. `libopl4`'s `run()` / `render()` split maps onto it directly. | Busy, LD and timers are guest-visible; turbo and sound-off must not change them. |
| D4 | Port decoding is **full 16-bit**, not partial. | §2. The card's CPLD does full decode precisely because #C4/#C6/#7E collide with the ULA under ZX partial decoding. |
| D5 | Two registry sources: `Moonsound_FM` and `Moonsound_PCM`. The existing `AudioSourceType::Moonsound` placeholder is replaced by these two. | R5. A single stereo source cannot express "mute the FM, keep the samples", which is the first thing anyone wants. |
| D6 | The core rate is **not** forced to 44100 when MoonSound is enabled, but 44100 is recommended and logged as such. At 44100 the device takes the library's bit-exact bypass path. | Forcing the core rate would be a hidden global side effect of one device's presence. A log line is honest and sufficient. |
| D7 | **Wide mix plus soft limiter in `SoundManager` is a prerequisite**, not a follow-up. MoonSound is a 16-bit full-scale source; summing it with AY and beeper in the current path will clip. | It was deferred out of TSFM scope. It cannot be deferred here. |
| D8 | The library's character chain is used for MoonSound rather than `AudioCharacterChain`, because it needs to sit on the FM and PCM taps inside the library where the separate sums exist. `AudioCharacterChain` remains the host-side chain for AY and beeper. | Avoids duplicating the FM/PCM split across the library boundary. |
| D9 | Wave RAM enters TTD through a **dirty-page delta**, not a full 1 MiB blob per checkpoint. The library exposes a dirty bitmap for exactly this. | 1 MiB per checkpoint is not viable for time-travel. |
| D10 | The wave ROM image is a **host-supplied file**, path from config, loaded at device construction. A missing image is a warning plus a device that runs with a zero-filled ROM region, not a hard failure. | The emulator must still start. |

---

## 2. Port decoding

### 2.1 The card's ports

| Port | Direction | Function |
|---|---|---|
| `#C4` | write | FM register address, bank 1 |
| `#C4` | read | FM status |
| `#C5` | write | FM data, bank 1 |
| `#C6` | write | FM register address, bank 2 |
| `#C7` | write | FM data, bank 2 |
| `#7E` | write | Wave register address |
| `#7F` | read/write | Wave register data |

### 2.2 Why this needs full decode

`#C4`, `#C6` and `#7E` all have A0 = 0. On a ZX Spectrum the ULA responds to
**any** port with A0 = 0. Under the partial decoding used by most ZX peripherals
these three addresses collide with the ULA outright, and `#7F` collides with the
128K memory-paging port on machines that decode it loosely.

The real card resolves this in its CPLD by decoding the full 16-bit address off
NemoBus. The emulator must do the same:

- Register the device in `attachToPorts` with **exact 16-bit match** on the seven
  addresses above, and make sure the ULA/paging handlers see the access as well
  where the hardware would let them — or not, depending on what the CPLD actually
  gates. **This is the one behaviour that must be taken from the card's CPLD
  source or schematic and not guessed.** Getting it wrong shows up as border
  flicker or paging corruption when MoonSound software runs, which is a
  miserable bug to chase later.
- Until the CPLD behaviour is confirmed, implement the conservative reading — the
  card claims the address exclusively — and record the assumption in the device
  header.

### 2.3 Access path

```cpp
void SoundChip_Moonsound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    const uint64_t t = currentTState();
    switch (port)
    {
        case 0xC4: _opl4.writeFm  (t, /*bank*/0, /*addr latch*/ value, kAddrWrite); break;
        case 0xC5: _opl4.writeFm  (t, 0, _fmLatch[0], value);                       break;
        case 0xC6: _opl4.writeFm  (t, 1, value, kAddrWrite);                        break;
        case 0xC7: _opl4.writeFm  (t, 1, _fmLatch[1], value);                       break;
        case 0x7E: _waveLatch = value;                                              break;
        case 0x7F: _opl4.writeWave(t, _waveLatch, value);                           break;
    }
}
```

Every access calls into the library with the current T-state, and the library
advances its own clocks lazily. No per-instruction polling of the chip is needed
beyond §3.2.

---

## 3. Device lifecycle

### 3.1 Construction

```cpp
// SoundManager::SoundManager
if (_context->config.sound.moonSoundEnabled)
{
    _moonsound = new SoundChip_Moonsound(_context);
    _moonsound->setCoreRate(_coreRate);
}
```

Parsed in `Config::ParseConfig` alongside `CovoxFB`:

```ini
[SOUND]
MoonSound    = 1        ; enable ZXM-MoonSound (YMF278B / OPL4)
MoonSoundVol = 8000     ; 0..8192, legacy scale

[MOONSOUND]
WaveRom      = yrw801.rom   ; path relative to the ROM directory; optional
RamSizeKb    = 1024         ; ZXM-MoonSound fits 2 x 512 KiB SRAM
RenderMode   = authentic    ; authentic | hifi
Quality      = reference    ; reference | highfidelity
Punch        = off          ; off | pcm | both
BoardAnalog  = 0            ; YAC513 + LF347 output filter model
```

`MoonSound=0` (or the key absent) means the device is never constructed, never
registered, never attached to ports — satisfying R6 trivially.

### 3.2 Per-frame and per-step

| Hook | Action |
|---|---|
| `handleFrameStart` | Rebase the T-state origin; call `libopl4::run(t)`; clear the per-frame tap buffers. |
| `handleStep` | Nothing by default. Unlike TSFM, MoonSound has no per-instruction guest-visible state that a lazy sync cannot cover: every read of BUSY/LD goes through a port access, which already syncs. |
| `handleFrameEnd` | `run(t_end)`, then — only if audio is wanted — `render()` into the two frame buffers. |

The one case that needs care: a program that does a very long computation between
port accesses while a PCM slot is looping. The library's lazy advance handles it,
but the per-frame `run()` at frame start and end bounds how far behind the core
can fall, which keeps the internal buffering bounded.

### 3.3 Turbo and sound-off

`run()` always. `render()` only when `_soundEnabled && !turbo`. The library
guarantees this does not change chip state (core TDD D11, R8).

---

## 4. Sound-device registry

### 4.1 Source types

```cpp
enum class AudioSourceType
{
    MasterMix,
    Beeper,
    AY1_All, AY2_All, AY3_All,
    COVOX,
    GeneralSound,
    Moonsound_FM,      // was: Moonsound
    Moonsound_PCM,
    AY1_ChannelA, /* ... */
    Custom
};
```

Two `AudioDeviceInfo` entries, registered only when the device exists:

| Source | Name | Channels |
|---|---|---|
| `Moonsound_FM` | "MoonSound FM (OPL3)" | stereo |
| `Moonsound_PCM` | "MoonSound PCM (wave)" | stereo |

Per-channel sources (18 FM + 24 PCM) are **not** added to the enum. 42 extra
enum values would swamp the mixer UI and the recording source list. Instead the
library's tap API is exposed through a separate, indexed accessor on the device:

```cpp
size_t channelCount(ChannelGroup g) const;
void   setChannelMute(ChannelGroup g, size_t index, bool mute);
float  channelPeak(ChannelGroup g, size_t index) const;
```

The mixer UI shows two rows by default and expands to a channel grid on demand.

### 4.2 Buffers

Two `int16_t` stereo frame buffers, sized `MAX_SAMPLES_PER_FRAME * 2`, filled by
`render()` at the core rate. Same shape as the existing device buffers, so the
recording and metering paths need no special case.

---

## 5. Mixing

### 5.1 The problem this section exists to solve

Today the mixer sums device buffers into a 16-bit master. The existing sources are
modest: beeper is 1-bit, AY channels are a 4-bit log DAC each, COVOX is 8-bit.
MoonSound is a **full-scale 16-bit stereo source** — 24 PCM voices plus 18 FM
channels, already summed and clipped inside the chip. Adding it to the current
path will clip the master whenever a MoonSound track plays over anything else.

R7 states the requirement precisely: no audible distortion, and the perceived
level must not change with the number of active sources.

### 5.2 Wide mix plus soft limiter

Required before MoonSound ships (D7). The design, carried over from the RTL work
on the same problem:

1. **Widen the mix bus.** Sum into 32-bit float (or ≥20-bit fixed) rather than
   16-bit. No clipping inside the sum.
2. **Static gain staging** per source, so that a nominal full-scale source lands
   at a defined level in the bus. This is where `MoonSoundVol` and the other legacy
   `*Vol` keys map in.
3. **Piecewise soft limiter** on the master: linear below the knee, smoothly
   compressive above it, hard ceiling below full scale. Attack and release chosen
   so that a percussive PCM hit is not audibly ducked.
4. **DC blocker before the limiter.** A stuck PCM slot holding DC will otherwise
   eat the limiter's headroom permanently.

Bit-exactness note for R6: when MoonSound is absent and the limiter never engages,
the widened path must be numerically identical to the old 16-bit path. That is a
CI assertion, not a hope — the existing regression corpus for AY output is the
test.

### 5.3 Gain staging

The chip already applies its own attenuation twice (per-slot pan, then the block
mix 0xF8/0xF9). Do **not** compensate for the −9 dB FM reset offset in the host —
it is real chip behaviour and software sets those registers deliberately.

Host-side gain:

```
device_gain = (MoonSoundVol / 8192) * headroom_trim
```

`headroom_trim` is a single constant, chosen once by measurement so that a
full-scale MoonSound track sits at the same perceived loudness as a loud AY track.
Document the measurement (which track, what meter) next to the constant — an
undocumented magic gain constant is how the AY/beeper level mismatch happened.

### 5.4 Where the character chain sits

Inside the library, on the FM and PCM taps, before they are summed into the
device's stereo output (core TDD §8.4). The host does not apply
`AudioCharacterChain` to MoonSound.

Consequence for the mixer: the MoonSound device buffer arrives already
character-processed. Recording taps upstream of the master limiter, as it does
today, so recordings capture the character chain but not the limiter — consistent
with the existing behaviour for AY.

Default: punch **off** for FM, **off** for PCM, room off. `Punch=pcm` in config
turns on the `Opl4Pcm` preset for the wave side only, which is the setting most
likely to be worth having: the YRW801 tone set is largely 22.05 kHz material
upsampled by the chip and it is genuinely dull at the top.

---

## 6. Wave memory

### 6.1 Layout

| Range | Contents |
|---|---|
| `0x000000 – 0x1FFFFF` | ROM — 2 MiB wave image |
| `0x200000 – 0x2FFFFF` | SRAM — 1 MiB, guest-writable |
| `0x300000 – 0x3FFFFF` | unpopulated |

The host implements `IWaveMemory` over a single allocation of ROM + RAM, sized
from config, with an inlinable read path (core TDD §11).

### 6.2 ROM image

Loaded at construction from `[MOONSOUND] WaveRom`, resolved against the ROM
directory. Missing or short image: log a warning, zero-fill, continue (D10).
A zero-filled ROM produces silence from GM tones but the chip still works and
software that uploads its own samples to SRAM runs normally — which is most
demoscene material.

### 6.3 Upload path

Guests write samples into SRAM through the memory-access registers (0x03–0x06),
one byte at a time with LD busy between accesses. This is slow on real hardware
and programs are written around it; the emulator must reproduce the timing, not
just the data transfer, or loaders will run at the wrong speed and any
timing-sensitive loader will misbehave.

---

## 7. Time-travel debugging

### 7.1 What gets serialised

| Component | Size | Method |
|---|---|---|
| FM engine state | small | full, every checkpoint |
| PCM engine state (24 slots) | small | full, every checkpoint |
| Clock accumulators, BUSY/LD deadlines, TL interpolation counters | tiny | full |
| Wave SRAM | 1 MiB | **dirty-page delta** (D9) |
| Wave ROM | 2 MiB | never — content-hash only, verified on restore |
| Render layer | — | never (core TDD §9.2 item 3) |
| **Host-side device state** | tiny | full, every checkpoint — see §7.2 |

### 7.2 Host-side state that is easy to forget

The library's own inventory (core TDD §9.1) covers the chip. It does **not** cover
what `SoundChip_Moonsound` holds on the host side, and that state is just as
guest-visible:

| Item | Why it matters |
|---|---|
| `_fmLatch[0]`, `_fmLatch[1]` | The FM register address latched by a write to `#C4` / `#C6`. A restore that loses it sends the next data byte to the wrong register. |
| `_waveLatch` | Same for `#7E` / `#7F`. |
| T-state origin used for `currentTState()` | The library's clock is driven from it. Restore with a different origin and every subsequent access lands at the wrong chip phase. |
| Last timestamp passed to the library | Needed to assert monotonicity after restore rather than silently advancing backwards. |
| Registry state: mute, solo, volume, per-channel mutes | Not required for replay exactness, but a restore that resets the user's mixer settings is a bug of its own. Save it separately from the replay-critical payload. |

The first three are replay-critical. A useful sanity rule: anything the device
class stores between two port accesses belongs in the checkpoint.

### 7.3 Interface

`SoundChip_Moonsound` implements the project's `ttd::TTDSerializable` and
forwards to the library:

```cpp
void SoundChip_Moonsound::serialize(ttd::Archive& ar)
{
    ar("fmLatch", _fmLatch);
    ar("waveLatch", _waveLatch);
    ar("tstateOrigin", _tstateOrigin);
    ar("lastSyncTime", _lastSyncTime);

    Opl4Adapter adapter(ar);      // bridges ISerializer -> ttd::Archive
    _opl4.serialize(adapter);

    serializeWaveRam(ar);         // dirty-page delta, §7.5
}
```

The adapter exists so the library keeps its own minimal `ISerializer` and does not
take a dependency on the emulator's TTD types (core TDD R10).

### 7.4 Ordering constraint

`serialize()` must be called at a point where the core has already been advanced
to the checkpoint timestamp, and it must not advance it further. Concretely: the
host calls `run(t)` and then `serialize()`, never the other way round, and never
relies on `serialize()` to flush. The library guarantees the second half of that
(core TDD §9.2 item 1); the host owns the first.

### 7.5 Wave SRAM capture

**Superseded.** An earlier revision of this document described a device-local
dirty-page delta with a periodic full snapshot. That was written before the
emulator's TTD subsystem was examined and does not match it: the peripheral
interface is a fixed-size POD blob captured every frame, and page-store slot
references cannot live inside an opaque blob without leaking on timeline
truncation.

The actual design routes wave SRAM through the same codec page store that model
RAM uses, via a new generic peripheral paged-region mechanism. See
`opl4-ttd-integration-tdd.md` §5 and §9 for the mechanism, the framework change
it requires, and the size and cost budgets.

Summary of what changed for a reader of this document:

- Tier A (chip + host state) is a 2816-byte fixed blob through `TTDSerializable`.
- Tier B (1 MiB wave SRAM) is a paged region, 16 KB pages, I/P encoded, with
  `kNeverTouched` making an unwritten SRAM free for the whole session.
- The guest's write bandwidth through the wave data port caps SRAM at **≤ 3 dirty
  4 KB slots per frame**, so the worst case is bounded by the Z80 rather than by
  our implementation.

### 7.6 Replay exactness

The determinism contract in the core TDD (§9) is what makes this work. Add one
host-level test: run a session with MoonSound active, checkpoint continuously,
seek backward and replay forward, assert the audio stream matches the original
from the seek point. This is the integration-level counterpart to the library's
save-neutrality test, and it is the one that will catch a host-side mistake such
as rebasing the T-state origin without telling the library.

---

## 8. Recording and analysis

- Both sources appear in the recording source list and in the audio-sources UX
  alongside the existing devices.
- The audio-capture analyzer gains MoonSound as a capture target with no special
  casing, because the device buffers have the standard shape (§4.2).
- A register-log analyzer, modelled on the existing AY log analyzer, is worth
  having: it makes the VGM export path (§9) nearly free and it is the fastest way
  to get a reproducer out of a misbehaving program.

## 9. VGM export

A low-cost, high-value addition once the register log exists: dump MoonSound
register writes with T-state timestamps in VGM form. This gives:

- reproducers for bug reports that can be replayed against the library's test
  harness directly,
- the ability to contribute to the public VGM corpus,
- a way to compare unreal-ng's output against any other OPL4 implementation on
  real material, using the library's own differential test tooling.

---

## 10. Implementation order

| Phase | Deliverable | Gate |
|---|---|---|
| 0 | Wide mix + soft limiter in `SoundManager`, MoonSound absent | AY/beeper regression corpus bit-identical; limiter tested with synthetic overload |
| 1 | Config parsing, device skeleton, port attachment, silent registry sources | Device appears in mixer UI and meters at zero; R6 assertion passes |
| 2 | `libopl4` vendored; wave memory implementation; ROM loading | Library's own test suite green in unreal-ng's CI |
| 3 | Core wired to the T-state axis; port protocol; BUSY/LD | Known MoonSound software detects the card and runs; register log matches expectations |
| 4 | Render path, two registry sources, gain staging | Audio output correct at 44100; bypass path asserted bit-exact |
| 5 | Multirate — 48/96/192 kHz | Rate-invariance test passes |
| 6 | TTD: chip state, dirty-page SRAM | Replay-exactness test passes |
| 7 | Character chain presets, per-channel taps, mixer UI expansion | Subjective evaluation against real hardware recordings |
| 8 | VGM export, register log analyzer | Round-trip: export from unreal-ng, replay in the library harness, identical output |

Phase 0 is genuinely first. Everything after it produces audible output, and
audible output over a clipping mixer will send the evaluation in the wrong
direction.

---

## 11. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| CPLD port-decode behaviour unknown (§2.2) | ULA/paging corruption under MoonSound software | Get the CPLD source or schematic before phase 3; conservative exclusive claim until then |
| No real ZXM-MoonSound available for reference | Board analog model and BUSY/LD timings stay unverified | Ship with those components disabled/datasheet-derived and clearly marked; treat hardware access as a project dependency, not a nice-to-have |
| SRAM upload timing wrong | Loaders run at wrong speed; timing-sensitive software breaks | Model LD busy per access from phase 3, not as a later refinement |
| Limiter audibly ducks PCM transients | The device sounds worse than a naive implementation | Tune attack/release against percussive material specifically; A/B against the unlimited path with headroom |
| Core rate other than 44100 becomes the default | Every user gets resampled OPL4 output | Log a recommendation (D6); consider surfacing it in the UI when MoonSound is enabled |
