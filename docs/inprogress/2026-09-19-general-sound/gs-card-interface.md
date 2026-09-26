# GS Card Personalities: Interface + Lightweight Implementation

Status: design (2026-09-21). Superseded by the full as-built TDD:
`gs-card-personalities-tdd.md` (this file stays as the historical design
input). Parent materials: `gs-tdd.md`,
`verification-findings-and-bugs.md`, `materials/gs/gs-programming-guide.md`.

## 1. Goals

1. One contract - `GeneralSoundCard` - behind which the Z80 LLE card, a new
   **lightweight** (LW) mod-player card and the future NeoGS card are
   interchangeable for SoundManager, TTD and all five automation surfaces.
2. LW card = protocol-faithful HLE: reacts on the host command interface
   (mailbox #B3/#BB/#33), no second CPU, no firmware ROM, **no BASS library**
   (upstream's `MOD_GSBASS` intent, rewritten in-tree).
3. Fidelity contract (user decision): mailbox/reply behavior bit-exact against
   the LLE card; audio compared by tolerance (RMS/envelope per channel), not
   bit-exact.
4. v1 command coverage (user decision): music set + graceful no-ops for SFX.
5. Runtime, feature-driven personality switching without touching configs.
6. Full compliance with unreal-ng sound-generation principles: shared
   `AudioFrameDescriptor` + `blip_t` pipeline, `setSampleRate` live rebuild,
   turbo suppression path, frame lifecycle, `gs_vfx` volume curve, activity
   HUD notifications.

## 2. Personality matrix

| Capability | LLE (`SoundChip_GeneralSound`) | LW (`SoundChip_GSLightweight`) | NeoGS (reserved, P2) |
|:--|:--|:--|:--|
| Second Z80 + firmware | yes (z80ex + gs105a.rom) | no | FPGA model (neogs-tdd.md) |
| Module engine | firmware QUANTUM/QTPLAY | in-tree ProTracker player | firmware + SD/MP3 |
| Mailbox protocol | exact (FIFOs, split bit7) | exact (shared `GSForwardMailbox`) | expected shared |
| Reply bytes | firmware-true | bit-exact vs LLE (probe-captured) | TBD |
| SFX commands | full | v1: parse + ack-shaped no-op | full |
| POST timing | real 0.3-1.1 s (BUG-6 visible) | skipped (idle signature prompt) | TBD |
| TTD peripheral | `GeneralSound` (=5) | `GeneralSoundLightweight` (=11) | `NeoGS` (=12, reserved) |
| `hasCoprocessor()` | true | false | true |
| Config `[SOUND] GSType` | `Z80` | `LW`/`LIGHT` (`BASS` = deprecated alias) | `NGS` (parses, P2) |

## 3. Interface contract (`core/src/emulator/sound/chips/gs/generalsoundcard.h`)

```cpp
enum class GSCardImplementation : uint8_t { LLE, LW, NGS };

class GeneralSoundCard : public PortDevice, public ttd::TTDSerializable
{
public:
    // canonical host port keys (moved up from the LLE card; concrete keeps aliases)
    static constexpr uint16_t PORT_DATA = 0x00B3;
    static constexpr uint16_t PORT_COMMAND = 0x00BB;
    static constexpr uint16_t PORT_CONTROL = 0x0033;

    // lifecycle
    virtual void reset() = 0;               // power-on
    virtual void resetCard() = 0;           // #33 bit7 semantics
    virtual void hostReset() = 0;           // ZX reset line (GSReset=1)
    virtual void loadROM(const std::string& romPath) = 0; // LW: warn + ignore

    // audio frame lifecycle (SoundManager calls)
    virtual void handleFrameStart() = 0;
    virtual void handleFrameEnd(size_t expectedSamples = 0) = 0;
    virtual int16_t* getBuffer() = 0;
    virtual void setSampleRate(size_t sampleRate) = 0;     // live rebuild
    virtual void setSynthesisSuppressed(bool) = 0;         // turbo path

    // mailbox automation (flush-first host-port semantics)
    virtual uint8_t readStatus() = 0;
    virtual uint8_t readData() = 0;
    virtual void sendCommand(uint8_t cmd) = 0;
    virtual void sendData(uint8_t data) = 0;
    virtual void triggerNMI() = 0;

    // introspection
    virtual uint8_t getStatusRaw() const = 0;
    virtual uint8_t getDataFromHost() const = 0;
    virtual uint8_t getDataToHost() const = 0;
    virtual uint8_t getCommandFromHost() const = 0;
    virtual size_t getCommandQueueCount() const = 0;
    virtual size_t getDataQueueCount() const = 0;
    virtual uint8_t getMPAG() const = 0;                   // LW: 0
    virtual uint8_t getChannelSample(int) const = 0;       // DAC latch view
    virtual uint8_t getChannelVolume(int) const = 0;
    virtual bool isROMLoaded() const = 0;                  // LW: false
    virtual size_t getRamSizeKB() const = 0;               // LW: virtual size

    // coprocessor capability (LLE-only truth; others return zeros/false)
    virtual bool hasCoprocessor() const = 0;
    virtual GSCardImplementation implementation() const = 0;
    virtual bool isCPUHalted() const { return false; }
    virtual uint16_t getCPUReg(Z80_REG_T) const { return 0; }

    // diagnostics (identical data model on every personality)
    virtual const GSActivityCounters& getActivityCounters() const = 0;
    virtual void resetActivityCounters() = 0;
    // port-trace session: start/stop/pause/resume/clear/getters (pure)
};
```

Notes:
- `getCPUReg(Z80_REG_T)` on the interface forces `z80ex.h` into every
  consumer; acceptable - `soundchip_gs.h` already does, and `gsporttrace.h`
  (included for counters) sits below it. Callers that never touch the CPU
  still include the header transitively via `soundmanager.h`.
- Automation verbose views gate on `hasCoprocessor()` and print
  `(no coprocessor - lightweight)` instead of registers.
- `SoundChip_GeneralSound` keeps its exact class name (tests, TTD ids,
  automation history) and simply gains the base.

## 4. LW command interpreter (protocol state machine)

Firmware truths this must reproduce (verification doc sections 2.1-2.4):

- **Data-first rule**: COM16/COM13/COM30/COM31 consume their parameter via
  DATRG at dispatch time; host order is `OUT (#B3), data` then `OUT (#BB), cmd`.
  The shared mailbox FIFOs already preserve this for bursts and param-first
  trios (`00,31 40,2B 25,2A`).
- **WTDTL abort path**: the loader wakes on ANY flag; a queued command
  mid-stream aborts the handler (LOADCM drain semantics). LW mirrors this:
  a new command during a COM30 stream aborts the load unless it is `D2`.
- **COM30 loads+parses only** (PLAYMD); **COM31 starts** (BUSY=#FF,
  CURMOD=param or current, MTSTAT=3, INITPAT/EFXGTNT, PROCESS=#FF).
  COM31 param semantics: P != 0 -> module P else current; P > CNTMOD ->
  error path zeroes CURMOD (LW: reject start, keep parsed module).
- **Sequencer**: 37.5 kHz quantum is the sample clock; tempo tick
  `TICKLEN=750` quanta = 20 ms = 50 ticks/s at default; each EFXINT tick
  advances rows/effects; each quantum descriptor feeds one `LD A,(DE)` DAC
  fetch per interrupt.

LW dispatch table (v1, rows verified against the in-tree firmware source
`materials/gs/gs-firmware/firmware/src/{COM_L,COM_H,INIT_H,LOAD_L}.a80`):

| Command | Action | Reply (via #B3, OUTRG) |
|:--|:--|:--|
| 00 | reset flags (consumes 1 dummy param) | none |
| 01/02/03 | DAC mid / VOL latches 0x3F / VOL latches 0 | none |
| 04/05/06/07/09/0A/0B/0C/0D | direct channel select + data/volume latch family | none |
| 0E | covox stream: data latches DAC0+DAC2, exit on next command | none |
| 0F | stereo covox ('Y'): v1 documented no-op (consumes 1 param) | none |
| 13 / 16 / 17 / 18 | jump / putByte / getByte / memPtr: memory-model no-ops | 17: 0x00 (no RAM window) |
| 20 / 21 | virtual total/free RAM query | 3 bytes L,H,C (free = total - store); 21 clears ERRCODE |
| 22 / 23 | page byte peek / page count | 22: 0x00; 23: `pages - 1` (NUMPG) |
| 2A / 2B / 34 / 35 | MODVOL / FXVOL / MODFADE / MTVOL get-then-set (2A/2B/35 clamp 0x40) | old value |
| 2C / 2D / 2E | CURMOD / CURSMP / CURFX selector (0=count, >CNT -> 0) | old value |
| 30 | fresh module slot: CNTMOD=1, reply 1, consume 1 param, enter load; CNTMOD!=0 -> INITVAR (soft reset, no reply) | 1 |
| D2 (in load state) | LOAD3: finish stream, parse store | none |
| 31 | start playback (P=0 -> CURMOD; P>CNTMOD or no module -> error zeroes CURMOD) | module # on success, 0x00 on error |
| 32 / 33 | stop (freeze position) / continue; reply old MODULE | old MODULE |
| 36 / 37 | query 0xFF / full module-system reset (stop, clear store, CURMOD/CNTMOD/MODULE=0) | 36: 0xFF |
| 38 / 3E | FX upload (table-full -> reply 0); stream discarded, nothing stored | new FX # or 0x00 |
| 39 / 3A / 3B / 3C / 3D | FX select+play / chan-off mask / FXFADE x2 / FXMVOL | 39: 0x00 ok / 0xFF bad; 3B/3C/3D: old value |
| 40-49 | SFX channel fields: params consumed per firmware, nothing played | 42/45/46/47: 0x00 (fresh-RAM semantics) |
| 50 / 58 | SFX sub-command protocol: 1 param, then the next command byte is a selector (sel&7), then 1-3 data params (sel>=4 -> +1, sel==7 -> +1) | 58: 0x58 (its own command byte); 50: none |
| 80 / A0 | FX channel setup sub-command: 1 param (FX number), selector command byte, 0-2 data params (COM80 bits 3-4) / none (COMA0) | 80: 0xFF on bad selector |
| 60 / 61 / 62 | MTSNGPS / MTPATPS / combined (song<<6 & 0xC0 \| row & 0x3F) | 1 byte each |
| 63 / 64 | 4x CHREAL (0x7F = no sample) / 4x CHMVOL (row volume 0-63) | 4 bytes |
| 66 / 67 / 68 | FXF external tempo (param) / MTSPEED / MTBPM query | 67/68: 1 byte |
| F0 | ERRCODE query | ERRCODE |
| F3 / F4 | INITVAR / POST reboot: volumes 0x40, MTSTAT=0xC3, module system cleared, VOL latches 0x3F; F4 skips the 0.3-1.1 s POST window (prompt BUG-6 idle signature) | none |
| other (COMZ) | ack only | none |

Note: the programming guide's "#50 set global volume / #51 query" table
entry does not match the shipped firmware - COM50 is the SFX sub-command
protocol and there is no #51 handler (falls to COMZ). The LW card follows
the firmware (protocol-faithful decision); master volume lives on COM35
(MTVOL), music scaling on COM2A (MODVOL).

Reply-byte policy: exact values are **captured from the LLE card** by a probe
test (scripted traffic, record every `#B3`-readable byte) and codified in LW;
the cross-validation test then asserts bit-exact equality forever.

## 5. MOD player core (`gsmodplayer.h/.cpp`)

- Parser: ProTracker M.K./M!K! 31-sample header, pattern table, up to 127
  patterns (v1.05a), 4 channels. Effects implemented: 0xx arpeggio, 1xx/2xx
  slides, 3xx tone portamento, 4xx vibrato, 5/6/A volume slide + carries,
  7xx tremolo, 9xx offset, Bxx/Cxx position/pattern break, Dxx pattern break,
  E1x/E2x fine slides, EAx/EBx fine volume, ECx/EDx mute/retrigger, Exx
  invert-loop (parse-ignore, firmware parity), Fxx speed/tempo, E0x filter
  (parse-ignore). Sample format 8-bit signed, loop points honored.
- Scheduler mirrors the firmware: the card feeds wall-clock quanta (37.5 kHz
  domain); the player keeps `tickQuantaRemaining` (750 default, Fxx tempo
  rescales: tick = 37500/(0.4*bpm) quanta) and `speed` ticks/row. Per tick:
  apply row effects, then the card samples channel data each quantum.
- Volume model: per-channel latch 0-63 = row volume scaled by
  MODVOL*MTVOL >> 12 (the firmware VOL_H math before the 6-bit latch);
  MODVOL (2A) scales music channels, FXVOL (2B)/FXMVOL (3D) the future SFX
  path, MTVOL (35) is the module master. Output math is the card's
  `computeStereo` (below), identical to LLE.
- Timing domain: row/tick counters - **not** sample counters - so
  `setSampleRate` (blip rebuild) never disturbs musical position.

## 6. Audio pipeline compliance (LW card)

Byte-for-byte the LLE card's shape (`soundchip_gs.cpp` regions):

- Two `blip_t` accumulators clocked in the 12 MHz GS cycle domain; frame
  length `frameGsLength()` (turbo-invariant, same formula).
- Per-quantum emission: each 320-cycle quantum the player writes the four
  channel latches (`_channelData[4]`, centered 0x80) and volume latches
  (0-63), then `emitSample()` computes `computeStereo()` (channels 1,2 -> L,
  3,4 -> R, 50% cross-feed, `_vfx[vol]` curve from `gs_vol`) and adds blip
  deltas at the quantum position. Sample-and-hold cadence matches the LLE
  37.5 kHz DAC fetch character.
- `setSampleRate`: rebuild both blips (`blip_set_clocks_rate`), keep player
  position; `_lastL/_lastR` continuity re-anchored as the LLE does.
- `setSynthesisSuppressed(true)`: keep latch/level tracking, skip deltas
  (turbo mode contract).
- `handleFrameEnd(expectedSamples)`: run player to frame end, end blip
  frames, read exactly `expectedSamples` stereo pairs into the
  `AudioFrameDescriptor` buffer, zero-fill remainder, post `NC_AUDIO_ACTIVITY`
  HUD notifications on activity transitions.

## 7. Runtime switching (v1 semantics)

`SoundManager::switchGeneralSoundCard(GSTypeKind target)`:

1. Mixer-lock; unregister the three host ports.
2. Snapshot `GSForwardMailbox` (queues, latches, flags) + activity counters.
3. Personality handoff (bidirectional): both cards mirror the raw COM30..D2
   payload stream at the host-port layer as it goes by (independent of
   where the firmware parks it), so either direction replays it through the
   new card's own interpreter (module load re-executed; COM31 resumes
   playback if the outgoing card was playing). A switch mid-upload keeps
   the mailbox but loses the partial stream (documented limit).
4. Construct target card, restore mailbox, re-register ports, unlock.

FeatureManager: feature `gs_lightweight` (default off) toggles personality
without touching `[SOUND] GSType`. WebAPI GS control endpoint mirrors it
(`action=switch_personality`, personality param) - the same endpoint family
the MCP surface proxies. Switching mid-note: audio gap of one frame max
(mailbox-preserving), never a crash path; switching is safe under run.

## 8. Config + registration changes

- `platform.h`: `GSTypeKind::LW` after `BASS` (enum append; stored configs
  are string-parsed, never serialized raw).
- `config.cpp` `[SOUND] GSType`: `LW`/`LIGHT` -> LW; `BASS` -> LW +
  deprecation warning ("BASS HLE is replaced by the in-tree lightweight
  card"). `Z80`/`NGS` unchanged.
- `ttdserializable.h`: `GeneralSoundLightweight = 11` (NeoGS = 12 stays
  reserved; ids are append-only).
- SoundManager `_gs` becomes `GeneralSoundCard*`; factory
  `createGeneralSoundCard(kind, context)`; device registry entry unchanged
  (one `AudioSourceType::GeneralSound` slot for all personalities).

## 9. TTD

- LLE blob unchanged (PeripheralId 5).
- LW blob: mailbox (same 59..94 layout) + interpreter state + player state +
  raw upload store + parsed sample data. `TTDHashState` covers all of it.
  PeripheralId 10. A timeline recorded with one personality does not restore
  onto the other (id mismatch = clean error, by design).

## 10. Test inventory

`core/tests/emulator/sound/chips/soundchip_gslw_test.cpp`:

| # | Test | Asserts |
|:--|:--|:--|
| 1 | Mailbox parity | burst F4,30,D1 + param-first trio survive (queue counts, flags) |
| 2 | Reply fidelity vs LLE | scripted traffic through both cards; every readable #B3 byte equal |
| 3 | Memory queries | 20/21/23 return virtual geometry (512 KB: 23 -> 15) |
| 4 | Load + parse | synthetic M.K. module uploads, D2 parses, sample/pattern counts |
| 5 | Start/stop/continue | COM31 starts (dacFetches grow), 32 stops, 33 resumes |
| 6 | Volumes | 2A/2B/50 scale emitted levels (buffer RMS ratio) |
| 7 | Tempo | Fxx tempo change shifts tick rate (fetch-rate ratio) |
| 8 | Sample-rate change mid-play | output continues, player position survives |
| 9 | Suppression | turbo flag silences deltas, latches keep tracking |
| 10 | Cross-validation vs LLE | real firmware, 2 s of playback, per-channel RMS within tolerance |
| 11 | Switching | LW->LLE replay + LLE->LW mailbox-preserving stop (SoundManager-level) |
| 12 | BUG-6 shape | idle signature readable immediately after F4 (no POST window) |

Slow tests (10) get a justification comment (real firmware boot is the only
way to cross-validate; target < 3 s each).

## 11. Risks / open items

- Reply-byte values are empirical (probe LLE) - the probe test codifies them
  once, cross-validation keeps them honest.
- Effect coverage parity with the firmware player is best-effort; uncommon
  effect mismatches surface in cross-validation RMS drift, not crashes.
- NeoGS interface fit is asserted by design only (no P2 work here).
- `getCPUReg` on the interface drags `z80ex.h` into LW includes - accepted
  (single translation-unit cost, keeps automation type-simple).
