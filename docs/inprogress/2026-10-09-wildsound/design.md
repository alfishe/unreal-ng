# Wild Sound ISA card (Sprinter): design

**Date:** 2026-10-09 · part of [README.md](README.md) · requirements in [requirements.md](requirements.md) ·
firmware facts in [reference-firmware.md](reference-firmware.md) (cited as "fw §X")

## 1. Shape

The card is a board-independent **core** (the firmware's behaviour: bus registers, streams, storage, XM
player, two AY chips) behind a thin **ISA adapter**, as the ISA design wants
([2026-10-02-sprinter-isa/tdd.md](../2026-10-02-sprinter-isa/tdd.md) §2: cards are thin wrappers, chips are
shared). The same core can later sit on the ZX AY socket (an AYX-32 running this firmware) without change.

| Layer | Files | Role |
|:--|:--|:--|
| Core | `core/src/emulator/sound/wildsound/wildsoundcore.{h,cpp}` | bus events (address / data / read) with the read pipeline, register file, unlock, identification, stream engine, storage, mode (AY / XM) |
| Storage | `wildsoundstorage.{h,cpp}` | the 999 424-byte store and its 64 descriptors; the parsed track and instrument tables |
| XM player | `wildsoundxmplayer.{h,cpp}` | FT2 replayer over the uploaded pack; renders stereo int16 at the core rate |
| AY pair | `wildsoundaypair.{h,cpp}` | two `SoundChip_AY8910` at 1 774 400 Hz, time-stamped write queues, the card's ABC balance |
| Audio | `wildsoundaudio.{h,cpp}` | the `ICardAudio` face (§7): frame hooks, mixer rows, buffers |
| ISA adapter | `core/src/emulator/io/sprinter/isa/cards/isawildsoundcard.{h,cpp}` | `IIsaCard`: decodes base + 0 / + 1 into bus events, timestamps them, RESET DRV, Describe, state |
| Tools | `tools/machines/sprinter/wildsound/` | converter, EXE linker, PT3 player (§9) |
| Test data | `testdata/sound/wildsound/` | programs, tracks, disk descriptor (§10) |

Namespaces: `wildsound` for the core, `sprinterisa` for the adapter.

## 2. Bus (fw §A)

### 2.1 Port mapping (R1)

The ISA glue of the Sprinter card is not documented. What is known: `prosiak.exe` writes the register number to
ISA I/O `#0000`, data to `#0001` and reads `#0001`, and the firmware sees three cycle kinds (address write,
data write, read). The adapter therefore decodes:

| ISA cycle | Core event |
|:--|:--|
| write `base + 0` | `WriteAddress(v)` |
| write `base + 1` | `WriteData(v)` |
| read `base + 1` | `Read()` |
| read `base + 0` | `Read()` (open: the glue may answer differently; nothing reads it) |

Decode: A9-A1 compared with the base, mirrored every `#400` like the other cards; AEN = 1 ignored (`IgnoresAen`
false). `RegisterName`: "AY register select", "AY data", "AY read". As built: the base is fixed at `#000` (what
prosiak.exe uses); `[ISA] SlotNBase` is not read for this kind, since the board's real decoding is unknown.

### 2.2 The read pipeline (R2)

```text
WriteAddress(v): if v >= #80: SpecialAddress(v)      (may switch chip, reset pointers, snapshot status)
                 chip[sel].addr = v
                 latch = Lookup(chip[sel].addr)
WriteData(v):    ws[addr] = v (echo store); per-register action (AY register, #F0 unlock, #F1 command, #F2 packet ...)
                 latch = Lookup(addr)
Read():          r = latch; ReadSideEffect(addr) (advance string / frame pointers); latch = Lookup(addr); return r
```

`Lookup`: `addr < #20` → the selected chip's `R[addr]` (raw, unmasked), else `ws[addr]`; the special registers
fill `ws[addr]` (or their own pointer state) before the lookup, exactly as fw §A.4 lists per register. The
`Peek` path (debugger) returns `latch` without the side effect.

### 2.3 Registers

Implemented as fw §A.4: `#00-#0F` AY, `#18-#1A` PWM duty, other `#10-#1F` stored, `#20-#DF` echo, `#E0-#E4`,
`#EB-#EF` identification (version words, strings and `#EF` = 220 000 000 as on the card; `#EB` uptime =
emulated milliseconds since the card's reset), `#F0` unlock, `#F1-#F3` streams, `#F4-#F7` stubs (RX empty
`#03`, RX count 0, TX free 0), `#FE` / `#FF` chip select. The `#E1` / `#E3` / `#E4` firmware-upload commands
store their bytes and do nothing else (R8).

## 3. Unlock and identification (R4, R6)

A small state machine (`LockPos`, `LockState` = locked / mode byte / zero byte / unlocked, `VerPos`) per
fw §A.5. The version string, copyright and build string are the shipped build's: "165931.23042021",
"Wild Sound II By Rob F.", "16:59:31 23.04.2021"; the version words 12104, 12316, 15931.

## 4. Streams and storage

### 4.1 Stream registers (R5)

- `#F1` data: bytes into `cmdBuf[pos++ & 31]`; at byte 2: clear both FIFOs, busy and error, `cmd = u16`;
  at byte 6: `size = u32`, `streamPos = 0`.
- `#F2` data: if busy, ignore; first byte = packet length N; the next N bytes append to the 127-byte OUT FIFO
  (excess dropped, as the card does); when the packet completes (or N = 0): run the command step (§4.4).
- `#F3` address write: snapshot the 32-byte frame, frame index 0; reads walk it with the running crcX
  (fw §A.8), byte 31 = CRC, then a new snapshot. `#F1` reads do the same through the fall-through to `#F3`.
- Frame: `[0..1] cmd | busy << 15`, `[2..5] size`, `[6..9] streamPos`, `[10] 127 - fifo.size`, `[11] 0`,
  `[12..28]` the shared work buffer as the card leaves it (the core keeps the same 32-byte buffer the `#E3`,
  `#EB`, `#EC`, `#EF` writers use), `[29..30] FA 3D`, `[31]` crcX.

### 4.2 Commands (R10)

| Command | Action in the core |
|:--|:--|
| `#0000` NOP | none |
| `#0001` CLEAR | stop the XM, mode = AY |
| `#0002` ERASE | stop, mode = AY, free the storage (descriptors, track, instruments) |
| `#0010` XMAST | first packet: allocate `size`; every packet: append the FIFO to the track pack; a 0-length packet closes it and parses the pack (§4.3) |
| `#0011` XMADO | none |
| `#0020` XMBST | as XMAST for the instrument table; closing parses the records and allocates each sample block |
| `#0021` XMBDO | sample bytes routed by the global sample pointer to the instrument whose range holds it (fw §C.4 routing); survives across XMBDO commands; reset by XMBST |
| `#0030` DIGPLAY | start the player on the parsed track and instruments; mode = XM. Refused (stays AY, `error` set in the report) when no valid track is stored |

### 4.3 Storage (R11)

A `std::vector<uint8_t>` of 999 424 bytes is the card's flash (the TTD region `wildsound.flash`, §8). The
track pack, the instrument table and each instrument's sample block are bump-allocated in it, 64 descriptors
at most (track, table, blank instrument 0, one per instrument: 61 instruments); an allocation past either
limit fails the command: it stops consuming data, `streamPos` stops below `size` (what a program can see,
fw §A.7) and the report says why. Samples are kept as uploaded (unsigned, `#00` already replaced by `#01`);
the player converts on read. Layout of a sample block: the 21-byte record, then the sample bytes; the
firmware's zero-marker padding is not needed by our mixer.

Parsing (`ParseTrack`, `ParseInstruments`) validates what the firmware trusts: pattern record 0's offset equals
the header size, every record's offset + size within the pack, rows 1..256, channels 1..32, order entries
below the pattern count; a bad pack is reported and DIGPLAY refuses it.

### 4.4 Timing (R12)

A command step runs synchronously at the end of its packet. Busy is cleared before the bus write returns, so
the frame shows busy = 0. ERASE is instant. This keeps the card a pure function of its bus input (TTD needs no
card clock) and satisfies both uploaders (they only wait for busy = 0). A later "real timing" option would set
busy for a modelled time and step on the host clock; the frame format already carries the bit.

## 5. XM player (R20, R21)

### 5.1 Input

The parsed pack: header (name, channels, speed, BPM, frequency flag, song length, restart, order), pattern
records, raw XM-packed rows; instruments 1..N each with one sample (length, loop start, loop end, loop type
from `SType & 3`, volume, fine-tune, panning, relative note). No envelopes, fadeout or auto-vibrato exist in
the upload; the player behaves as FT2 does for an instrument without them (key-off cuts the volume, fadeout
none).

### 5.2 Replayer

A FastTracker 2 replayer written for this card, following the FT2 specification (XM.TXT in the author's
repository, the FT2 effect documents next to it) and checked against libxmp / OpenMPT renders:

- timing: tick = 2.5 / BPM seconds, rendered in exact fractional samples at the core rate (no drift); speed =
  ticks per row; Fxx < `#20` speed, else BPM; F00 stops the song (FT2);
- frequencies: linear (`flag bit 0`) or Amiga tables, FT2 period formulas, fine-tune per sample and `E5x`;
- effects: 0 arpeggio, 1 / 2 / 3 portamentos with memory, 4 vibrato, 5, 6, 7 tremolo, 8 panning, 9 offset with
  memory, A slide, B jump (to order xx), C, D break (decimal row), E1-EE (fine porta, glissando, vibrato /
  tremolo wave, fine-tune, loop, retrigger, fine slides, note cut, note delay, pattern delay), F, G / H global
  volume, K key-off, L envelope position (no-op without envelopes), P panning slide, R multi-retrigger, T tremor,
  X1 / X2 extra-fine porta; volume column set / slide / fine / vibrato speed and depth / panning / pan slide /
  tone porta; note 97 key-off;
- end of song: wrap to the restart position (the card loops forever).

### 5.3 Mixer

Up to 32 channels; linear interpolation between sample points; forward and ping-pong loops (stored data is
not doubled: ping-pong is a direction flip); FT2 volume (sample volume × channel volume × global volume) with a
short ramp on volume changes and note starts (no clicks); panning law: FT2's square-root law; master scale
1 / sqrt(channels) with a fixed headroom so 32 channels do not clip; output stereo int16 at the core rate into
the `Wild Sound XM` row.

### 5.4 State

Fixed-size: 32 channel records, song position, tick counters, effect memories, the fractional sample clock.
Serialised field by field (§8).

## 6. AY pair (R22)

Two `SoundChip_AY8910` instances (the class TurboSound uses), clock 1 774 400 Hz, rendered at the core rate
through the chips' own decimators, the HQ flag as for the machine's AY. Register writes from the bus are
queued with their host time (`SsgWriteQueue`, as TurboSound does) and applied when the frame renders, so a
write lands on its T-state.

- Stereo: as built, the chip class's own ABC panning (the card's firmware balance is A L 1.00 / R 0.20, B 0.68 /
  0.68, C 0.20 / 1.00; matching it needs a per-channel balance input in the chip class: TODO). Chip 0 and chip 1
  are separate rows, summed in phase by the mixer (the firmware's inversion is a DAC detail, not "intended").
- As built, the generators tick at clock / 8 and are averaged per output sample (the TurboSound LQ path); the HQ
  FIR path is a TODO.
- PWM duty `#18-#1A`: the chip class has no duty input. Phase 1 stores the registers and renders 50 % duty
  (the reset value `#80` = 50 %, which is what every known program leaves); a duty input for the tone
  generator is a phase-3 item (TODO).
- In XM mode the AY rows output silence (writes still land in the registers, as on the card).

## 7. Audio integration (R23)

`SoundManager` mixes per-frame card output only for `ICard` slot cards (`attachSlotCard`, used by the
ZX-MultiSound). `ICard` is a ZX-bus `PortDevice`; its audio part does not depend on ports. Split it:

```cpp
// core/src/emulator/slots/cardaudio.h (new): the audio face of a card, independent of its bus
class ICardAudio {
public:
    virtual ~ICardAudio() = default;
    virtual void FrameStart() {}
    virtual void FrameEnd(size_t samples) {}
    virtual void SetOutputRate(uint32_t rate) {}
    virtual void MixerRows(std::vector<CardMixerRow>& out) const {}
    virtual const int16_t* MixerBuffer(AudioSourceType type) const { return nullptr; }
    virtual int16_t* SsgMixerBuffer(AudioSourceType type) { return nullptr; }
    virtual uint64_t RenderEpoch() const { return 0; }
    virtual bool WantsWideMix() const { return false; }
};
class ICard : public PortDevice, public ICardAudio { ... };   // unchanged for its users
```

`SoundManager::attachSlotCard / detachSlotCard / _slotCards` take `ICardAudio*`. The Wild Sound adapter
attaches its `WildSoundAudio` when it is fitted and detaches it in its destructor. New `AudioSourceType`
values `WildSoundXm`, `WildSoundAy1`, `WildSoundAy2` (appended), the AY rows with `ssgRow = true` so the AY
voicing and character chain apply.

Frame flow: `SoundManager::handleFrameEnd` calls `FrameEnd(samples)`: the AY pair renders its queue up to the
frame end, the player renders `samples` frames. Silent / turbo frames get `FrameEnd(0)`: the player does not
advance (the song pauses like every other sound source while sound is off).

Host time: the adapter stamps each bus event with `emulatorState.AudioTstate(z80->t)` (the Sprinter's turbo
removed, as `GSHostClock` does), so AY writes land at the right sample.

## 8. Configuration, state, TTD, automation (R30-R32)

- `CardKind::WildSound = 8`, name `WILDSOUND` in `kKinds`; `KindAvailable` true; `SlotSummaries`;
  `DefaultConfig` unchanged; the kind lists in `config.cpp`, `sprinterbios.cpp`, MCP tool text, CLI and the two
  OpenAPI includes. `[ISA] SlotNBase` default 0 for this kind.
- Fit: `PortDecoder_Sprinter` fits it next to `FitZxBusAdapters`, from the slot config.
- State: inside the SprinterIsa blob (id 33) through `IIsaCard::StateSize / SaveState / LoadState`: fixed size
  (registers, latches, unlock, stream state, FIFO, descriptors, parsed-table offsets, player state, AY pair
  state via the chips' TTD serialisation into a fixed buffer). The parsed tables are re-derived from the
  storage on load.
- Storage: a TTD region `TTDRegionId::WildSoundFlash = 19` through `ITTDRegionSource`, so a 1 MB upload costs
  only its written pages; `memory_access` lists it as `wildsound.flash`.
- Report: `Describe` adds a `wild_sound` object to the slot: mode, unlock state, chip select, command / busy /
  size / pos / FIFO, storage used / descriptors, track (name, channels, length, BPM, speed), player (order,
  pattern, row, tick, active channels), AY registers of both chips, last error.
- `audio_wildsound` aspect: `DeviceState::WildSound`, WebAPI `/state/audio/wildsound`, MCP aspect, CLI, Lua,
  Python, OpenAPI (the `audio_multisound` pattern).

## 9. Tools (R41-R43)

`tools/machines/sprinter/wildsound/`:

| Tool | What it does |
|:--|:--|
| `wsconv.py` | XM or S3M → the card's chunks, the MasonXM layout: `<name>.trk00..` (4-byte total size + track pack, 32 KB chunks), `<name>.ins00` (instrument table), `<name>.ins01..` (samples, 32 KB chunks). XM: fw §C.5 (proper 16-bit decode, `SType & 3`, empty patterns emitted explicitly). S3M: instruments → one-sample instruments, patterns re-encoded to XM packing, effects mapped to XM (A→F speed, T→F BPM, B, C (decimal), D/K/L slides, E/F portas incl. fine / extra-fine, G, H, I tremor, J arpeggio, O, Q, R, S8x / X panning, S3x / S4x waves, SBx loop, SCx / SDx / SEx), Amiga frequency flag; reports what it drops |
| `wslink.py` | Sprinter EXE from `prosiak.exe`'s loader, VM and player (block 0) + the chunks of one track: LZSA2-compresses each chunk (a Python LZSA2 compressor, checked by the decoder of `docs/disasm/.../tools/lzsa2.py`), writes the VM script (`05` per chunk with the call `4755` / `4796` / `47AB` the player needs, then `47C0`, `011C`), patches the banner string |
| `wsay/` | `wsay.asm`: a DSS program with S.V. Bulba's PT3 player (PTxPlay, adapted) that writes the card's AY registers through ISA port 0 / 1 (`#1FFD`, page `#D4` / `#D6`, `#9FBD` as prosiak does), one module per chip (TurboSound when two), INT-paced at 50 Hz, Esc to quit; built with the repository's `zxasm`. `wsaybuild.py` embeds the module(s) into an EXE per track |

## 10. Test data (R40)

`testdata/sound/wildsound/`:

```text
README.md                 origin and licence notes of every file
wildsound.ucompose.yaml   a FAT disk of this folder for ide0.slave (DSS drive D:), next to the system disk
prosiak.exe               the original
xm/   consul.exe faster2.exe discharz.exe ...   wslink output; sources in src/
s3m/  <two tracks>.exe                          wsconv + wslink
pt3/  <single>.exe <turbosound>.exe             wsay
src/  the source modules (XM from the author's repository, S3M / PT3 with their origin)
ref/  reference renders (short excerpts) and their checksums for the acceptance tests
```

XM: CONSUL (4 ch, effects 1/2/A/C/F), FASTER-2 (8 ch, ping-pong), DISCHARZ (10 ch, 3xx, key-offs, 50 KB track:
two track chunks); S3M: two small freely redistributable modules (OpenMPT `test.s3m` and one from the libxmp
test set or the Mod Archive with a stated licence); PT3: one single-chip and one TurboSound track from the local
collection.

Reference renders for XM / S3M come from libxmp (`xmp -o out.wav`, installed with Homebrew on the host; the
rendered WAVs go to `ref/`, the tool is not a build dependency).

## 11. Tests (R33)

| Test | Level | What |
|:--|:--|:--|
| `wildsoundcore_test.cpp` | core | read pipeline, chip select latches, unlock (good / bad / resync), version and identification strings, `#F1` command bytes, packet length and FIFO overflow, frame layout and crcX against the Z80 algorithm, `#F1` read fall-through |
| `wildsoundstreams_test.cpp` | core | the XMPLOAD upload sequence of a small generated module; storage and descriptor limits; bad packs refused; ERASE; RESET |
| `wildsoundxmplayer_test.cpp` | core | timing (rows per second at given speed / BPM), each effect on a one-channel generated module (pitch from zero crossings, volume from peak), loops, key-off, pattern loop / delay, B / D / Bxx+Dxx; render of a short module against a stored reference checksum |
| `wildsoundaypair_test.cpp` | core | register writes per chip, chip select, ABC balance, XM / AY exclusivity |
| `isawildsoundcard_test.cpp` | ISA | port decode, AEN, RESET DRV, Describe, state round trip, slot config |
| `sprinterwildsound_test.cpp` | machine | a Z80 upload loop run on the SPRINTER model through ISA window 3 (the prosiak access path), then DIGPLAY and non-silent output; TTD record / replay bit-exact; env-gated (`UNREAL_SPRINTER_HDD_CHD`): `prosiak.exe` from the composed test disk to "XM PLAY" |

Each test stays under 50 ms except the machine tests (boot-bound, `EnableTurboMode`, justified in a comment).

## 12. Phases

| Phase | Content | Done when |
|:--|:--|:--|
| W1 | core bus, unlock, identification, streams, storage; ISA adapter, config, Describe, state | core / ISA tests pass; prosiak.exe prints the version and uploads to POS = SIZE (TTD check) |
| W2 | `ICardAudio` split, AY pair, XM player, mixer rows | prosiak.exe is heard; player tests pass |
| W3 | tools: wsconv, wslink, wsay; testdata folder; compose descriptor | the acceptance list of requirements §3 |
| W4 | TTD region, `audio_wildsound` aspect on every surface, docs, recipe, survey status | TTD and automation tests; docs |
| later | PWM duty in the AY tone generator; real busy timing; the AYX-32 (AY socket) host for the same core | |

## 13. Open points

| # | Point | Decision now |
|:--|:--|:--|
| O1 | Port 0 read, base addresses other than `#000`, the card's IRQ | read = AY read; base fixed at `#000`; no IRQ |
| O2 | Firmware on shipped Sprinter cards (later than 2021?) | the 2021 protocol; later features when a source appears |
| O3 | DAC left / right wiring on the ISA board | AY: A left, C right (ABC); XM: instrument / effect panning, left = left |
| O4 | Busy timing | synchronous (§4.4) |
| O5 | Interpolation | linear (FT2 itself has none in the original DOS replayer, the intended sound is the module author's tracker playback, which is interpolated in FT2 2.08+) |
