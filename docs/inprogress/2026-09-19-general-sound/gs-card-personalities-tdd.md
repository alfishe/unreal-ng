# GS Card Personalities: Interface, Lightweight Card and Runtime Switching

Status: as-built (2026-09-21). Phases 0-4 of the personalities plan are
implemented and gated (zero-warning build; GS filter green); Phase 5 (LW test
suite, cross-validation, live smoke) is in progress. This document is the full
as-built design record and supersedes the Phase 0 design snapshot
`gs-card-interface.md` (kept as the
historical design input, now carrying a supersession pointer). Parent materials: `gs-tdd.md` (LLE card TDD),
`verification-findings-and-bugs.md` (mailbox protocol truths, BUG-1..7),
`materials/gs/gs-programming-guide.md`, firmware sources under
`materials/gs/gs-firmware/firmware/src/` (v105b same dispatcher family as the
shipped gs105a ROM).

## 1. Overview

### 1.1 Goals

1. One contract - `GeneralSoundCard` - behind which the Z80 LLE card, the new
   **lightweight** (LW) mod-player card and the future NeoGS card are
   interchangeable for SoundManager, TTD and all five automation surfaces
   (state_audio_api, cli-processor-gs, lua_emulator, python_emulator, MCP via
   WebAPI JSON).
2. LW card = protocol-faithful HLE: reacts on the host command interface
   (mailbox #B3/#BB/#33) exactly like the gs105a firmware, with no second
   CPU, no firmware ROM and **no BASS library** (upstream's `MOD_GSBASS`
   intent, rewritten in-tree).
3. Fidelity contract (user decision): mailbox/reply behavior bit-exact against
   the LLE card; audio compared by tolerance (per-channel RMS), never
   bit-exact.
4. v1 command coverage (user decision): music command set fully implemented;
   SFX commands consumed protocol-faithfully and played as documented no-ops.
5. Runtime, feature-driven personality switching without touching
   `[SOUND] GSType` in stored configs.
6. Full compliance with unreal-ng sound-generation principles: shared
   `AudioFrameDescriptor` + `blip_t` pipeline, `setSampleRate` live rebuild,
   turbo suppression path, frame lifecycle, `gs_vfx` volume curve, activity
   HUD notifications.

### 1.2 Scope and v1 decisions

- In scope: interface extraction (no LLE behavior change), shared mailbox
  extraction (TTD byte-identical), the LW card + in-tree ProTracker player,
  `switchGeneralSoundCard` runtime switching, the `gs_lightweight` feature,
  the WebAPI `switch_personality` action, the LW test suite.
- Out of scope (v1): NeoGS (reserved enum + interface slot only), SFX
  playback on LW, SGen/UST formats (ProTracker MOD only, guide §6). Bidirectional
  module handoff (LW<->LLE) is IN scope: both cards mirror the raw COM30..D2
  payload at the host-port layer, so either switch direction replays it.
- Tolerated divergences (documented in §4.6): LW rejects malformed uploads
  with ERRCODE 0x10 where the firmware plays anything; COMF4 skips the
  0.3-1.1 s POST window (BUG-6 idle signature is prompt by design).

### 1.3 Personality matrix (as built)

| Capability | LLE (`SoundChip_GeneralSound`) | LW (`SoundChip_GSLightweight`) | NeoGS (reserved, P2) |
|:--|:--|:--|:--|
| Second Z80 + firmware | yes (z80ex + gs105a.rom) | no | FPGA model (neogs-tdd.md) |
| Module engine | firmware QUANTUM/QTPLAY | in-tree ProTracker player (`gsmodplayer.h`) | firmware + SD/MP3 |
| Mailbox protocol | exact (FIFOs, split bit7) | exact (shared `GSForwardMailbox`) | expected shared |
| Reply bytes | firmware-true | bit-exact vs LLE (fidelity test) | TBD |
| SFX commands | full | v1: parse + ack-shaped no-op | full |
| POST timing | real 0.3-1.1 s (BUG-6 visible) | skipped (idle signature prompt) | TBD |
| Coprocessor introspection | `hasCoprocessor()` true, live regs | false, regs read 0 | true |
| TTD peripheral id | `GeneralSound` (=5) | `GeneralSoundLightweight` (=10) | `NeoGS` (=9, reserved) |
| Config `[SOUND] GSType` | `Z80` | `LW`/`LIGHT` (`BASS` = deprecated alias) | `NGS` (parses, P2) |
| Runtime switch target | yes | yes | no (factory returns nullptr) |

### 1.4 Implementation status

| Phase | Scope | State |
|:--|:--|:--|
| 0 | design doc (Phase 0 snapshot) | done |
| 1 | `generalsoundcard.h`, LLE re-derives, SoundManager factory, `GSTypeKind::LW`, automation retarget | done, LLE tests unchanged-green |
| 2 | `gsmailbox.h/.cpp` (`GSForwardMailbox`), LLE delegates, TTD blob byte-identical | done |
| 3 | `soundchip_gslw.h/.cpp` + `gsmodplayer.h/.cpp` + `PeripheralId::GeneralSoundLightweight` | done |
| 4 | `switchGeneralSoundCard`, `gs_lightweight` feature, WebAPI `switch_personality` | done, gates green |
| 5 | `soundchip_gslw_test.cpp`, cross-validation, full gates, live smoke | in progress |

## 2. Architecture

### 2.1 Class topology

```
PortDevice, ttd::TTDSerializable
        |
GeneralSoundCard                      (chips/generalsoundcard.h)
   |-- SoundChip_GeneralSound          (LLE: z80ex + firmware, PeripheralId 5)
   |-- SoundChip_GSLightweight         (LW: interpreter + player, PeripheralId 10)
   '-- (NeoGS: reserved slot)

GSForwardMailbox   (chips/gsmailbox.h)  owned by both cards; single protocol truth
GSModPlayer        (chips/gsmodplayer.h) owned by the LW card; UI-free, chip-free
SoundManager       _gs: GeneralSoundCard* + createGeneralSoundCard() factory
```

The LLE card keeps its exact class name (tests, TTD ids, automation history)
and simply gains the base; every previously public method became `override`.
No LLE behavior changed in phases 1-2 (the TTD round-trip and protocol tests
pin the blob and the mailbox semantics).

### 2.2 The `GeneralSoundCard` contract

Method groups (see `generalsoundcard.h` for the authoritative list):

- Port keys: `PORT_DATA` (#B3), `PORT_COMMAND` (#BB), `PORT_CONTROL` (#33)
  moved up from the LLE concrete class; concrete aliases kept for existing
  call sites.
- Lifecycle: `reset()` (power-on), `resetCard()` (#33 bit7), `hostReset()`
  (ZX reset line, GSReset=1), `loadROM(path)` (LW: warn + ignore).
- Audio: `handleFrameStart()` / `handleFrameEnd(expectedSamples)` /
  `getBuffer()` / `setSampleRate(rate)` / `setSynthesisSuppressed(bool)` /
  `isSynthesisSuppressed()`.
- Mailbox automation (flush-first host-port semantics):
  `readStatus() / readData() / sendCommand() / sendData() / triggerNMI()`.
- Introspection: status/latch getters, queue counts, `getMPAG()` (LW: 0),
  `getChannelSample()/getChannelVolume()`, `isROMLoaded()` (LW: false),
  `getRamSizeKB()` (LW: virtual geometry).
- Coprocessor capability: `hasCoprocessor()`, `implementation()`
  (`GSCardImplementation::LLE/LW/NGS`); `isCPUHalted()`/`getCPUReg()` carry
  default bodies returning false/0 so non-LLE personalities stay header-light
  for automation. Verbose CPU views gate on `hasCoprocessor()` and print
  `(no coprocessor - lightweight)`.
- Diagnostics: `getActivityCounters()/resetActivityCounters()` + the
  `GSPortTraceRecorder` session surface (identical data model on every
  personality; LW stamps `pc = 0`).
- Runtime switch group (phase 4): `snapshotMailbox()` /
  `restoreMailbox(snapshot)` / `accumulateActivityCounters(other)` are pure
  virtual; `captureModuleUpload(bytes, playing)` (default: none) and
  `replayModuleUpload(bytes, startPlayback)` (default: no-op) carry default
  bodies; both shipped personalities implement both sides (LLE mirrors the
  COM30..D2 stream at the host-port layer, LW replays through its own
  interpreter), so the handoff is bidirectional.

### 2.3 SoundManager integration

- `_gs` is typed `GeneralSoundCard*`; `hasGeneralSound()` /
  `getGeneralSound()` expose the slot to automation (M8 pattern).
- `createGeneralSoundCard(GSTypeKind)`: `Z80` -> `SoundChip_GeneralSound`
  (loads `config.gs_rom_path` inside the factory), `LW` ->
  `SoundChip_GSLightweight` (virtual RAM = `config.sound.gsRamKB`), anything
  else (NONE/NGS/BASS-unmapped) logs and returns nullptr.
- `Init` consults the `gs_lightweight` feature before creating the card
  (feature on + configured Z80 -> LW; the config file is never touched).
- One `AudioSourceType::GeneralSound` mixer slot serves all personalities
  (device registry entry, buffers and mixer routing are unchanged).
- `switchGeneralSoundCard` / `requestGeneralSoundCardSwitch` (§7) are the
  only paths that delete `_gs`.

## 3. Shared protocol core: `GSForwardMailbox`

### 3.1 Responsibilities

Extracted verbatim from the LLE card (phase 2) so both personalities share
one source of protocol truth (`chips/gsmailbox.h/.cpp`):

- 16-deep command + data FIFO rings (power-of-two, masked head/count) with
  the direction-split status bit7 (host->GS data pending) / bit0 (command
  pending) semantics.
- Shadow latches `dataFromHost` / `commandFromHost` / `dataToHost` with the
  stale-latch readback rule: reading an empty queue returns the latch without
  popping (the firmware's `IN A,(DATRG)` analog; see verification doc §2.3).
- `pushHostCommand/readHostCommand/ackHostCommand` and the data-side trio:
  push enqueues + sets the latch + refreshes the pending flag; read pops +
  refreshes; ack converges the flag to the queue state without popping
  (every firmware handler ends with `OUT (RSCOM),A`).
- Overflow policy: pushing onto a full ring drops the newcomer, keeps the
  queued traffic and bumps `hostCommandsDropped` / `hostDataDropped` on the
  owner card through a `counters` back-pointer (`GSActivityCounters*`, bound
  by the owning card's constructor and re-bound by `restoreMailbox`).

### 3.2 TTD layout

The LLE card re-emits the mailbox into its fixed blob bytes 59..94 exactly
as before the extraction (command count/head/ring, data count/head/ring);
the LW card uses the identical offsets inside its own fixed header so a
mailbox snapshot is layout-compatible across personalities:

| Offset | Field |
|:--|:--|
| 59 / 60 | command queue count / head (ring 61..76, 16 bytes) |
| 77 / 78 | data queue count / head (ring 79..94, 16 bytes) |

The pre-extraction TTD round-trip tests stayed green through the refactor,
pinning byte-identity.

## 4. Lightweight card: command interpreter (`soundchip_gslw.cpp`)

### 4.1 Shell and state

The LW card owns: its `GSForwardMailbox`, a 16-deep card->host reply ring
(`postReply`/`pumpReply`, the OUTRG+HSEND analog), the firmware-variable
mirror (MODVOL/FXVOL/FXMVOL/MTVOL/MODFADE/FXFADE/CURMOD/CNTMOD/MODULE/
MTSTAT/ERRCODE/CURSMP/CNTSMP/CURFX/CNTFX, load/covox/sub-command flags),
the raw upload `store`, the `GSModPlayer`, the DAC latch pair arrays, the
blip pair + `AudioFrameDescriptor`, and the diagnostics instances
(`GSActivityCounters`, `GSPortTraceRecorder`).

Virtual geometry: `getRamSizeKB()` reports the configured size (clamped to
the original card range 128..512 KB); memory queries 20/21/23 derive from it
and the store size. `loadROM()` warns and ignores (no coprocessor).

### 4.2 Service model

There is no instruction timing; the interpreter runs at protocol events:

- Every host port write (`portDeviceOutMethod` on #B3/#BB) and every
  automation `sendData`/`sendCommand` runs `serviceMailbox()` synchronously
  after the push. Consequences: replies become readable immediately
  (`pumpReply` makes the queue head pending on post), the FIFOs effectively
  never overflow through normal traffic (each byte is consumed on arrival),
  and burst/param-first traffic dispatches in write order - the same ordering
  the LLE's FIFOs guarantee across the firmware's dispatch latency.
- `serviceQuantum()` (once per 320-cycle quantum from the lazy-sync core,
  §6.4) also polls the mailbox, covering traffic queued without further port
  activity, exactly like the firmware's COMINT poll between interrupts.
- `flush()`/`runTo()` are the LLE's lazy-sync timing math (12 MHz GS domain,
  host-speed-multiplier stretching, sub-quantum remainder carry) with the
  z80ex step replaced by the interpreter bookkeeping.

### 4.3 Reply engine

`postReply(value)` appends to the reply ring and pumps: the head byte lands
in `_mb.dataToHost` with the pending flag set, so a ZX `IN #B3` sees it at
once; each read pops and pumps the next byte - the firmware's HSEND wait
collapses to zero on this personality. Order is preserved; capacity 16 with
silent drop on overflow (protocol traffic never produces more than a few
outstanding bytes: multi-byte queries cap at 4).

### 4.4 Dispatch table (as built)

Verified row-by-row against `materials/gs/gs-firmware/firmware/src/{COM_L,
COM_H,INIT_H,LOAD_L}.a80`; replies are pinned by the fidelity test:

| Command | Action | Reply (via #B3) |
|:--|:--|:--|
| 00 | reset flags (consumes 1 dummy param) | none |
| 01/02/03 | DAC mid / VOL latches 0x3F / VOL latches 0 | none |
| 04-0D | direct channel select + data/volume latch family (physical-volume 09, WTDTL-tail 0A/0B, all-channels 0C, nibble-mask 0D) | none |
| 0E | covox stream: data latches DAC0+DAC2, exit on next command | none |
| 0F | stereo covox ('Y'): v1 documented no-op (consumes 1 param) | none |
| 13/16/17/18 | jump / putByte / getByte / memPtr: no code, no RAM window; 2/3 params consumed per firmware | 17: 0x00 |
| 20/21 | virtual total/free RAM (free = total - store); 21 clears ERRCODE | 3 bytes L,H,C |
| 22/23 | page byte peek / NUMPG (`pages - 1`) | 22: 0x00; 23: 1 byte |
| 2A/2B/34/35 | MODVOL / FXVOL / MODFADE / MTVOL get-then-set (2A/2B/35 clamp 0x40) | old value |
| 2C/2D/2E | CURMOD / CURSMP / CURFX selector (0 = count, > CNT -> 0) | old value |
| 2F | track selector: 2 params, no reply | none |
| 30 | fresh slot: CNTMOD=CURMOD=1, reply 1, consume 1 param, enter load; CNTMOD!=0 -> INITVAR reset-and-retry (no reply, consumes 1 byte) | 1 |
| D2 (in load) | LOAD3: finish stream, parse the store | none |
| 31 | start: P=0 -> CURMOD; P>CNTMOD or unparseable module -> error zeroes CURMOD; success sets MODULE/CURMOD=P, MTSTAT=3, MTVOL=0x40, tempo reset, player starts at position 0 | module # / 0x00 on error |
| 32/33 | stop (freeze position) / continue (resume); reply old MODULE | old MODULE |
| 36/37 | query / full module-system reset (stop, clear store, CURMOD/CNTMOD/MODULE=0, no INITVAR) | 36: 0xFF |
| 38/3E | FX upload: table-full (60) -> reply 0; stream consumed, nothing stored (v1 no-op); 3E param 00/01 -> same protocol, else reply 0 + 1 param | new FX # / 0x00 |
| 39/3A/3B/3C/3D | FX select+play / chan-off mask / FXFADE x2 / FXMVOL get-then-set | 39: 0x00 ok / 0xFF bad selector; 3B/3C/3D: old value |
| 40-49 | SFX channel fields: params consumed per firmware, nothing played | 42/45/46/47: 0x00 (fresh-RAM semantics) |
| 50/58 | SFX sub-command: 1 param, next command byte is the selector, then 1-3 data params (sel&7 >= 4 -> +1, == 7 -> +1) | 58: 0x58; 50: none |
| 80/A0 | FX channel setup sub-command: param + selector + 0-2 params (COM80 bits 3-4) / selector only | 80: 0xFF on bad selector |
| 60/61/62 | MTSNGPS / MTPATPS / combined (song<<6 & 0xC0 \| row & 0x3F) - player-driven | 1 byte each |
| 63/64 | 4x CHREAL (0x7F = no sample) / 4x CHMVOL (row volume 0-63) | 4 bytes |
| 66/67/68 | FXF external tempo (param) / MTSPEED / MTBPM | 66: none; 67/68: 1 byte |
| 69 | single engine step: no-op on this card | none |
| F0 | ERRCODE query | ERRCODE |
| F3/F4 | INITVAR / POST reboot: MODVOL/FXVOL/FXMVOL 0x40 but MTVOL 0x00 (COM31 raises it to 0x40, 5.4), MTSTAT=0xC3, module system cleared, VOL latches 0x3F, 1 tail byte consumed; F4 skips the POST window | none |
| other (COMZ) | ack only | none |

Programming-guide correction (verified against the firmware sources): the
guide's "#50 set global volume / #51 query" entries do not match the shipped
firmware - COM50 is the SFX sub-command protocol and there is no #51 handler
(falls to COMZ). The LW card follows the firmware: master volume lives on
COM35 (MTVOL), music scaling on COM2A (MODVOL).

### 4.5 Load machine and covox loop

`serviceMailbox()` while `_loading`: every pending data byte appends to the
store (FX-load variant discards instead); commands are acked and discarded
(`LOADCM`) except F3/F4 which execute and restart interpretation, and D2
which finishes the load. `finishLoad()` parses the store through the player;
a malformed upload raises ERRCODE 0x10 (ERR10 class) instead of playing.
The covox loop (COM0E) latches every pending data byte into DAC0+DAC2 and
exits on the next command.

### 4.6 Deliberate divergences from the firmware

| Area | Firmware | LW card | Why |
|:--|:--|:--|:--|
| POST window | 0.3-1.1 s RAM walk, BUG-6 idle-signature timing | COMF4 lands in the idle state immediately (`IN (#BB)` = 0x7E, queues empty) | no RAM to walk; prompt signature is the desired UX |
| Malformed uploads | plays anything | parse-reject, ERRCODE 0x10, COM31 error path | protecting the in-tree parser beats executing garbage |
| Reply latency | HSEND waits for the host read | next byte visible on pop | no coprocessor pacing; ordering preserved |
| SFX playback | full engine | params consumed, ack-shaped replies, nothing played | v1 scope (user decision) |
| Free-memory model | firmware allocator state | total - store size | no allocator; the approximation serves the probe contract |

## 5. MOD player core (`gsmodplayer.h/.cpp`)

UI-free, chip-free, directly unit-testable; owned by the LW card.

### 5.1 Parser

Standard Amiga/ProTracker module: 31-sample header at offset 20, song
length at 950, 128-entry pattern table at 952, `M.K.`/`M!K!`/`4CHN`/
`FLT4` tag at 1080, `patternCount = max(order) + 1` patterns of 64 rows x
4 channels, sample data in header order after the pattern block.
Validation: minimum size, tag, song length 1..128, order entries <= 127,
exact expected size (truncation rejected). Loop handling: `loopLengthWords
< 2` means one-shot (hold last byte, increment zeroed); loops that overrun
their sample are clamped to playable ranges. Sample bytes are SIGNED
8-bit (ProTracker); the player converts to the card's 0x80-centred DAC
domain with `^ 0x80` at fetch time - exactly what the firmware does on
upload (BUG-10, 2026-09-22: the raw signed byte was fed to the DAC and
every near-zero sample became a full-swing 0x00/0xFF - the "fuzz" heard on
all real content). Sample volume is the ProTracker 0..64 range (0x40 = full;
masking to 0x3F silenced 0x40 instruments - BUG-11).

### 5.2 Timing model (firmware QUANTUM/QTPLAY mirror)

- The 320-cycle quantum (37.5 kHz) is the sample clock: one
  sample-and-hold DAC latch update per quantum, exactly like the firmware's
  one `LD A,(DE)` fetch per interrupt acceptance.
- Tempo tick `TICKLEN = 37500 / (0.4 * BPM)` quanta (750 at the default
  125 BPM = 20 ms = 50 ticks/s), computed with the firmware's integer
  floor (`kQuantumRate * 5 / (2 * bpm)`); each `speed` ticks (default 6)
  advance one row. `Fxx` splits at param 32: speed vs tempo, both live.
- Position lives in the row/tick/quantum domain, never the audio sample
  domain: a runtime `setSampleRate` (blip rebuild) cannot disturb it.
- Song end (firmware QUANTUM.a80 `EFXSKP7`): restart at byte 951's position
  when it is inside the song (else 0), speed := 6, TICKLEN := 750 (125 BPM)
  - the `MTBPM` variable is NOT rewritten (COM68 keeps reporting the last
  Fxx tempo), only the tick length. Plain ProTracker keeps the tempo across
  the wrap; a module ending on a slow Fxx (cc_wizard.mod: F20) therefore
  looped 4x slow on the LW card until this was mirrored (BUG-16).
- DAC byte synthesis mirrors the firmware generator (SGEN1_L.a80 /
  GEN_L.a80 GENZERO): linear interpolation on the 16.16 fraction between
  adjacent source bytes (the firmware writes (prev+next)/2 at phase
  crossings), and a one-shot tail eased to 0x80 by successive halving
  instead of holding the last byte. Pure nearest-neighbour + hold-last
  carried ~30x the LLE's energy above 6 kHz on real content (live A/B
  2026-09-22, the "quantization grain" report); interpolation brings the
  offline render's >6 kHz share from 1.37% to 0.36% (openmpt123 8-tap:
  0.45%). SGEN2's box-averaging (steps above 1 byte/quantum) is not needed:
  the ProTracker period range tops out at ~0.84 byte/quantum.
- Per-voice playback: 16.16 fixed-point byte position in a 64-bit
  container (a 32-bit one tops out at 65535 bytes; ProTracker samples run
  to 131070 and real modules exceed 64 KB - BUG-12), increment =
  `3546895 * 65536 / (period * 37500)` (Amiga PAL rate resampled onto the
  card clock; the firmware's GSFRQTB+CHFADV equivalent), period clamped to
  the PT range 113..856.

### 5.3 Effect coverage

Implemented: 0xx arpeggio, 1xx/2xx portamento slides (with memory), 3xx
tone portamento (target latched from row or param-only rows), 4xx vibrato
+ 7xx tremolo (32-entry sine, rendered once **per tempo tick** — PT/firmware
CHFADV semantics; rendering per quantum turned 4xx/7xx into kHz-rate FM/ring
modulation, BUG-9; rendered only on rows whose effect column IS 4xx/6xx
or 7xx - the remembered param is memory for the next such row, not a
standing modulation, BUG-13), 5xx (continues the 3xx tone portamento AND
slides volume - BUG-14) / 6xx combined slides, 9xx offset (`param * 256`
bytes, param 0 reuses memory, plain notes start at 0 - BUG-15), Axx
volume slide (memory), Bxx/Cxx
position/pattern break (deferred to row end, PT semantics), Dxx pattern
break, E1x/E2x fine slides, EAx/EBx fine volume, ECx cut, EDx delayed
note, E9x retrigger, EEx row delay, Fxx speed/tempo. Parse-and-ignore
(firmware parity): E0x filter, EFx invert loop, E3x-E8x waveform/control
entries. Deferred navigation flags act at row end so mid-row effects keep
their PT semantics.

### 5.4 Volume model

Row volume is the ProTracker 0..64 domain (sample default, Cxx, slides,
tremolo; 0x40 = full). The card scales it as `(rowVolume * MODVOL * MTVOL) >> 12` before the 6-bit
latch write (the firmware VOL_H math). MODVOL (COM2A) scales music
channels, MTVOL (COM35) is the module master (COM31 resets it to 0x40);
FXVOL (COM2B) / FXMVOL (COM3D) belong to the SFX path (no-op in v1).

### 5.5 Runtime TTD blob

The player serializes only the quantum-domain runtime (playing/speed/bpm,
song/row/tick/quanta counters, tick length, deferred navigation, the four
`ChannelState` PODs, `GSMP` guard word). The parsed module image is rebuilt
from the card's restored upload store - never carried twice.

Audio across a restore follows the LLE convention: the load reseeds
`_lastL/_lastR` from the restored channel state but clears the blips, so
the integrator DC restarts at 0 and the fractional clock accumulator
restarts at its reset phase. The restored waveform therefore equals the
original up to one constant per channel (the dropped integrator seed)
plus a few LSB of kernel-phase wiggle - the round-trip test compares
DC-removed per-channel RMS. Two saves at a frame boundary stay
byte-identical; a restored card replays the same interpreter trajectory
(bit-exact `TTDHashState`).

## 6. Audio pipeline (as built)

### 6.1 Shape parity with the LLE

Two `blip_t` accumulators clocked in the 12 MHz GS cycle domain; frame
length `frameGsLength()` uses the LLE formula (host-speed multiplier only;
hardware turbo already descaled). Per quantum the player writes the four
`_channelData` latches (0x80-centered) and `_channelVol` latches, then
`emitSample()` computes `computeStereo()` and adds the delta pair at the
clamped quantum position. Sample-and-hold cadence matches the LLE 37.5 kHz
DAC fetch character.

### 6.2 Volume curve and stereo map

`_vfx[i] = gs_vol * i * 63 / 1024` (gs_vol clamped 8192) lands the channel
level directly in the int16 blip domain (Unreal `make_gs_volume`).
`computeStereo`: centered sample scaled by `_vfx[vol]`, channels 1,2 -> L,
3,4 -> R with 50% cross-feed - `L = (v0 + v1 + (v2 + v3) / 2) / 2`, mirrored
for R (Unreal gsz80.cpp:148-154,251).

### 6.3 Live rate change and suppression

`setSampleRate`: re-rates and clears both blips; the player position is in
the row/tick/quantum domain and survives untouched.
`setSynthesisSuppressed(true)`: latch/level tracking continues (`_lastL/
_lastR` stay current so the unsuppress resync is seamless), blip deltas are
skipped (turbo-mode contract, Beeper pattern).

### 6.4 Lazy-sync core and frame lifecycle

`flush()` derives the target from ZX tacts (null-core tests fall back to the
frame base), `runTo()` advances in quantum steps: NMI latch consumption,
quantum boundary -> `serviceQuantum()` (mailbox poll + player/DAC tick),
remainder carry. `handleFrameStart()` latches the frame base;
`handleFrameEnd(expectedSamples)` runs to the frame end, ends both blip
frames, reads `expectedSamples` stereo pairs (or the rate-derived count
when 0) into the `AudioFrameDescriptor` buffer, zero-fills the remainder and
posts `NC_AUDIO_ACTIVITY` HUD notifications on activity transitions.

### 6.5 Activity counters invariants

`cpuSteps` is defined as 0 (no coprocessor). `interruptPeriods ==
interruptsAccepted` always (the quantum handler is instant, never
coalesced). `dacFetches` counts one fetch per quantum while playing
(~749 per Pentagon frame: 239602 / 320). NMIs count on acceptance at the
next quantum boundary. All counters are monotonic and survive personality
switches through `accumulateActivityCounters` (§7.4), so triage totals
never regress across a handoff.

## 7. Runtime personality switching

### 7.1 Thread model

- `switchGeneralSoundCard(target)` is synchronous and must run on the
  emulation thread (the same ownership as the frame lifecycle) - tests and
  `Init` call it directly.
- `requestGeneralSoundCardSwitch(target)` is the thread-safe variant: it
  stores the target into `_pendingGSSwitch` (0xFF sentinel = none) and
  returns; `SoundManager::handleFrameStart()` applies the pending switch
  right after the pending-core-rate block - the established frame-boundary
  precedent, because the switch deletes and recreates the card. The WebAPI
  action and the feature toggle use this path.
- Corollary (observed live, verified by design): a pending switch cannot
  apply while the main loop is paused (no frame boundaries run) - it lands
  at the first frame after resume, indistinguishable from an instant apply
  on a running instance.

### 7.2 Switch algorithm

1. Validate: only `Z80`/`LW` are switchable; no card fitted -> false;
   already the requested personality -> true (no-op, card pointer kept).
2. Snapshot the outgoing card: `snapshotMailbox()` (queues, latches, flags;
   the counters back-pointer is stripped), `getActivityCounters()`, and the
   v1 module handoff payload via `captureModuleUpload(bytes, playing)`.
3. Unregister the three host ports from the port decoder (it holds the
   outgoing card's raw pointer and must never dispatch into a deleted
   object); a null decoder (bare-context tests) is tolerated.
4. Construct the target through `createGeneralSoundCard` (the LLE loads its
   firmware inside); on failure, roll the ports back onto the surviving
   card and keep the old personality.
5. `delete _gs; _gs = card;` then re-register the three ports for the new
   card.
6. Module handoff on virgin queues - **before** `restoreMailbox`, so the
   COM30/param/stream/D2 sequence runs on pristine FIFOs and the restored
   host traffic then processes on the loaded card - then restore the
   mailbox and fold the counters.

### 7.3 LW -> LLE: module replay

`replayModuleUpload` on the LLE mirrors the sequencing a real loader uses,
verified against gs105a end-to-end (COM2C -> 1, ~749 DAC fetches/frame):

1. **Boot wait** - the factory hands over a constructed-but-unbooted card,
   and the COM30 stream must not interleave with POST (the INITVAR tail
   consumes a DATRG byte and the boot posts a phantom NUMPG reply - either
   derails the load protocol). Advance frames until the volume latches
   report the past-INITVAR signature (4 writes), then until the boot reply
   is pending, and consume it. An already-booted card skips to the drain.
2. **COM30 open** - `sendData(1); sendCommand(0x30)` (param don't-care:
   CNTMOD/CURMOD -> 1), wait for the command to be popped (status bit0
   falls), then wait for and consume the slot reply before any module byte
   flows.
3. **Paced stream, one byte per firmware drain** - a #B3 write raises the
   shared bit7 and the loader's DATRG read clears it; the replay advances
   the card **two INT periods at a time** (640 cycles - the loader consumes
   a byte within a few hundred) until bit7 falls, bounded at 200 frames'
   worth of cycles (one HSEND timeout is ~73 frames). Advancing a whole
   239602-cycle frame per byte instead froze the emulation for ~6 minutes
   on a 381 KB module (the "demo stopped" report, 2026-09-22); the current
   pacing takes ~17 s for the same module. The latch model has no FIFO, so
   a byte is never written before the previous one is consumed.
4. **D2 terminator** - wait for the command to pop, then an 8-frame LOAD3
   settle margin (LOAD3 posts no completion reply).
5. **COM31 resume** (when the outgoing card was playing) - param first,
   wait for the reply, drain, settle frames.

The replay advances the fresh card's cycle base far ahead of the ZX clock;
that is audio-safe because `emitSample` gates all blip deltas on
`_frameGsCycles > 0`, which stays 0 until the card's first
`handleFrameStart` - zero deltas, zero backlog, timing bases re-derive at
the next frame boundary.

### 7.4 Counter and mailbox continuity

`accumulateGSActivityCounters` (gsporttrace.h) sums every monotonic
counter, keeps the max for `lastDacFetchGsCycle/Frame`, and preserves the
`interruptPeriods == interruptsAccepted + interruptsCoalesced` invariant
under summation; the LW override re-zeroes `cpuSteps` (no coprocessor - the
data model defines 0). `restoreMailbox` re-binds the drop-accounting
back-pointer to the new card. Net effect: queue contents, pending flags,
shadow latches and triage totals all survive the handoff.

### 7.5 v1 handoff limits

- Both directions (LLE<->LW) replay the module: each card mirrors the raw
  COM30..D2 payload stream at the host-port layer as it is written
  (`onHostDataWrite`/`onHostCommandWrite` on the LLE, the existing `_store`
  capture on the LW), independent of where the firmware parks the bytes in
  its own RAM. `captureModuleUpload` hands the last completed stream (plus
  whether playback was active) to `replayModuleUpload` on the freshly
  constructed target card, which replays it through that card's own
  interpreter (COM30 + payload + D2, then COM31 if it was playing).
- A switch mid-upload (COM30 opened, D2 not yet seen) keeps the mailbox but
  loses the partial stream - the target card starts with no module. This is
  the only remaining v1 limit.
- LW -> LLE mid-upload (`_loading`): the partial stream is lost; only the
  mailbox carries over.
- Reply-queue bytes beyond the visible `dataToHost` latch are not carried
  (the LLE keeps its replies inside firmware RAM).
- LW -> LLE resumes from row 0 (upload replay, not player-state transfer).

## 8. Feature and WebAPI surfaces

- Feature `gs_lightweight` (alias `gslw`, category performance, default
  **off**): `Init` fits the LW card when enabled (config untouched);
  `UpdateFeatureCache` reacts only to **feature-edge transitions**
  (`_gsLightweightFeatureWasOn` latch) - it requests LW on an off->on edge
  and the configured kind on an on->off edge (guarded to Z80/LW configs),
  all at the next frame boundary. A per-notification request would revert
  WebAPI `switch_personality` choices on the next FeatureManager sweep
  (observed live before the latch, 2026-09-21); the edge-triggered form
  keeps both surfaces independently authoritative.
- WebAPI `POST /api/v1/emulator/{id}/control/audio/gs` with
  `action=switch_personality`, `personality` = `z80` | `lle` | `lw` |
  `lightweight`: validates, calls `requestGeneralSoundCardSwitch`, responds
  with `personality` (normalized), `current`, `requested` and the note
  `applied at the next frame boundary`; 400 on missing/unknown personality.
  OpenAPI spec (`openapi_state.inc`) documents the action enum.
- WebAPI `action=dump_module` (2026-09-22, added for the real-content
  verification in §5): writes the last completed COM30..D2 upload
  (`captureModuleUpload`) to a file - `path` (optional, default
  `gs-module-dump.mod`), responds with `path`/`bytes`/`playing`; 404 when
  no upload has completed yet.

### 8.1 Automation surface parity (2026-09-22)

All five automation surfaces expose the same nine control actions
(`reset`/`reset_card`/`nmi`/`send_command`/`send_data`/`read_status`/
`read_data`/`switch_personality`/`dump_module`) plus full read access
(mailbox/channels/coprocessor state, activity counters, port trace).
`switch_personality` and `dump_module` were WebAPI-only through
2026-09-21; closed everywhere below the same day.

| Surface | Read (state + counters + trace) | Control (9 actions) |
|:--|:--|:--|
| WebAPI | `GET .../state/audio/gs`, `.../porttrace` | `POST .../control/audio/gs` (`action` field) |
| MCP `inspect_state` | `aspects: ["audio_gs"]` - **not** `domain` (no such param; passing it is silently ignored and falls back to the default aspect set `registers`/`disasm`/`screen_ocr`, no error) | - |
| MCP `emulator_manage` | - (use `inspect_state`) | `gs_reset` / `gs_reset_card` / `gs_nmi` / `gs_send_command` / `gs_send_data` / `gs_read_status` / `gs_read_data` / `gs_switch_personality` (needs `personality`) / `gs_dump_module` (optional `path`) - all forward to the same `/control/audio/gs` endpoint |
| MCP `analyze_gs_activity` | Wraps `porttrace` start/run/stop/read into one round-trip | - |
| CLI | `state audio gs [--verbose]`, `gsporttrace <start\|stop\|pause\|resume\|clear\|status\|counters\|events [n]>` | `gs <reset\|reset_card\|nmi\|send_command <byte>\|send_data <byte>\|read_status\|read_data\|switch_personality <z80\|lle\|lw\|lightweight>\|dump_module [path]>` (added 2026-09-22 - previously CLI had no GS control at all, only the other subsystems did) |
| Lua | `gs_state()`, `gs_counters()`, `gs_porttrace_*()` | `gs_reset()` / `gs_reset_card()` / `gs_nmi()` / `gs_send_command(byte)` / `gs_send_data(byte)` / `gs_read_status()` / `gs_read_data()` / `gs_switch_personality(personality)` / `gs_dump_module(path?)` |
| Python | `gs_state()`, `gs_counters()`, `gs_porttrace_*()` | same nine methods on `Emulator`, snake_case, `gs_dump_module(path="gs-module-dump.mod")` |

Lua/Python caveat: both interpreters' `_emulator` binds to whatever
instance `Automation`/`LuaEmulator::setEmulator` was last pointed at -
nothing in the WebAPI multi-instance lifecycle (`EmulatorManager`) wires it
to an instance created via `POST /api/v1/emulator/start`. Every `gs_*`
call (old and new alike) degrades gracefully when unset - bool actions
return `false`, byte reads return `-1`, table/dict actions return
nil/None - rather than crashing, but a Lua/Python script talking to a
WebAPI-created instance needs that binding wired first; this is a
pre-existing gap in the interpreter's emulator binding, not specific to
GS. Live-verified 2026-09-22: `gs_switch_personality`/`gs_dump_module`
return `false`/`nil` against a WebAPI instance exactly like every
untouched sibling (`gs_read_status` returns `-1`, `gs_state` returns nil)
under the same unset binding - both new functions match the existing
family's failure contract, not a regression.

## 9. Config and TTD registration

- `platform.h` `GSTypeKind`: NONE/Z80/BASS/LW/NGS (append-only; stored
  configs are string-parsed, never serialized raw).
- `config.cpp` `[SOUND] GSType`: `LW`/`LIGHT` -> LW; legacy `BASS` -> LW
  with a deprecation warning (no BASS library ever existed; upstream intent
  preserved).
- `ttdserializable.h`: `GeneralSoundLightweight = 10` (NeoGS = 9 stays
  reserved; ids are append-only).
- LW TTD blob: fixed 95-byte header - [0..3] mailbox latches, [4] LLE mpag
  slot (always 0), [5..8] channelVol, [9..12] channelData, [13..20]
  gsCyclesAbs, [21..22] intQuantum, [23] flags, [24..42] interpreter
  variables, [41..58] reply queue (count/head/ring 16), [59..94] the shared
  mailbox layout verbatim - then u32 store length + bytes, u32 player blob
  length + player runtime. `TTDHashState` covers the fixed header and the
  player runtime (the store is pinned by its length + the loading flag,
  like the LLE's RAM policy). A timeline recorded on one personality does
  not restore onto the other (id mismatch = clean error, by design).

## 10. Test inventory and gates

`core/tests/emulator/sound/chips/soundchip_gslw_test.cpp` (GSHarness-style;
chip-level tests are ROM-free; fidelity/cross-validation/switch-to-LLE
tests boot the real firmware with a runtime justification comment):

| # | Test | Asserts |
|:--|:--|:--|
| 1 | Mailbox parity | burst F4,30,D1 + param-first trio `00,31 40,2B 25,2A` dispatch in order, nothing dropped, bootdiag-7 expectations via player/latch state |
| 2 | Reply fidelity vs LLE | identical scripted traffic through both cards; every readable #B3 byte equal |
| 3 | Memory queries | 20/21/23 virtual geometry (512 KB: 23 -> 15; free shrinks by the store) |
| 4 | Load + parse | synthetic M.K. module uploads, D2 parses, ERRCODE 0, channel queries; garbage upload -> ERRCODE 0x10 |
| 5 | Start/stop/continue | COM31 starts (dacFetches ~749/frame), 32 freezes, 33 resumes |
| 6 | Volumes | 2A/35 halve the emitted RMS, 2B leaves music untouched (guide-vs-firmware COM50 note) |
| 7 | Tempo | Fxx/COM66 tempo change shifts the row-advance rate (~2x at 254 BPM) |
| 8 | Sample-rate change mid-play | output continues, player position survives |
| 9 | Suppression | turbo flag silences deltas, latches keep tracking, resync seamless |
| 10 | Cross-validation vs LLE | real firmware, ~2 s of playback, per-channel RMS within tolerance |
| 11 | Switching | LW->LLE replay + resume, LLE->LW replay + resume, mid-upload loses the partial stream, no-op/reject paths, request/frame-boundary apply (SoundManager-level) |
| 12 | BUG-6 shape | idle signature (status 0x7E) readable immediately after F4 |
| + | TTD round-trip | blob determinism + restored card keeps playing |

Gates: zero-warning build, GS filter green, full `test-parallel` (only the
proven pre-existing shard failures), live smoke (NEARTHGS with `GSType=LW`,
WebAPI mid-run switch). CMake must be reconfigured after adding test files
(the glob has no CONFIGURE_DEPENDS).

Live smoke results (2026-09-21, PENTAGON 512K + NEARTHGS.TRD): boot the
inserted disk, press `1` at the BOOT menu (the disk autostart lands on the
menu, not the game), music plays on LW with user-confirmed good quality
(after BUG-9); mid-music `lw->z80` replays the module into the LLE and
playback resumes (COM31 from the replay and/or the game's driver retry,
with the game's MODVOL fades landing on the firmware card); mid-music
`z80->lw` keeps the mailbox (game traffic consumed) and stops playback per
the §7.5 v1 limit. SCORPION (GS-128K) switch smoke: `z80 -> lw` clean.
GS-512K coverage note: pentagon512k/atm3/atm710 carry `GSRamSize=512` in
their configs but are not creatable on this branch (runtime `models`
endpoint reports why) - config audit is the verification surface there.

## 11. Known limitations / open items

- SFX playback on LW is v2 work (protocol consumption is already
  firmware-faithful, so traffic stays in sync).
- Effect parity with the firmware player is best-effort; uncommon effect
  mismatches surface as cross-validation RMS drift, never crashes.
- Finetune is approximated (1/8-semitone steps vs the firmware's 16-step
  table) - tolerance domain.
- NeoGS interface fit is asserted by design only (no P2 work here).
- `getCPUReg` on the interface drags `z80ex.h` into LW includes - accepted
  (keeps automation type-simple).
