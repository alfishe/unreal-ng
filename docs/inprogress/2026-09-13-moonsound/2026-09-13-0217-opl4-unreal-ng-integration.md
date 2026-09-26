# ZXM-MoonSound in unreal-ng — integration and mixing design

**Revision 5** (2026-09-14). Records the end-to-end demo verification on the card author's complete 26-disk corpus — every disk plays, both engines audible (§12.6) — and the test-pinned gain staging / no-clip proof (§5.3, §12.7). The one remaining harness failure is a TR-DOS double-sided loader stall in the disk subsystem, independent of the card (§12.8).
**Revision 4** (2026-09-14). Revision 3 resolved the low-byte port-decode blocker. This revision records the openMSX reference audit (`2026-09-14-1828-opl4-openmsx-audit-and-diff-harness.md`): status LD moved to bit 1, LD now opens only on tone-load writes, wave writes are gated on NEW2 (0x105 bit 1), and the PCM loop end switched to the chip's stored-complement form. See that document for the full suspect-by-suspect diff.
**Revision 3** (2026-09-14). Revision 1 was the pre-implementation design draft (2026-09-13); Revision 2 recorded implementation and live verification. This revision resolves the former §12.5 blocker: guest I/O never reached the card because the Z80 immediate port forms leave A in the high address byte and the card decodes A0..A7 only (§2.5) — now fixed and guest-verified with the author's own binary.
**Status:** phases 0–4 + TTD Tier A implemented and verified end to end; the author's complete disk set plays FM + PCM in-app (§12.6). Open: TTD Tier B (wave SRAM), the §12.8 harness loader stall. Depends on `2026-09-13-0217-opl4-core-tdd.md` (the chip library).
**Scope:** wiring `libopl4` into unreal-ng — port decoding, device lifecycle, configuration, the sound-device registry, mixing, gain staging, TTD, recording, UI surface.
**Companion:** TTD details for this device live in `2026-09-13-0217-opl4-ttd-integration-tdd.md`; §7 here is the summary and that document is authoritative where they differ.
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
| D6 | The core rate is **not** forced to 44100 when MoonSound is enabled, but 44100 is recommended and logged as such. At 44100 the device takes the library's bit-exact bypass path. | Forcing the core rate would be a hidden global side effect of one device's presence. A log line is honest and sufficient. **Live changes (2026-09-18):** a device renegotiation (`SoundManager::requestCoreRate`, applied at the next frame start by `applyCoreRate`) reaches the chip through `SoundChip_Moonsound::setCoreRate` → `Opl4::SetOutputRate`, like the AY/TS/TSFM path. Chip state and pending chip-grid audio are kept; the render layer re-designs for any rate from 44100 to 192000. |
| D7 | **Wide mix plus soft limiter in `SoundManager` is a prerequisite**, not a follow-up. MoonSound is a 16-bit full-scale source; summing it with AY and beeper in the current path will clip. | It was deferred out of TSFM scope. It cannot be deferred here. |
| D8 | The library's character chain is used for MoonSound rather than `AudioCharacterChain`, because it needs to sit on the FM and PCM taps inside the library where the separate sums exist. `AudioCharacterChain` remains the host-side chain for AY and beeper. | Avoids duplicating the FM/PCM split across the library boundary. |
| D9 | Wave RAM enters TTD through a **dirty-page delta**, not a full 1 MiB blob per checkpoint. The library exposes a dirty bitmap for exactly this. | 1 MiB per checkpoint is not viable for time-travel. |
| D10 | The wave ROM image is a **host-supplied file**, path from config, loaded at device construction. A missing image is a warning plus a device that runs with a zero-filled ROM region, not a hard failure. | The emulator must still start. |

---

## 2. Port decoding

### 2.1 The card's ports

*Update 2026-09-17 (MFM Music sample 2 evidence):* the card CPLD decodes only
**A0..A7** — every high-byte alias of a card port is the card port (MoonService-
verified; the Z80 `out (n),a` form leaves A in the high address byte and real
drivers depend on the card ignoring it). The two FM **data** ports are
**equivalent**: a data write delivers to the bank of the most recent
address-port write (`#C4` → bank 1, `#C6` → bank 2), not to a per-port bank.
Measured melody-7 traffic mixes the pairs freely: C4→C5 1341, C6→C7 1166,
C6→C5 112, C4→C7 0. Under the earlier per-data-port decode, the author's own
MBPlayer programmed bank-2 registers as `out (#C6),reg` + `out (#C5),data`,
NEW never latched, and every later bank-2 register write aliased onto bank 1 —
the exact host-side half of the "some channels noising" failure.

| Port (low byte) | Direction | Function |
|---|---|---|
| `#C4` | write | FM register address latch, selects bank 1 |
| `#C4` | read | FM status (BUSY/T1/T2/LD) |
| `#C5` | write | FM data — writes bank of the last address-port write |
| `#C6` | write | FM register address latch, selects bank 2 |
| `#C7` | write | FM data — same as `#C5` |
| `#7E` | write | Wave register address latch — ignored while NEW2 clear |
| `#7F` | read/write | Wave register data — ignored while NEW2 clear |

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
  header. *Update 2026-09-14: read-side arbitration is now verified and
  NEW-gated rather than statically exclusive — see §2.4.*

### 2.3 Access path

```cpp
void SoundChip_Moonsound::portDeviceOutMethod(uint16_t port, uint8_t value)
{
    switch (port & 0xFF)  // CPLD decodes A0..A7 only (2.1 update)
    {
        case 0xC4: _fmBank = 0; _fmLatch[0] = value;                          break;
        case 0xC6: _fmBank = 1; _fmLatch[1] = value;                          break;
        case 0xC5:  // data ports are equivalent: the bank is the last
        case 0xC7:  // address-port write's, not the data port's own
            _opl4.WriteFm(chipTimeNow(), _fmBank, _fmLatch[_fmBank], value);  break;
        case 0x7E: if (_opl4.New2Mode()) _waveLatch = value;                  break;
        case 0x7F: if (_opl4.New2Mode()) _opl4.WriteWave(chipTimeNow(), _waveLatch, value); break;
    }
}
```

Every access calls into the library with the current T-state, and the library
advances its own clocks lazily. No per-instruction polling of the chip is needed
beyond §3.2.

### 2.4 Read arbitration on `#7F` — verified (2026-09-14)

The collision called out in §2.2 is real and, on Pentagon-family machines,
sharper than the conservative reading assumed:

- **The FDC owns `#7F` in the port registry.** `WD1793` registers
  `#1F/#3F/#5F/#7F/#FF` as exact-match port handlers, and a registry slot has
  a single owner (`PortDecoder::_portDevices`), so a second full-decode device
  cannot register `#7F` — `RegisterPortHandler` refuses the duplicate.
- **The Pentagon decoder gates Beta-128 ports.** While no TR-DOS session is
  open (`emulatorState.flags & CF_TRDOS` clear), `PortDecoder_Pentagon128`
  rewrites decoded `#1F/#3F/#5F/#7F/#FF` accesses to undecoded
  (`portdecoder_pentagon128.cpp`). The FDC data port answers only inside a
  session — opened when the CPU executes from the `#3D2F` entry region
  (`CF_SETDOSROM`), closed by `CF_LEAVEDOSADR` the moment a fetch has
  `PC ≥ #4000`.

The implemented and unit-verified design
(`core/tests/emulator/sound/chips/soundchip_moonsound_test.cpp`):

1. `SoundChip_Moonsound` registers writes for all seven ports as before, but
   does **not** own `#7F` reads statically.
2. A `PortDevice` virtual, `portDeviceClaimsRead(port)`, lets a full-decode
   device claim the read side only once the guest has armed the card. The
   MoonSound override returns true for `port == 0x7F && _opl4.New2Mode()` —
   i.e. after software sets the OPL4 `NEW2` bit (FM2 bank-1 register 5 bit 1;
   Revision 4: the chip's own write gate uses NEW2 specifically, matching
   openMSX `YMF278B::getNew2`), exactly the arming sequence every detection
   routine performs.
3. `NotifyFullDecodeIn()` reports `claimsBus` alongside the value; `Z80::in()`
   takes the full-decode value when the handler claims the bus or when no
   partial-decode device answered (`WasLastPortDecoded()`).
4. R6 holds: an unarmed card never claims, so the FDC mirror and the floating
   bus behave exactly as before the device existed.

The test pinning this (`SharedBus_ArmedCardOverridesLegacyMirrorAt7F`) also
records two harness facts future tests will need: a mirror stub must *borrow*
the `#7F` registry slot from the WD1793 (unregister, install, restore via
`context->pBetaDisk`), and the TR-DOS session must be opened by hand
(`context->emulatorState.flags |= CF_TRDOS`) or the Pentagon decoder drops the
port before arbitration runs.

### 2.5 Low-byte port decode — the verified card behaviour (2026-09-14)

The ZXM-MoonSound CPLD wires **A0..A7 only**: every high-byte alias of
`#C4/#C5/#C6/#C7/#7E/#7F` is a card port. Guest software depends on this
because the Z80 immediate port forms build the address from two registers —
`out (n),a` and `in a,(n)` execute at `(a << 8) | n` (`op_noprefix.cpp`) — so
A lands in the **high** address byte. The card author's own driver
(MoonService v0.3a) writes registers with `ld a,d / out (n),a`: the register
number itself dirties the high byte (the FM1 `reg #BD` select is a write to
port `#BDC4`), and the device-ID read is `in a,(#7F)` with `A = #10` → port
`#107F`.

The implementation therefore registers the card as a **low-byte full-decode
observer** (`PortDecoder::RegisterFullDecodeLowBytePort`): the notify taps
check the exact 16-bit table first, then the low-byte table, and
`SoundChip_Moonsound` dispatches its port handlers on `port & 0xFF`
(`portDeviceClaimsRead` compares `(port & 0xFF) == 0x7F`). The original
exact-16-bit registration only caught host-side `Z80::out()/in()` calls and
silently missed every guest instruction — the direct cause of the former
§12.5 symptom (`dev_id = #E0` = floating-bus `#FF` masked with `#E0`).

Ownership follows the AY/TurboSound device pattern: the six claims live in
`SoundChip_Moonsound::attachToPorts(PortDecoder*)` / `detachFromPorts()` —
the card owns its bus interface, and `SoundManager` (a router) only forwards
the lifecycle call from its own `attachToPorts()` / `detachFromPorts()`.

### 2.6 Motherboard decode clashes — analysis API and claim override (2026-09-14)

**The bug.** MFM Music sample 2 (moonsound_2.trd) played "only PCM, with
totally wrong timings" on the Pentagon. Port trace + demo sources pinned it:
the player's FM primitives use the immediate forms with the register number
in the high byte — `MBPlayer_out_fm1: ld a,c / out (#C4),a / out (#C5),a`
(mfm_player.asm 1817–1836) and `in a,(#C4)` status polls. On the loose
Pentagon decode those dirty aliases double-deliver into motherboard state:

| Raw port | Motherboard decode (rule) | Damage |
|---|---|---|
| `#04C4` (91×/frame) | `#7FFD` (0x8006/0x0004: A15=0, A2=1, A1=0) | pages RAM bank 4 into `#C000` — the song data — every frame |
| `#81C5` (91×/frame) | `#BFFD` (0xC002/0x8000) | spurious AY register writes |
| `#xxC4/#xxC6/#xx7E` | `#FE` (A0=0) | border/beeper/keyboard-arm traffic |
| `#xxC7` | Beta-128 `#FF` rule (0x83/0x83) | FDC drive-select alias |

The wave ports `#7E/#7F` never clash with `#7FFD`/AY (A1=1) — which is
exactly why PCM still played. On stock loose-decode clone hardware the demo
would corrupt identically; the card's real host class (Nemo-bus era,
+2A/+3-style refined decodes, Scorpion GAL) resolves this with **stricter
decoding while the card is attached** — and that is what the emulator now
models.

**Analysis API** (base `PortDecoder`, table-driven models only — if-chain
decoders have no rule table to analyze):

- `FindFullDecodeClashes()` — every overlap between the exported decode
  rules and the registered low-byte claims (`PortDecodeClash`: rule,
  claim, sample address satisfying both, `fdcProtected` flag).
- `FindFullDecodeRuleResolutions()` — per clashing rule: the claim set and
  the **minimal separating mask** — the smallest ≤3-bit tightening keeping
  the canonical port decoded while excluding every claim
  (`PortDecodeRuleOverride`). Pentagon + MoonSound: `#7FFD/#FFFD/#BFFD`
  each separate with one bit (A3: `0x8006|0x08 / 0x0004|0x08` — the +2A/+3
  refinement), `#FE` needs two (`{A3,A7} = 0x0088/0x0088` — no single bit
  separates `#C4,#C6,#7E` from `#FE`), and the Beta-128 `#FF` rule reports
  `precedenceRequired` (session-gated, never mask-tightened).
- Known gap: the Pentagon BDI fallback (`0x83/0x03` → `#1F/#3F/#5F/#7F`)
  is not part of the exported rule table, so the `#7F` wave-data overlap
  with the FDC data register is not reported by the analysis — it is
  protected at runtime (below) and FDC-owned regardless.

**Runtime claim override.** `OverrideDecodeForFullDecodeClaim()` is called
from every model decoder (`DecodePortIn`/`DecodePortOut`) right after the
model decode, before dispatch. When a card claims the raw port's low byte,
the motherboard partial decode **stands down** for that cycle: `#7FFD` no
longer pages, AY no longer sees `#BFFD/#FFFD` aliases, the `#FE` arm keeps
its border/keyboard state. The claiming observer was already serviced by
the Z80 I/O funnel tap, so the cycle belongs to the card exactly once. On
reads the decoder returns the observer's **cached** funnel value
(`NotifyFullDecodeIn` caches it — the card's stateful status register is
never read twice). Guarantees:

- **Beta-128 ports keep TR-DOS session precedence (R6)**: the override
  never claims a Beta-128 register decode (table models check the resolved
  port via `IsBeta128Port`, if-chain models check the raw placeholder), so
  inside a DOS session the FDC still owns `#1F/#3F/#5F/#7F/#FF` — with the
  card observing the shared-bus cycle, like real hardware.
- **Canonical clean writes are untouched** (`#7FFD`, `#FFFD/#BFFD`, `#FE`
  have unclaimed low bytes).
- **No card, no change**: with no low-byte claims registered every model
  decodes exactly as before (pinned by `NoClaim_LooseDecodePinned`).

Read claims were extended to match: `portDeviceClaimsRead` now also claims
`#C4` (FM1 status — the register exists unconditionally; guests poll
BUSY/LD there before NEW2 arming). Port-trace attribution marks claimed
cycles as device `FullDecodeClaim` with flag `kFullDecodeClaimed` (CSV
column `full_decode_claim`, JSON `full_decode_claim`, CLI flag letter `C`)
so the card's traffic is visible in diagnostics instead of blaming
"unmapped".

Covered by `core/tests/emulator/ports/fulldecodeclaim_test.cpp` (17 tests):
exact clash set and minimal masks, dirty `#04C4/#81C5` writes standing down
with claims active, baseline double-delivery without claims, canonical
writes unaffected, single FM status read, FDC interplay both ways, and the
if-chain decoder path (Spectrum 128).

---

## 3. Device lifecycle

### 3.1 Construction

```cpp
// SoundManager::SoundManager — as implemented (2026-09-14)
#ifdef UNREALNG_HAVE_OPL4
    if (_context->config.sound.moonsound)
    {
        _moonsound = new SoundChip_Moonsound(_context, _coreRate);
        const float vol = std::clamp(_context->config.sound.moonsound_vol, 0, 8192) / 8192.0f;
        _devices.push_back({AudioSourceType::Moonsound_FM,  "MoonSound FM (OPL3)",  ..., vol, ...});
        _devices.push_back({AudioSourceType::Moonsound_PCM, "MoonSound PCM (wave)", ..., vol, ...});
        enableWideMix(true);   // D7: a full-scale 16-bit source joins the mix
    }
#endif
```

`UNREALNG_HAVE_OPL4` is a **PUBLIC** compile definition on the core target, so
the app and core-tests (which compile core sources directly) see the same gate
— a mismatch would be an ODR hazard. Wave-ROM misses log a **warning** and
zero-fill (D10). Note the inverse probe is unsound: a *clean* ROM load logs
nothing, so a silent app log does **not** mean the chip was absent — that
inference misled the first pass over §12.5 (see its resolution).

Parsed in `Config::ParseConfig` alongside `CovoxFB`:

```ini
[SOUND]
MoonSound    = 1        ; enable ZXM-MoonSound (YMF278B / OPL4)
MoonSoundVol = 8000     ; 0..8192, legacy scale

[MOONSOUND]
WaveRom      = yrw801.rom   ; path relative to the ROM directory; optional
RamSizeKb    = 1024         ; ZXM-MoonSound fits 2 x 512 KiB SRAM
RenderMode   = hifi         ; hifi | authentic (default hifi; see 2026-09-18-2045-opl4-output-stage-harshness.md)
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

**Verified 2026-09-14 (rev 5).** The full chain — chip block mix, in-chip 16-bit
rail, headroom trim, `MoonSoundVol`, wide bus, `MasterLimiter` — is now pinned
by three device tests (§12.7): a full-scale FM voice plus a full-scale PCM slot
at the chip's reset mix stays inside the limiter's **linear** region (nominal
material never engages the compressor); at the loudest chip-legal mix
(0xF8 = 0 dB) the master rides the soft curve and stays under the 32000
ceiling; and with all 9 bank-0 FM channels plus all 24 PCM slots maximised the
group sums rail inside the chip and both sources still arrive at the mixer
under the trim — over-gain is structurally impossible. The −6 dB provisional
value is structurally verified; the perceived-loudness A/B against a real-card
recording remains the open calibration item.

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
`2026-09-13-0217-opl4-ttd-integration-tdd.md` §5 and §9 for the mechanism, the framework change
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

**Status 2026-09-14 (rev 3):** phases 0–4 and the TTD Tier-A blob are
implemented; 11 device tests, 2 guest-level integration tests
(`moonservice_guest_test.cpp`) and the full suite (2666 tests, 20 shards) are
green. The phase-3 gate is met beyond protocol level — the author's own
MoonService v0.3a **binary** now detects YM278B when executed as guest code
inside core-tests (§12.5), with arbitration unit-verified (§2.4) and the port
decode matching the card's A0..A7 CPLD wiring (§2.5).

**Status 2026-09-14 (rev 5):** verified end to end on real material — the
author's full 26-disk corpus boots and plays FM + PCM in-app (§12.6); 14
device tests including the gain/no-clip set (§12.7), 2 MoonService guest
tests (§12.5) and the 17-test claim suite (§2.6) green; the 2960-test suite
is green except the §12.8 demo-disk boot harness test (disk subsystem, not
the card). Remaining: §12.8, TTD Tier B (wave SRAM paged region), character
presets A/B (phase 7), VGM export (phase 8).

---

## 11. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| CPLD port-decode behaviour unknown (§2.2) | ULA/paging corruption under MoonSound software | **Decode width verified** against the author's driver: A0..A7 only (§2.5), implemented as low-byte observer registration. Read side resolved by the NEW-gated claim (§2.4), verified against MoonService v0.3a (§12.1). Write-side shadowing resolved by the claim override (§2.6): the motherboard decode stands down for claimed low bytes while the card is attached — the strict-decode host behaviour the card was designed for; Beta-128 session precedence preserved |
| No real ZXM-MoonSound available for reference | Board analog model and BUSY/LD timings stay unverified | Ship with those components disabled/datasheet-derived and clearly marked; treat hardware access as a project dependency, not a nice-to-have |
| SRAM upload timing wrong | Loaders run at wrong speed; timing-sensitive software breaks | Model LD busy per access from phase 3, not as a later refinement |
| Limiter audibly ducks PCM transients | The device sounds worse than a naive implementation | Tune attack/release against percussive material specifically; A/B against the unlimited path with headroom |
| Core rate other than 44100 becomes the default | Every user gets resampled OPL4 output | Log a recommendation (D6); consider surfacing it in the UI when MoonSound is enabled |

---

## 12. Verification log (2026-09-14)

Ground truth for this section: **MoonService v0.3a** — the ZXM-MoonSound card
author's own service/diagnostic software. Assembly source (user-provided) lives
in `scratch/Moonsound service v0_3a/`; the bootable disk
`moonservice_v03a.trd` came from the MickLab archive
(`testdata/sound/moonsound/`, provenance in that directory's `SOURCES.md`).

### 12.1 Detection protocol (from the author's assembly source)

Base ports: FM1 `#C4/#C5`, FM2 `#C6/#C7` (status via `IN A,(#C4)`), wave
`#7E/#7F` — matching §2.1. Entry at `org #6000`:

```
di; ld a,10h; ld bc,7FFDh; out (c),a    ; pages RAM bank 0 into the #8000
                                         ; window and — PC ≥ #4000 — closes
                                         ; any open TR-DOS session
                                         ; (CF_LEAVEDOSADR), handing #7F
                                         ; reads to the card (§2.4)
call init_card
```

`init_card`, in order:

| Step | Access | Purpose |
|---|---|---|
| 1 | `IN A,(#C4)` | card present if `≠ #FF` (floating-bus test) |
| 2 | FM2 `#C6←04, #C7←00` then `#C6←05, #C7←03` | reset bank 1, arm `NEW2|NEW` |
| 3 | FM1 `#C4←BD, #C5←00` | clear rhythm/register window |
| 4 | wave `#7E←02, #7F←10` | select register 2 for read-back |
| 5 | `IN A,(#7F)` | `A & #E0` = device ID; **`#20` = YM278B** |

The result is parked at `MoonService_dev_id` (`#88D0`). The service then reads
the flash JEDEC ID (`555←AA, 2AA←55, 555←90`, reset `F0` — hang-safe) and tests
SRAM before dropping into the menu key-poll loop.

**How to tell detection passed.** The service UI is drawn with a custom VideoDRV
font that OCR cannot read, and the UI is drawn **even when the chip is unknown**
(a live run with `dev_id = #E0` still painted the full window frames) — so
"reached the menu" is necessary but *not* sufficient. The authoritative checks:

- `MoonService_dev_id` at `#88D0` equals `#20`;
- porttrace shows the card-port accesses (`#C4..#C7`, `#7E/#7F`) after the FM2
  register-5 arming writes.

Address map of `MoonService.bin` (10451 bytes, `#6000–#88D3`): menu key-poll
loop `MoonService_key_pressed` at `#6280`; `MoonService_busy` at `#645A`; all
error paths park in `MoonService_press_anykey` (`#64xx`) — a PC parked in the
`#64xx` range means the chain *failed*, in `#6280–#62FF` that it ran on.

### 12.2 Unit status

- 22 `MoonSoundDevice` tests green: bus arbitration (§2.4), dirty-alias
  handling, render split (D5), TTD Tier A, the rev-5 gain-staging set (§12.7),
  and the 8 conformance canaries promoted from the PoC sweep families
  (inventory in the core TDD §12.2): FM TL ladder, FM envelope stages, FM
  waveform symmetry, PCM loop E=0 one-shot, PCM pan row, PCM TL 0x7F special,
  block-mix fields, SRAM window round-trip — each a thin per-field row driven
  through the real port funnel and frame lifecycle. The canaries drain the
  device's one-frame-stale delivery FIFO (`FlushAudioPipe`, 2–3 frames) after
  each register write before measuring: between-frame writes reach the
  published buffers only once the in-flight audio drains (observed FM ~2
  frames, PCM ~1).
- **2026-09-15 recalibration** (PoC sweep fixes landed in `libopl4`): the FM
  modulator-depth fix re-scaled the FM block to its datasheet level (a full
  voice ≈ 1/4 rail through the default −9 dB mix), so the gain-staging
  expectations were re-banded — default-mix master sum > 9000
  (PCM-dominated); the unity-mix test now asserts the +9 dB lift over the
  default mix plus the soft ceiling (renamed
  `..._UnityChipMix_MasterStaysUnderSoftCeiling`; the limiter's compression
  region is exercised by the all-voices-maxed test); the maxed test asserts
  the PCM group rails at the chip's 16-bit DAC boundary (≥ 30000) instead of
  the obsolete source-buffer halving (the −6 dB device trim lives in the
  mixer gain, §5.3, not in the source buffers). `WavePorts` reg-3 read-back
  now expects the hardware-verified 6-bit latch mask (`0xAB → 0x2B`), and
  `KeyOnPcmSlotThroughPorts` writes slot RC/RR at the correct stride-24
  register (`0xC8 + slot`; `0xD8 + slot` decodes to slot 16 — coincidentally
  harmless only because the written value is the reset default).
- 2 `MoonServiceGuest` tests green (§12.5), 17 `FullDecodeClaim` tests (§2.6),
  10 `MasterLimiter` + 15 `DeviceMixer` tests.
- Full suite 2958 tests green **except the open §12.8 demo-disk boot test**
  (unchanged) — and still only trustworthy **after killing the running app**:
  WebAPI-port tests collide with any app holding TCP 8090, and a stale app
  produces phantom shard failures that look like regressions.

### 12.3 Live-harness facts (WebAPI)

- **Disk chain.** `moonservice_v03a.trd`'s catalog sits at TRD file offset
  `#0000` (not `#200`): a 1-sector `boot` file plus `MoonServ`. The boot
  payload (file offset `#1000`) is a **MAXBOOT v9.1** loader that shows an
  intro and loads the service on ENTER. TR-DOS `RUN` of this disk is flaky
  in-emulator: the ROM parks in the DRQ wait (`#3E44` OUT loop /
  `#3EF3–#3EFE` wait-and-read in `trd504tm.rom`). That is a disk-loader
  issue, independent of the card.
- **Direct load is the reliable harness.** Run a paging stub at `#6000`
  (`ld a,10h; ld bc,7FFDh; out (c),a; jr $`), then write the image at `#6000`
  and set PC there. During a TR-DOS session `#4000–#7FFF` is always RAM bank
  5, and this emulator's Pentagon model pages the `#8000` window from
  `p7FFD & 7`, so the stub guarantees the image tail is visible before the
  write. Verified: the service runs and paints its UI this way.
- **Porttrace semantics.** Events carry both `raw_port` and `decoded_port`
  (FDC traffic shows as raw `#xx7F` → decoded `#7F`) plus `pc`, `cf_trdos` and
  `beta128_gated` — the right tool for arbitration debugging.
- **Input/API quirks.** Keyboard `tap` defaults to `frames=2` and
  **double-registers** characters (`RUN` becomes `RUNun`); use press/release
  pairs with ~120 ms hold to type. Register writes take a numeric value
  (`PUT /registers/pc {"value": 24576}` — a hex string throws server-side).
  Memory reads use `?len=`. `capture/ocr` is useless on the service's custom
  font; `capture/screen` (base64 GIF) still works for eyeballing.

### 12.4 Config resolution chain (verified during the app investigation)

- Search order: executable directory, then the macOS bundle `Resources/`.
  The bundle build resolves `configs/pentagon128k/unreal.ini` from
  `Resources/`; the staged copies (`bin/configs/…` and
  `…/Resources/configs/…`) are byte-identical to `data/configs/…`.
- The shipped ini files are **CRLF heritage Windows files with inline `;`
  comments**; the vendored SimpleIni handles both (`\r` is whitespace,
  `strtol` stops at the comment). Verified with a standalone probe against
  the exact staged file: `SOUND.MoonSound = 1`, `SOUND.MoonSoundVol = 8000`,
  `ROM.MOONSOUND = rom\opl4\yrw801-m-yamaha-1993.rom` (file renamed from the
    archive's spaced `YRW801-M - Yamaha - 1993.rom` on 2026-09-14, per the
    no-spaces kebab-case naming rule),
  `MOONSOUND.WaveRom` absent, `BETA128.beta128 = 1`,
  `ROM.PROFROM = rom\scorp_prof401.ROM:0`. `CSimpleIniA` compares section
  and key names case-insensitively.
- Model → folder: `PENTAGON` default RAM is 128, so `GetConfigFolderForModel`
  returns `pentagon128k` (RAM ≥ 512 would select `pentagon512k` — keep both
  folders' `[SOUND]` in sync when keys change).
- `ParseConfig` demonstrably runs in the app: the ProfROM quadrant-strip
  warning fires at instance creation in the app log.

### 12.5 Resolved: guest I/O never reached the card (low-byte decode)

Symptom: app-created PENTAGON instances showed a Z80 probe stub (arm NEW,
read `#7F`/`#C4`) returning `#FF`/`#FD` — floating bus — and a direct-loaded
MoonService reading `dev_id = #E0`.

The chip **was** constructed all along. The first-pass inference
"no wave-ROM warning ⇒ no construction" was wrong: the YRW801 loads
cleanly, and a clean load logs nothing (see the §3.1 note). The real defect
was one layer down, in the port path:

- **Root cause.** The card was registered as an exact-16-bit full-decode
  observer (`#00C4`…`#007F`). Guest instructions never produce those
  addresses: the Z80 immediate forms `out (n),a` / `in a,(n)` execute at
  `(a << 8) | n` (`op_noprefix.cpp`, OUT at :1236, IN at :1332), so
  MoonService's `in a,(#7F)` with `A = #10` reads port `#107F`, its FM
  register selects run at `#BDC4` etc. Every guest cycle missed the observer
  map; the model decode found nothing armed; the floating bus answered `#FF`;
  `#FF & #E0 = #E0` — byte-exact the app symptom. The author's own helpers
  (`ld a,d / out (n),a` at `#6437/#6440/#6449`) *rely* on the card ignoring
  the dirty high byte.
- **Fix.** Low-byte full-decode registration (§2.5): the notify taps fall
  back to a `port & 0xFF` table, the chip dispatches on the low byte, and
  the `#7F` read claim compares the low byte. Host-side exact calls still
  work — their low byte is the card port.
- **Verification (2026-09-14).** Guest-level integration tests in
  `core/tests/emulator/sound/moonservice_guest_test.cpp`, both green:
  `GuestCode_RunsAuthorDetectionProtocolAndDetectsYm278b` (the init_card
  sequence as hand-assembled guest code through the production stack —
  result `#30`, device-ID bits `#20`) and
  `AuthorBinary_MoonServiceV03a_DetectsYm278b` (the author's actual
  10451-byte image executed from `#6000`: `dev_id @ #88D0 = #20`, PC stays
  inside the image). Plus the dirty-alias assertions inside
  `SharedBus_ArmedCardOverridesLegacyMirrorAt7F` (§2.4). Full suite after
  the fix: 2666/2666 across 20 shards, zero warnings.

The "ruled out" list from the investigation remains valid — staged config
content and parse (§12.4), QSettings, the `UNREALNG_HAVE_OPL4` gate, timing
defaults, custom config path, model folder — all of them were correct; none
of them was the fault.

### 12.6 End-to-end demo verification — the author's full disk set (2026-09-14)

Ground truth upgraded from one service binary to the card author's complete
published software set: **26 runnable TR-DOS disks** in
`testdata/sound/moonsound/` (provenance and per-disk credits in that
directory's `SOURCES.md`):

| Set | Disks | Content |
|---|---|---|
| `moonsound` + `moonsound_2..14` | 14 | demo/music collections (MoonBlaster MSX material, MWM/MFM formats) |
| `mfm_sample`, `mfm_sample_2..4` | 4 | MFM sample collections (MFM Music sample 2 = `moonsound_2.trd`, the §2.6 reproducer) |
| `moonmusic_1/2` (+ `u` Unreal variants) | 4 | music collections — Bart Roymans (1), AAA co-author (2) |
| `moonservice_v01/v02/v03/v03a` | 4 | the diagnostic utility (§12.1) |

**Status: every disk boots and plays, with both engines audible (FM + PCM)**
— verified in-app on 2026-09-14 in the original bug-report environment
(ZX Evo BaseConf / ATM3, Pentagon timings), after the three fixes this
document tracks: the low-byte decode (§2.5/§12.5), the §2.6 claim override
(MFM Music sample 2's "PCM only, wrong timings": the dirty `#04C4/#81C5`
aliases were paging the song data away every frame), and the Revision-4
openMSX alignment (bus/loop semantics, commit `5426b4b1`).

Why this corpus is the right gate: real guest software from the card author,
exercising card detection, SRAM upload, wave-ROM playback, FM+PCM mixing,
keyboard-driven tune switching and 128K paging — on the exact hardware
generation the card targets.

Harness asymmetry, stated plainly: in-app the full corpus works, while the
core-test re-verification of one representative disk still stalls in the
disk subsystem (§12.8). The card stack itself is covered in the harness by
§12.1/§12.5 (detection, protocol, binary) and §12.7 (audio path) — §12.8 is
the last open item of the demo verification.

### 12.7 Gain staging and clipping — test-pinned (2026-09-14)

Three device tests in `soundchip_moonsound_test.cpp` pin the §5.3 chain end to
end, all through the real port funnel and the real frame lifecycle (wide bus
+ limiter exactly as production):

| Test | Scenario | Proves |
|---|---|---|
| `FrameEnd_FmAndPcmFullScale_DefaultChipMix_StaysInLimiterLinearRegion` | full-scale FM voice + full-scale PCM slot together, chip reset block mix (FM −9 dB, PCM 0 dB) | each source ≤ half scale under the trim; the summed master stays **below the knee** — nominal material never engages the compressor |
| `FrameEnd_FmAndPcmFullScale_UnityChipMix_MasterSoftLimitedNeverClips` | same voices, block-mix 0xF8 = 0 dB (the loudest the chip allows) | the sum rides past the knee; the soft curve holds the master under the 32000 ceiling — no hard clip anywhere |
| `FrameEnd_AllFmChannelsAndPcmSlotsMaxed_MasterNeverClips` | all 9 bank-0 FM channels + all 24 PCM slots at TL 0, unity mix | group sums rail at the authentic in-chip 16-bit DAC boundary; sources still arrive under the trim; master still ≤ ceiling |

Measured reference points: FM single voice at unity mix ≈ 16384 in the
buffer (engine rail 32768 × trim 0.5); PCM full-scale square ≈ 16384;
default-mix master ≈ 21600 (< knee 24576); unity-mix master ≈ 29250
(`LimitSample(32000)`, under the 32000 ceiling, never 32767).

Test-writing facts pinned on the way:

- The master DC blocker (~5 Hz) eats a DC PCM loop within a frame —
  master-mix tests must upload a **zero-DC square** (4× +FS then 4× −FS,
  end 8 stored as its complement `0xFFF8`), not a constant-level loop.
- The YMF278B wave register file is **interleaved**: `reg = group base +
  slot` (group = `(reg−8)/24`, slot = `(reg−8)%24`) — not `slot*8 + offset`.
  Slot 0 coincides with both encodings, so a wrong helper passes every
  single-slot test and silently corrupts every multi-slot one.
- Test helpers live in the file's anonymous namespace:
  `KeyOnFmChannelThroughPorts` (parameterised reference voice, backend-aware
  register maps), `UploadSquareToneThroughPorts`, `KeyOnPcmSlotThroughPorts`.

### 12.8 Open: TR-DOS demo-disk loader stalls at the double-sided track boundary (Pentagon harness)

The core-test re-verification (`moonsound_demo_guest_test.cpp`,
`AuthorDisk_Moonsound2_PlaysFmTrafficWithoutPagingAlias`) boots
`moonsound_2.trd` through the real TR-DOS 6.10E chain on the **Pentagon**
model. The boot sector and catalog load cleanly (52 sectors, 13 312 `ini`
bytes), the shipped player starts and plays AY music — but the tune load
never advances: **all FDC activity stops around frame ~157** with the loader
re-reading logical track 1 and never seeking track ≥ 2. `fmTotal = 0`
because the MBPlayer tune data never arrives; the card is idle, not broken
(`lastCardFrame = 157`, zero card-port traffic afterwards).

Root-cause state (TR-DOS 6.10E disassembly in `scratch/trdos610e.asm`):

- The ROM position loop (0x1E70–0x1EAB) is pure RAM arithmetic
  (5CF4 sector 0–16 with wrap → 5CF5 logical track) — the observed restart
  is a caller retry, not arithmetic.
- Routine 3E63 maps logical → physical tracks for **double-sided** disks
  (3E11 disk-type bit 1 → 3EAA: `cyl = L>>1`, `side = L&1`; odd tracks
  side-flip through the `#FF` write `0x2C` at 1FF6). t0 = cyl0/side0,
  t1 = cyl0/side1, t2 = cyl1/side0 — the t1→t2 boundary is the first
  **real head step** plus a side flip, and exactly there the read fails and
  the ROM restarts track 1 (re-seek + re-read s1..s6, FORCE INTERRUPT at
  3EC9, catalog re-scan, then quiet).
- All 26 corpus disks are double-sided (655 360 bytes), so every demo's tune
  load crosses this boundary in the harness.

Prime suspects (unverified): the Beta-128 side-bit semantics in
`processBeta128` (`#FF` bit 4, inverted), the 3E11 disk-type probe (the
volume-sector-9 diskType byte as our TRD loader populates it), and the 3D30
real-seek path. The in-app corpus runs that work used the ATM3 configuration
— a hint that the defect may be model-specific (Pentagon Beta-128 gating),
not core-FDC-wide, but that is unconfirmed.

This is a disk-subsystem defect chain independent of the card. The
diagnostic instrumentation that established the above (FDC/`#FF` traces with
FSM state, Z80 freeze dumps, port histograms) currently lives uncommitted in
the test file; strip it to the useful minimum before any commit of that
file.
