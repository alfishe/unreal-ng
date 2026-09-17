# MFM / MWM - MoonBlaster Music File Format

## Overview

MFM (MoonBlaster FM Music) and MWM (MoonBlaster Wave Music) are music file formats used by the **MoonBlaster** tracker software for MSX computers equipped with OPL4-based sound cards such as MoonSound, Wozblaster, and FM-Blaster.

### History and Attribution

| Component | Creator(s) | Year |
|-----------|-----------|------|
| MoonBlaster (original) | Remco Schrijvers | 1992 |
| MoonBlaster Wave/FM | Remco Schrijvers, Marcel Delorme | 1995+ |
| MoonSound cartridge | Henrik Gilvad | 1995 |
| MoonSound distribution | Sunrise (Netherlands) | 1995 |

The MFM/MWM formats evolved from the original MBM format (MoonBlaster 1.x for MSX-MUSIC/MSX-AUDIO) to support the enhanced capabilities of the OPL4 chip.

## Hardware Target

### OPL4 (Yamaha YMF278B)

The formats target the Yamaha OPL4 sound chip, which provides:

- **18 FM synthesis channels** (6 four-operator channels or 18 two-operator channels)
- **24 PCM/Wave channels** with 12/16-bit sample playback
- **512 KB sample ROM** (YRW-801) with General MIDI instruments
- **Compatibility** with OPL, OPL2, OPLL, and OPL3 chips

### I/O Port Layout

| Port | Description |
|------|-------------|
| 0xC4 | FM Register Select (Bank 1) |
| 0xC5 | FM Data |
| 0xC6 | FM Register Select (Bank 2) |
| 0xC7 | FM Data (Bank 2) |
| 0x7E | Wave Register Select |
| 0x7F | Wave Data |

## File Identification

| Signature | Format | Description |
|-----------|--------|-------------|
| `MBMS` | MWM/MFM | MoonBlaster MoonSound music |

All MoonBlaster MoonSound files begin with the ASCII signature `MBMS` (0x4D 0x42 0x4D 0x53).

## File Structure

All fixed offsets below are player-verified (see "Verification method" at the
end of this document): the reference loader (`moonsound.bin` menu routine at
runtime 0x6471) consumes the 6-byte header as a dummy block, then relocates
`file[6:]` contiguously to its work image at 0xC000, so **track-block offset X
= file offset X+6** everywhere.

```
+----------+------------------------------------------+
| Offset   | Content                                  |
+----------+------------------------------------------+
| 0x0000   | Header (6 bytes)                         |
| 0x0006   | Track-info block (718 bytes)             |
| 0x02D4   | Trailer (94 bytes: metadata + config)    |
| 0x0332   | Position table (song_length+1 bytes)     |
| varies   | Pattern pointer table (2 x pattern count)|
| varies   | Pattern data (packed rows)               |
+----------+------------------------------------------+
```

### Header (6 bytes)

| Offset | Size | Description |
|--------|------|-------------|
| 0x00 | 4 | Magic: `MBMS` (0x4D424D53) |
| 0x04 | 1 | Format version high byte (0x10) |
| 0x05 | 1 | Format version low byte (0x01) |
| 0x06 | 1 | Last position index `xleng` (position count = value+1) |
| 0x07 | 1 | Loop position `xloop` (0xFF = no loop, play once) |

Note that bytes 0x06/0x07 are the first two bytes of the track-info block:
the block simply starts inside the "header" most tools assume.

### Track-Info Block (718 bytes at file offset 0x0006)

Copied verbatim by the player (`ldir` of 718 bytes in `MBPlayer_start_music`)
into its workspace. Relative offsets below; add 6 for file offsets.

| Relative | File | Size | Description |
|----------|------|------|-------------|
| 0x000 | 0x006 | 1 | Song length (last position index, `xleng`) |
| 0x001 | 0x007 | 1 | Loop position (`xloop`) |
| 0x002 | 0x008 | 552 | 24 FM instruments, 23 bytes each |
| 0x22A | 0x230 | 1 | Tempo (`xtempo`; copied to `play_speed`, timer = tempo-2) |
| 0x22B | 0x231 | 1 | Base frequency / Hz equalizer (`xhzequal`) |
| 0x22C | 0x232 | 25 | Per-channel configuration |
| 0x245 | 0x24B | 1 | Number of 4-op FM chains, 0..6 (`chvol_1`), see below |
| 0x246 | 0x24C | 7 | Channel configuration |
| 0x24D | 0x253 | 129 | Per-channel detune/effect configuration (varies per song; sub-ranges at file offsets 0x25C-0x25F, 0x261-0x264, 0x27C-0x28B, 0x28F-0x2A0, 0x2B4-0x2C0 change across the sample collection, the rest is zero; ends at block offset 0x2CD) |

The reference player's wave-number/volume arrays (`xwavnrs`/`xwavvols`) live
**outside** this block in player RAM; they are not file fields of `.MFM`
songs.

#### `chvol_1` — 4-op chain count

Despite its historical "FM channel count" mislabel, `MBPlayer_init_opl4`
uses this byte as the **number of 4-op chains** (0..6): it indexes the table
`{0x00, 0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F}` and writes the result to OPL4
bank-2 register **0x104** (4-op connection mask), then computes the 2-op
voice count as `0x12 - 2*chvol_1`. All 18 channels remain FM in `.MFM` files;
there are no wave channels. Observed values across the sample collection are
always even (0, 2, 4, 6).

### FM Instrument Definition (24 x 23 bytes at file offset 0x008)

Each instrument consists of **two 11-byte operator-pair patches plus one
extra byte**. The primary patch carries operators 1+2 (the whole voice for
2-op instruments); the secondary patch carries operators 3+4 for 4-op
instruments. Fields are stored **interleaved per register type** (all
modulator/carrier pairs adjacent), as raw OPL4 register values:

| Patch offset | OPL4 Register | Description |
|--------------|---------------|-------------|
| 0x00 / 0x01 | 0x20+op | AM/Vibrato/EG/KSR/Multiple, modulator / carrier |
| 0x02 / 0x03 | 0x40+op | KSL<<6 \| Total Level, modulator / carrier |
| 0x04 / 0x05 | 0x60+op | Attack Rate<<4 \| Decay Rate, modulator / carrier |
| 0x06 / 0x07 | 0x80+op | Sustain Level<<4 \| Release Rate, modulator / carrier |
| 0x08 / 0x09 | 0xE0+op | Waveform Select, modulator / carrier |
| 0x0A | 0xC0+ch | Feedback / connection |
| record +22 | — | Extra byte after both patches (purpose not identified) |

Cross-check: CRYOGENT.MFM instrument 0 primary = `71 31 0A 05 AF C7 14 24 00
00 0D`, which is exactly the lead patch captured at runtime on channels
15-17 (TL 10/5, AR/DR 10-15 / 12-7, SL/RR 1-4 / 2-4, WS 0/0). The runtime
AM/VIB bits may differ from the stored bytes because the player ORs pattern
effect flags into register 0x20 writes.

### Trailer (94 bytes at file offset 0x02D4)

| Relative | Size | Description |
|----------|------|-------------|
| +0x00 | 50 | Metadata string, space padded |
| +0x32 | 36 | Configuration (incl. drum/kit settings; content varies per song) |
| +0x56 | 8 | Sample kit name, space padded (`NONE` = ROM only) |

#### Info String Format
```
<Title> / <Game or Album> / <Author> <Year>
```

Examples (exactly 50 bytes each, from the sample collection):
- `Cryogenity LIVE! / R. v/d Moosdijk / Zodiac 1995  `
- `Alone Batlle / Ys-II / Bart Roymans Zodiac 1995   `
- `Dertig april 1995, Eerste met MoonSound A.Minnaard` (no padding — full)

#### Sample Kit Field
- `NONE    ` - Uses built-in YRW-801 ROM samples only
- `<name>  ` - Custom sample kit name (corresponds to .MWK file)

### Position Table (file offset 0x0332)

Follows the trailer. Contains `song_length+1` one-byte pattern indices
(positions 0..`xleng`). The pattern count is **not stored** in the file: the
loader computes it as `max(position table)+1` (helper routine at runtime
0x64EC in `moonsound.bin` returns max byte + 1).

### Pattern Pointer Table

Follows the position table. One little-endian 16-bit entry per pattern
(`pattern_count = max(positions)+1` entries):

- bits 13:0 — offset of the pattern **relative to the track-block base**
  (the player resolves `songdata + (ptr & 0x3FFF)`; file offset = value + 6)
- bits 15:14 — RAM bank index into `songdata_bank1[]` for songs spanning
  multiple 16 KB mapper banks (0 for all files under 16 KB)

In the sample collection patterns are stored in index order immediately
after the pointer table (pattern 0's pointer equals the table end).

### Pattern Data

Each pattern holds **16 rows** (the player's step counter wraps with
`and 0x0F`). Rows are bit-packed per tick into a 25-entry step buffer
(channel 0 plus 24 mask-covered channels):

```
Row encoding:
  0xFF                      -> empty row (no events, single byte)
  otherwise:
    byte 0                   -> channel 0 event
    bytes 1-3                -> bit masks for channels 1-8, 9-16, 17-24
                                (MSB = lowest channel number of each group)
    following bytes          -> one event byte per set mask bit, in channel
                                order (only present for set bits)
```

Pattern sizes are variable (20-208 bytes observed) and are delimited by the
next pointer in memory order, not by terminators.

Each event byte can contain:

| Value Range | Meaning |
|-------------|---------|
| 0x00 | No action |
| 0x01-0x60 | Note (C-0 to B-7) |
| 0x61 | Note off |
| 0x62-0x79 | Instrument change |
| 0x7A-0xB9 | Volume change |
| 0xBA-0xBC | Stereo panning |
| 0xC0-0xCF | Pitch bend |
| 0xD0-0xE2 | Vibrato/modulation |
| 0xE3-0xEF | Detune |
| 0xF0-0xF6 | Special effects |
| 0xF7-0xF9 | Portamento |
| 0xFA-0xFF | Speed/tempo commands |

### Channel Layout

All 18 OPL4 FM channels are used by `.MFM` songs (the format is FM-only;
wave channels exist only in `.MWM`). The 4-op chains selected by `chvol_1`
occupy the OPL4 standard master channels (0, 1, 2, 9, 10, 11 with slaves
+3); the remaining channels play 2-op voices. For CRYOGENT.MFM
(`chvol_1` = 6) the runtime capture shows:

- masters 0/1/2 — drum bed (three of the six chains), keyed via the
  master's 0xB0 with fnum 517/513/517
- masters 9/10/11 — the other three chains (accompaniment)
- channels 15/16/17 — 2-op lead voices

FM drum voices are not stored among the 24 song instruments: their patches
come from the player's internal patch table (`patch_table.inc`) combined
with the trailer's drum configuration bytes.

### Metadata

See the trailer layout above: the metadata string is a single 50-byte
field at file offset 0x2D4 (title/author concatenated with " / " or other
free-form separators chosen by the composer).

## Related Formats

| Extension | Format | Description |
|-----------|--------|-------------|
| .MBM | MoonBlaster 1.4 | Original format for MSX-MUSIC/MSX-AUDIO |
| .MFM | MoonBlaster FM | OPL4 FM-only music |
| .MWM | MoonBlaster Wave | OPL4 full music (FM + PCM) |
| .MWK | MoonBlaster Wave Kit | Sample kit for MWM files |
| .PAK | Packed Music | Compressed MFM/MWM for playback |

## Player Implementation

The reference player (`mfm_player.asm`) provides these entry points:

| Label | Address | Description |
|-------|---------|-------------|
| `MBPlayer_init` | +0 | Initialize and start music |
| `MBPlayer_play` | +3 | Call every frame for playback |
| `MBPlayer_stop` | +6 | Stop playback |

### Playback Timing

- Default tick rate: 50 Hz (PAL) or 60 Hz (NTSC)
- Speed range: 1-24 (ticks per step)
- Tempo is stored in the file and applied at initialization

### Key Data Structures

```c
// Play table entry for FM channels (13 bytes)
struct FMChannel {
    uint8_t port;           // I/O port base
    uint8_t note;           // Current note (0-95)
    uint8_t voice;          // Voice/channel number
    uint8_t reg_offset;     // Register offset
    uint8_t effect_type;    // Current effect
    int8_t  effect_param;   // Effect parameter
    int8_t  effect_delta;   // Effect delta
    uint8_t detune;         // Detune value
    uint8_t freq_low;       // Frequency low byte
    uint8_t freq_high;      // Frequency high byte
    uint8_t key_on;         // Key-on flag
    uint8_t volume;         // Channel volume
    uint8_t flags;          // Status flags
};

// Play table entry for Wave channels (18 bytes)
struct WaveChannel {
    uint8_t note;           // Current note
    uint8_t reg_base;       // Base register (0x20-0x25)
    uint8_t effect_type;    // Effect type
    int8_t  pitch_delta;    // Pitch bend delta
    int8_t  pitch_speed;    // Pitch bend speed
    int8_t  detune;         // Detune value
    uint8_t reverse;        // Sample reverse flag
    uint8_t wave_num;       // Wave/sample number
    uint16_t freq;          // Frequency value
    uint8_t instrument;     // Current instrument
    uint8_t stereo;         // Stereo pan (0-15)
    uint8_t volume;         // Volume (0-127)
    uint16_t wave_ptr_low;  // Wave data pointer low
    uint16_t wave_ptr_high; // Wave data pointer high
};
```

## Compatible Software

### Trackers
- **MoonBlaster FM** - FM-only editor for OPL4
- **MoonBlaster Wave** - Full OPL4 editor with sample support (by Remco Schrijvers, Marcel Delorme)

### Players (Real Hardware)
- **[RoboPlay](https://www.msx.org/forum/msx-talk/graphics-and-music/roboplay-multi-format-music-player-in-fusion-c)** - Multi-format MSX music player supporting MWM (Wave with USER/EDIT files), MFM (FM with USER/EDIT/RAW files), MBM (MoonBlaster 1.4), and many other formats
- **[VGMPlay MSX](https://www.grauw.nl/projects/vgmplay-msx/)** - VGM file player for MSX with OPL4/MoonSound support (by Grauw)
- **MoonDriver** - MML compiler and player for MoonSound

### Emulators with OPL4/MoonSound Support

| Emulator | Platform | OPL4 Support | Notes |
|----------|----------|--------------|-------|
| **[openMSX](https://github.com/openMSX/openMSX)** | Cross-platform | Full | Reference implementation; OPL4 fixes by ValleyBell |
| **[blueMSX](https://emulation.gametechwiki.com/index.php/BlueMSX)** | Windows | Full | MoonSound emulation included |
| **[blueMSX+](https://github.com/Sebbeug/blueMSX-plus)** | Windows 11 | Improved | Unofficial fork with enhanced OPL4 audio quality |
| **MAME** | Cross-platform | Partial | YMF278B core present, some features missing |

### PC Players/Plugins
- **[MSXPlug](https://www.msx.org/news/music/en/msxplug-version-026-mbm-support)** - Winamp plugin; v0.26+ supports MBM format with stereo mode
- **[VGMPlay](http://phd-sid.ethz.ch/debian/vgmplay/)** - Desktop VGM player with YMF278B core (ported from openMSX)
- **Moonblaster 1.2 Replayer for Windows** - Native Windows player for MoonBlaster files

## Compatible Hardware

| Device | Manufacturer | Notes |
|--------|--------------|-------|
| MoonSound | Sunrise | Original 1995 cartridge |
| Wozblaster | Gustavo Iriarte | DIY clone |
| Wozblaster Reloaded | The Retro Hacker | Modern reproduction |
| MSX-Blaster OPL4 | 8bits4ever | Commercial clone |
| FM-Blaster | Various | MoonSound-compatible |

## References

### Primary Sources
- **Sample files analyzed**: MFM Music Sample 2 collection:
  - `ALONEBTL.MFM` - "Alone Battle" from Ys-II by Bart Roymans (Zodiac), 1995
  - `DJINGLE1.MFM` - "End of Search / Rhumba Version" by R. v/d Moosdijk
  - `CRYOGENT.MFM`, `DERTIGAP.MFM`, `FEEDBACK.MFM`, `FOUNTAIN.MFM`, `JDK2.MFM`, `MEMORY.MFM`, `PALACEOD.MFM`, `PATSTORY.MFM`, `SALMON.MFM` - Various composers. In the collection's player ordering `CRYOGENT.MFM` is **melody 7**; it is the acceptance fixture for the OPL4 FM rearchitecture (`core/tests/emulator/sound/moonsound_mfm2_guest_test.cpp`, testdata `sound/moonsound/mfm-sample-2/`) and the file that exposed the 4-op master-ownership defect — see "Emulation notes" below.
- **Player source code**: `mfm_player.asm` by Mick (Михаил), 2015 - Z80 assembly replayer implementation for ZX Spectrum adaptation
- **Demo code**: `moonsound_demo.asm` - Integration example with player

### Emulation notes — CRYOGENT (melody 7)

Captured while fixing "sample 7 plays only some channels, others are noising" (2026-09-17);
useful to any emulator author validating an OPL4 FM core against this collection:

- **Drum beds sit on three of the six configured 4-op chains (masters 0/1/2)**
  (slaves 3/4/5, algorithm with C output routed back — OPL3 alg 11-style), programmed
  and keyed through the **master's** 0xA0-0xB0 only. The file sets `chvol_1` = 6, so
  register 0x104 receives 0x3F: **all six** 4-op chains are connected (the remaining
  chains ride masters 9/10/11). A per-slave decode leaves operators C/D unkeyed and
  the chain degenerates to an open-loop modulator squeal — measured as broadband noise
  (hf ratio ≈ 1.0, zero-cross rate ≈ 0.31). YMF262 semantics: the master's 0xB0 keys
  and tunes all four operators.
- **Leads (channels 15-17 region) are genuinely bright 2-op patches**: FB7, AM+VIB on both
  operators, WS0, TL 10/5, AR 10/12, DR 15/7, SL 1/2, RR 4/4, additive output. Their
  high-frequency energy (hf ≈ 1.03) is timbre, not a defect — verified identical in two
  independent engines (see `CompareLeadVoice` in `tools/poc/015-opl4-synthesis/tests/opl4fmcompare.cpp`).
  The file side confirms it: this is song instrument 0's primary patch verbatim
  (`tools/verification/moonsound/mfm_parser.py` decodes TL 10/5, AR/DR 10-15/12-7,
  SL/RR 1-4/2-4, WS 0/0 at file offset 0x0008). The drum patches, by contrast, are not
  among the 24 song instruments — they are composed at runtime from the player's
  internal patch table plus the trailer's drum configuration.
- **The player mixes FM data-port pairs freely** (`#C5`/`#C7`): a data write goes to the
  bank of the most recent address-port write, not to a per-port bank. Measured traffic:
  C4→C5 1341, C6→C7 1166, C6→C5 112, C4→C7 0. A per-data-port decode silently drops the
  0x105 NEW/NEW2 latching and aliases all bank-2 register writes onto bank 1.

### MSX Community Documentation
- [Moonblaster file format - MSX Wiki](https://www.msx.org/wiki/Moonblaster_file_format) - Official format specification
- [MBM file format - MSX Resource Center](https://www.msx.org/forum/development/msx-development/mbm-file-format) - Community discussion on format details
- [Moonblaster file format doubts - MSX Resource Center](https://www.msx.org/forum/msx-talk/software/moonblaster-file-format-doubts) - Format clarifications
- [MoonBlaster FM infos - MSX Resource Center](https://www.msx.org/forum/msx-talk/graphics-and-music/moonblaster-fm-infos) - FM format specifics
- [Music replayer routines - MSX Wiki](https://www.msx.org/wiki/Music_replayer_routines) - Available replayer implementations

### Hardware References
- [MoonSound - Wikipedia](https://en.wikipedia.org/wiki/Moonsound) - General overview and history
- [The Ultimate MSX FAQ - MoonSound section](http://faq.msxnet.org/opl4.html) - Technical FAQ and usage
- [MSX OPL4 Category - MSX Wiki](https://www.msx.org/wiki/Category:OPL4) - All OPL4-related articles
- [Programming OPL4 (FM Part) - Fusion-C](https://moltsxalats.wixsite.com/fusionc/post/programming-opl4-fm-part) - OPL4 programming tutorial
- [Moonsound OPL4 programming - MSX Resource Center](https://www.msx.org/forum/msx-talk/development/moonsound-opl4-programming) - Development discussion

### Software References
- [Moonblaster Wave - Team Bomba](https://www.teambomba.net/mbwave.html) - Original tracker software
- [RoboPlay - MSX Resource Center](https://www.msx.org/forum/msx-talk/graphics-and-music/roboplay-multi-format-music-player-in-fusion-c) - Multi-format player
- [VGMPlay MSX - Grauw](https://www.grauw.nl/projects/vgmplay-msx/) - VGM player for MSX
- [openMSX OPL4 fixes - GitHub](https://github.com/openMSX/openMSX/issues/1114) - Emulation improvements

### Hardware Clones
- [Wozblaster - MSX Resource Center](https://www.msx.org/news/en/wozblaster-moonsound-compatible-cartridge) - DIY clone announcement
- [MSX OPL4 Wozblaster Reloaded - The Retro Hacker](https://www.theretrohacker.com/product/msx-opl4-wozblaster-reloaded-cartridge-ymf278/) - Modern reproduction
- [8bits4ever MSX-Blaster OPL4 - MSX Wiki](https://www.msx.org/wiki/8bits4ever_MSX-Blaster_OPL4) - Commercial clone

## Document History

- 2026-09-17: Initial documentation based on analysis of sample files and mfm_player.asm, supplemented with web research on MSX community resources
- 2026-09-17: Layout corrected against the reference player binary (`moonsound.bin`)
  and a runtime OPL4 register capture. Key fixes: the track-info block starts at file
  offset **0x0006** (not 0x0008); FM instruments are **24 x 23 bytes** (two interleaved
  11-byte operator-pair patches, not 14-byte records); tempo lives at block +0x22A;
  `chvol_1` at block +0x245 is the **4-op chain count** (all 18 channels are FM);
  the trailer (50-byte metadata string + 36 config + 8-byte kit name) sits at 0x02D4;
  the pattern count is derived as `max(position table)+1`; pattern pointers are
  track-block-relative with bank bits 15:14; patterns are 16 bit-packed rows.

### Verification method

The layout above was pinned by disassembling the reference loader/player
(`moonsound.bin` from the "MFM Music sample 2" distribution: menu load routine at
runtime 0x6471, block-copy helper 0x64DB, max+1 helper 0x64EC,
`MBPlayer_start_music`, `MBPlayer_init_opl4`, and the row decoder at runtime
0x44D6), cross-checked over 14 sample songs (tempo always 3-8, chain count always
0/2/4/6 at the derived offsets) and against a live OPL4 capture of CRYOGENT.MFM
(`conn104 = 0x3F` matching `chvol_1` = 6; lead patch matching file instrument 0).
`tools/verification/moonsound/mfm_parser.py` implements this layout.
