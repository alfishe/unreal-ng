# MASON — Robus's AY music editor, `.may` modules and compiled tunes

## Overview

**MASON** (later "Mason-AY", "Mason AY II") is an AY / YM music editor by
Robus (Rob F., real name Aleksey Fil, Entire Group, Kyiv), the author of the
Wild Sound card. Two different data formats come out of it:

| Form | What it is | Status of this document |
|---|---|---|
| **`.may` module** | the editor's own song file, signature `MASON-AY-2xx` | header partly identified; body **not decoded** (no player or spec published) |
| **compiled module** | output of the editor's COMPILE: a relocatable Z80 data block played by a small Z80 player; this is what `.ay` rips and `.COD` releases contain | **fully decoded** from the player disassembly and checked on 4 real tunes |

The compiled form is neither a register dump (PSG/VTX) nor a pattern
tracker module (PT3). It is a **per-frame event stream**: every 50 Hz frame,
each of the three channels takes one index into a table of 4-byte
"frame descriptors" (tone, volume or envelope, optional instrument start).
Repeated stretches are factored out into subroutines that the stream calls.
Instruments ("samples" in this document) are short tables of per-frame
tone, volume and noise deltas.

- **Chips**: one AY-3-8912 / YM2149. A "2xAY" (TurboSound) mode alternates the
  chip every interrupt. The Wild Sound card plays up to 4 AYs, but no
  4-AY MASON data was seen.
- **Frame rate**: one event per channel per interrupt (50 Hz).
- **Loop**: each channel stream restarts from its beginning when it ends
  (whole-song loop, no intro).
- **Compression**: subroutine calls inside the streams. In `.ay` rips the
  player and module are also LZ-packed (see [Packing in the `.ay` rip](#packing-in-the-ay-rip)).
- **Analysis files**: `scratch/research/mason/` (git-ignored): `ayparse.py`
  (`.ay` container), `unpack.py` (port of the depacker), `compiled.py`
  (compiled-module parser and stream walker), `may_header.py` (`.may` header
  dump), `player0201.asm` / `player036e.asm` (z80dasm output).

## History and tools

| Year | Fact | Source |
|---|---|---|
| 1997 | MASON listed on zxart as Robus's tracker ("System Software / Sound / Digital Music Editors", prod 251050) | zxart, as cited in the in-progress Wild Sound research (`docs/inprogress/2026-10-09-wildsound/research-software.md`) |
| 2007 | zx-pk.ru thread "Mason-AY By Entire Group" (last reply 18.07.2007). URL not found. | zx-pk.ru "similar threads" list |
| 2009 | **Mason-AY II**: a Windows PC editor that also runs under Wine, with an installer `MasonAySetup.exe`. "Wild Sound AY mode" is on by default. Effects: only `3xxx` (slide) and `Fxxx` (speed). Robus: "compiling gives you a module with a player of about 4000 T-states"; "Double Sound is spread across a second AY". | [zx-pk.ru t=10768](https://zx-pk.ru/threads/10768-mason-ay-ii-music-editor.html) (2009–2013) |
| 2009 | "Mason-AY-II-Wild Sound by Robus F.'09 — a cross-platform music editor for AY" (`pcutilz/MASONAY.ZIP`) | [lysach.narod.ru](https://lysach.narod.ru/pcutilz.htm) |
| 2013 | Release *3123 Years Of Di Halt* contains `.may` ("Mason AY II module"), `3123-DI.COD` ("Compiled module at address 49152", **Init = 49152 (once), Play = 49155**) and `_MASON.TRD`. It ships recordings "from TurboSound", "from YM2149F" and "from Wild Sound 2xAY / 1xAY mode". | zxart tune 19742, file_id.diz in the [API record](https://zxart.ee/api/types:zxMusic/export:zxMusic/language:eng/filter:zxMusicId=19742) |
| 2018 | Robus: 2xAY has been "in MASON for about 10 years". PWM is "one parameter" away. | [hype.retroscene.org/blog/809](https://hype.retroscene.org/blog/809.html) |
| 2019 | *DiHalt RealTime*, `.may` file, 4th place in the DiHalt Lite 2019 realtime AY compo | [zxart 308023](https://zxart.ee/eng/authors/r/robus/dihalt-realtime/), [demozoo 197960](https://demozoo.org/music/197960/) |
| 2020 | Alternative AYX-32 firmware ("Wild Sound II"). Features: "internal Mason AY player"; "Phase (PWM) is supported in MASON"; "internal Mason Digital player". | [forum.tslabs.info t=883](https://forum.tslabs.info/viewtopic.php?t=883), robus, 21.01.2020 |
| 2022 | Robus: "MASON can play such music. Press COMPILE and the module can be played on Wild Sound." Also: Wild Sound plays music "for 4 AYs". | [zx-pk.com t=21431](https://zx-pk.com/forum/viewtopic.php?t=21431), post of 12.04.2022 |

Related name, different format: **MasonXM / "Mason Digital"** (2020) is a PC
converter. It turns an XM into packed patterns and instruments and builds a
TR-DOS disk that uploads them to the card's firmware XM player.
`MasonXM.pas` (`XM_SaveMasonDigitalTRD`, line 286; `XM_ModuleConvert`, line 400)
is in Robus's repository under `STM_AYX32_TSL/TMT/MasonXM/`. It has no AY part.

No format specification has been published. In 2009 Vitamin (ZXTune) asked
Robus for the specs of "BIM, MAI, RPK, MAY, MAO". They were promised but never
appeared (zx-pk.ru t=10768). **ZXTune does not support `.may`**: the source
contains no "mason". zxart lists the one `.may` tune without a format type.

## The `.may` module (editor format)

Reference file: `DiHalt_RealTime_2019.may`, 19 020 bytes, sha1
`b2404d7dede9897f793b617fa8b3bdd7b1ae1501` (zxart 308023). On scene.org the
same tune is named `DiHalt 2018 RealTime Robus Izvrat.may`.

```
00000000: 0022 6c00 a700 226c 004d 4153 4f4e 2d41  ."l..."l.MASON-A
00000010: 592d 3231 3344 6948 616c 7420 5265 616c  Y-213DiHalt Real
00000020: 5469 6d65 2032 3031 3800 0000 0003 0007  Time 2018.......
00000030: 000f 000b 00b1 526f 6220 462e 202f 2045  ......Rob F. / E
00000040: 6e74 6972 6520 4772 6f75 7020 2e2e 2e20  ntire Group ...
00000050: 3034 2e30 312e 3230 3139 202d 2031 393a  04.01.2019 - 19:
00000060: 3239 284b 4945 5629 2b32 2b00 2200 ff03  29(KIEV)+2+."...
00000070: 0100 b01b 201a aa18 4817 f815 be14 9413  .... ...H.......
00000080: 7a12 7011 7610 8a0f aa0e d80d f80e 100e  z.p.v...........
```

What can be read from this single file (`may_header.py`):

| Offset | Size | Content | Confidence |
|---|---|---|---|
| `0x00` | 9 | `00 22 6c 00 a7 00 22 6c 00`. Meaning unknown. The 24-bit value `0x006C22` (27 682) appears twice. | raw only |
| `0x09` | 12 | signature `MASON-AY-213`. The trailing digits are presumably the editor or format version (2.13, i.e. "Mason AY II"). | signature observed; version meaning is an inference |
| `0x15` | var | song title, NUL-terminated: `DiHalt RealTime 2018` | observed |
| `0x2A..0x35` | 12 | `00 00 03 00 07 00 0f 00 0b 00 b1`. Could be leftover bytes of a fixed-size title field. | unknown |
| `0x36` | var | info string, NUL-terminated: `Rob F. / Entire Group ... 04.01.2019 - 19:29(KIEV)+2+`, i.e. author, group and save timestamp. The meaning of `+2+` is unknown (2 chips?). | observed |
| `0x6C` | 6 | `22 00 ff 03 01 00` | unknown |
| `0x72` | 26 | 13 × u16 LE, `1BB0 … 0DD8`: one octave of semitones, ratio 2^(1/12), last value = first / 2. Purpose unknown. | values observed |
| `0x8C` | ~224 | note table, u16 LE. The first octave is exactly the Sound/Pro Tracker table `EF8 E10 D60 C80 BD8 B28 A88 9F0 960 8E0 858 7E0`. At `0xEF` a stray byte `FD` sits between the low and high bytes of entry `0x00E1`. After it the table continues down to `0x0005` at `0x16C`. | observed; the stray byte is unexplained |
| `0x16D..` | rest | song body: many `0x7F` bytes (3 857 of them) and repeating patterns such as `7F 00 02 FF 7F`. Probably patterns, instruments and order, terminated by `0x7F` per row or record. | **not decoded** |

The body is **not compressed**: zlib -9 shrinks the whole file to 0.226 of
its size, whereas the compiled modules below only reach 0.71. So `.may` is the
editable source form. The 2013 release confirms the split: the `.may` is
listed as "Mason AY II module" next to a separately compiled `.COD`. Decoding
the body needs the editor (`MasonAySetup.exe` / `MASONAY.ZIP`, not retrieved)
or more `.may` samples to compare. **Open.**

## The `.ay` rip: container usage

Reference file: `Robus_-_Abrove_MASON_4songs_TurboSound.ay`, 37 384 bytes, sha1
`966a9ad51370757125f9c690cccf59297eea5155` (zxart 91986 and its siblings,
"Abrove", demozoo dates it Nov 2015). Parsed with `ayparse.py`:

```
00000000: 5a58 4159 454d 554c 0103 0000 0008 000c  ZXAYEMUL........
00000010: 0300 044e 526f 6275 7300 4d41 534f 4e00  ...NRobus.MASON.
0000047e: 0001 0203 2d97 2d97 0001 0004 2892 ffff  ....-.-.....(...   song 0 data
0000048e: 0201 0000                                ....               points
```

| Field | Value |
|---|---|
| FileVersion / PlayerVersion | 1 / 3 |
| Author / Misc | `Robus` / `MASON` |
| Songs | 4: `Abrove Music`, `Abrove Music + 2xAY`, `Abrove Speed`, `Abrove Speed + 2xAY` |
| Channel map | A=0 B=1 C=2 Noise=3 |
| SongLength (= FadeLength) | 11 671 frames (songs 0–1), 7 211 (songs 2–3). This is the decoded stream length (11 520 / 7 060 frames) **plus 151** in both cases. |
| HiReg / LoReg | `00 / 01` for single-AY songs, `00 / 02` for the 2xAY songs |
| Stack / INIT / INTERRUPT | `FFFF` / `0201` / `0000` (all songs) |
| Block 0 | `0x0201`, 1 088 bytes: init stub, depacker, IM2 handler, packed player. Shared by all four songs (same file offset `0x20`). |
| Block 1 | `0x086D`, packed module (10 377 / 10 409 / 7 365 / 7 889 bytes) |

**INTERRUPT = 0**: per the AY spec the host stub is then
`DI; CALL INIT; LOOP: IM 2; EI; HALT; JR LOOP`. INIT never returns here. It
installs its own IM2 vector and halts in its own loop (`0x023D`).

**Song-select quirk.** INIT tests `A == 2` to enable 2xAY mode (`0x0205`).
Under the AY spec, A receives **HiReg** (Ay_Emul documentation; ZXTune
`src/module/players/aym/ayemul.cpp:482` fills every register pair with
`HiReg:LoReg`). HiReg is 0 in every song, so a spec-compliant player never
enables 2xAY, and the "+ 2xAY" songs play as single-AY. The file's author
evidently meant LoReg (1 or 2) to reach A. Untested in a real player.

**Pointer quirk.** Song 3's block-table entry for block 0 (at file `0x91FE`)
holds relative offset `0x6E22`. As the spec's signed offset that points past
the end of the file (`0x10020`). Only 16-bit wrap-around gives the intended
`0x0020`. ZXTune bounds-checks these offsets (`src/formats/chiptune/emulation/ay.cpp:176-183`,
per the source), so song 3 is likely rejected there (inferred, not run).

### Memory map at play time

| Address | Content |
|---|---|
| `0x01FF` | IM2 vector: I = `0x01`, data bus `0xFF` → handler `0x033D` (stored by `0x022A`) |
| `0x01FF` downwards | stack |
| `0x0201–0x023F` | init: depack player → `0x036E`; depack module → `0x086F`, move it to `0x086D`; IM2 on; call module init `0x0379`; `call 0x055C` (a bare `RET`); `EI` / `HALT` loop |
| `0x0241–0x033C` | depacker |
| `0x033D` | IM2 handler: select chip, write registers (`0x04F1`), compute next frame (`0x0419`) |
| `0x036E–0x086C` | unpacked player (1 279 bytes). `0x036E..` is reused for the three channel call stacks after relocation. |
| `0x0630–0x063B` | mixer masks, 4 per channel |
| `0x0700–0x07FF` | 16 × 16 volume table (`ch_volume × sample_volume / 15`, PT3-style) |
| `0x086D–` | compiled module (12 792 / 12 802 / 8 966 / 9 546 bytes unpacked) |

### Packing in the `.ay` rip

Both the player (`0x036E`, 723 → 1 279 bytes) and each module are packed with
a **backward LZ** whose depacker sits at `0x0241–0x033C`. Its packed-block
layout (addresses ascending): `bitstream… | u16 unpacked size | u8 long-offset
bit count`. In all five blocks the long-offset bit count is `09`. The
bitstream is read backwards and MSB first; output is written backwards.

| Prefix bits | Meaning |
|---|---|
| `0` + 8 bits | one literal |
| `11` + 1 bit | match, length 2–3 |
| `101` + 2 bits | match, length 4–7 |
| `1001` + 4 bits *v* | *v* < 15: match, length 8 + *v*; *v* = 15: literal run of 14 + (5 bits) bytes |
| `1000` + 8-bit chunks | match, length 23 + Σ chunks (continue while chunk = `FF`) |
| offset: `1` + *n* bits / `01` + 5 bits / `00` + 9 bits | distance 0x221 + v / 1 + v / 0x21 + v (copy from already written, higher addresses) |

In the rip, the module block is `u16 packed length` followed by the packed
data (`0x086D`: `87 28` = 10 375 for song 0). `unpack.py` is a straight port.
Unpacked sizes match the init's expectations, and all four modules parse
cleanly (see below). The packer itself is unidentified.

## Compiled module format

All pointers are **offsets from the module start**. The player relocates them
once at init (`0x0379–0x03CE`), then patches its relocator into a `RET`
(`0x03E6`). Stream call targets are added to the base at run time (`0x0842`).
The module is therefore position independent.

```
00000000: 7622 ae2e 041d 891b a21c 4521 080a 0962  v"........E!...b
00000010: 000f 802f 558a 2500 ff1d 8213 85af 80a5  .../U.%.........   (Abrove Music)
```

### Header (20 bytes)

| Offset | Size | Field | Used by |
|---|---|---|---|
| `+0x00` | 2 | descriptor table offset | `0x03CF` → `0x0562` |
| `+0x02` | 2 | short-call table offset | `0x03D5` → `0x0826` |
| `+0x04` | 1 | *k1*: number of long-index prefix codes | `0x037C` → `0x0816`, `0x0833` |
| `+0x05` | 1 | *k2*: first short-call code − `0x80` | `0x0387` → `0x081A`, `0x0822` |
| `+0x06` | 2 | channel A stream offset (→ R0/R1/R8) | `0x0494` |
| `+0x08` | 2 | channel B stream offset (→ R2/R3/R9) | `0x0456` |
| `+0x0A` | 2 | channel C stream offset (→ R4/R5/R10) | `0x0419` |
| `+0x0C` | 3 | call-stack depth for A, B, C (= maximum nesting + 1) | `0x03DB`, `0x0402` |
| `+0x0F` | 2 | number of short-call table entries (= `0x7F − k2`) | not read by the player |
| `+0x11` | 1 | number of samples | `0x03A6` |
| `+0x12` | 2 | sample pointer table offset | `0x03AA` → `0x0588`/`0x058B` |

The bytes from `+0x0C` onwards are overwritten at init by the three channel
stack pointers.

### Region order (observed in all 4 modules, regions tile exactly)

1. header, `0x0000–0x0013`
2. subroutines reached by long calls (the first at `0x0014`)
3. subroutines reached by short calls
4. channel A, B and C streams. Each ends with a bottom-level `FF` exactly where the next region begins.
5. descriptor table, *N* × 4 bytes. *N* = highest index used + 1. It ends where the call table begins.
6. short-call table, u16 × count
7. `FF FF FF FF`, a copy of the three channel offsets, `FF FF`, `09 AA` (meaning unknown, not read)
8. sample pointer table, then `FF` × 8
9. sample bodies. The last one ends exactly at the end of the module.

### Channel stream bytes (sequencer `0x080E`)

Each frame, the sequencer reads bytes until it yields one descriptor index:

| Byte *a* | Size | Meaning |
|---|---|---|
| `00–7F` | 1 | descriptor index *a* → **end of this frame** |
| `80 … 80+k1−1` | 2 | descriptor index `(a−0x80)<<8 | next` → end of frame |
| `80+k1 … 80+k2−1` | 2 | **long call**: push return address, jump to offset `(a−0x80−k1)<<8 | next` |
| `80+k2 … FE` | 1 | **short call** through the table entry `a−0x80−k2` |
| `FF` | 1 | **return**. At the bottom of the stack (sentinel `0000`), restart the channel from its start offset: the song loops. |

The return address is pushed on a per-channel stack (IX-based, 2 bytes per
level). Its depth comes from header `+0x0C`. Measured maximum nesting: 7/9/8
(Abrove Music) and 12/12/10 (Abrove Speed), always header − 1.

### Frame descriptor (4 bytes, engine `0x055D`)

| Byte | Content |
|---|---|
| 0–1 | u16 LE: bits 0–11 = **base tone period**. Bits 12–15 = **envelope shape** (0 = no envelope). |
| 2 | shape = 0: channel **volume** 0–15. Shape ≠ 0: **envelope period low byte** → R11. |
| 3 | **sample number** 1..n to (re)start, with tone, noise and volume accumulators reset. 0 = keep running the current sample. |

With an envelope, the channel volume register gets `0x10`, the shape goes to
R13 and byte 2 goes to R11. **R12 is never written** (stays 0), so envelope
periods are limited to 1–255. Shapes used in the corpus: 8, 10 and 14 (sawtooth
and triangle, i.e. "buzzer" basses), 149–198 envelope descriptors per song.

### Sample (instrument) records (4 bytes each)

| Byte | Content |
|---|---|
| 0–1 | u16 LE: tone offset added to the base period (bits 0–11 matter; the AY ignores the high bits of R1/R3/R5). Bits 12–15 = **sample volume** 0–15. |
| 2 | noise value / noise delta |
| 3 | flags (below) |

| Flag bit | Effect (`0x05AE–0x05FD`) |
|---|---|
| 7 | last record. A u16 **loop offset** follows; the next frame continues there. |
| 6 | tone accumulate: add the offset to a running sum (IY) instead of using it once. |
| 5 | noise accumulate: add byte 2 to a running noise sum (IXh). |
| 4 | volume slide: running ±1 per frame (IXl), clamped at ±64. Applied as `sample_vol + slide`, clipped to 0–15. |
| 3 | slide direction (1 = up, 0 = down) |
| 2 | tone enable (mixer) |
| 1 | noise enable (mixer); also writes R6 = noise value + noise sum |
| 0 | set in 18–39 records per tune, **ignored by this player** |

Output volume = `voltab[ch_volume][clip(sample_vol + slide)]` (table at
`0x0700`). The tone period is `base + offset (+ tone sum)`.

### Validation (`compiled.py`)

For all four modules, every check below holds with no exceptions. Three
independently walked channel streams have **equal frame counts** (11 520 /
11 520 / 7 060 / 7 060). Header depths equal measured nesting + 1. Header
`+0x0F` equals the number of call codes. The descriptor table ends exactly at
the call table. The last sample ends exactly at the module end.

## Playback

### Z80 player (from the `.ay` rip)

- Entry points in the rip: INIT `0x0201` (depack, then module init `0x0379`),
  frame = IM2 handler `0x033D`. The standalone compiled `.COD` of the 2013
  release uses **init = base, play = base + 3** at `0xC000` (file_id.diz). Its
  code was not available for comparison.
- Per frame: the handler first selects the chip (`OUT (#FFFD)`), then writes
  **R0–R12 every frame** with `OUTI` pairs on `#FFFD`/`#BFFD` (`0x04F1–0x054E`).
  It writes **R13 only when the value differs from the last one written**
  (`0x054F`), so repeating the same shape does not retrigger the envelope.
  Then it computes the next frame for C, B, A (`0x0419`). The register write
  is therefore one frame behind the sequencer.
- Robus quotes about 4 000 T-states per frame for the compiled player
  (zx-pk.ru t=10768). Not measured here.
- **Chip select / 2xAY** (`0x034B–0x0356`): `ld a,#FE; inc a` with
  self-modification and `or #FE` alternate `#FF`, `#FE`, … on `#FFFD`, i.e.
  TurboSound chip 0 and chip 1 on alternate interrupts. Each chip thus gets
  every other frame of the same stream, held for 2 frames. In single-AY mode
  INIT turns the `inc a` into `nop`, so `#FE` (TurboSound **chip 1**) is
  selected every frame. On a plain AY, `#FE` is just an ignored register
  number. (Chip codes: `core/src/emulator/sound/chips/soundchip_turbosound.cpp:453-458`.)
  The 2xAY modules are separate compilations with arrangement edits. The
  share of identical frame descriptors per channel ranges from 43 % to 97 %,
  with no even/odd-frame pattern.

### Wild Sound firmware

Robus describes an internal "Mason AY player" in the Wild Sound II firmware
(tslabs t=883, 2020) and says COMPILE output plays on Wild Sound (zx-pk.com
t=21431). Neither the published firmware sources in his repository
(`STM_AYX32_TSL_WILDSOUND/SRC`, 2021-04-23 build) nor the AYX32 tool contain
it. The stream commands are only `NOP/CLEAR/ERASE/XMAST/XMADO/XMBST/XMBDO/DIGPLAY`
(`ChipAY_StreamData.h:19-26`), and `grep -i mason` matches only the MasonXM
converter. See also `docs/inprogress/2026-10-09-wildsound/reference-firmware.md`.
**How a MASON tune reaches the card's internal player is not determined.**
What does work with the published firmware is the plain path: a Z80 player
writing AY registers through the card's AY ports (two chips, TurboSound
select), as in the rip above.

## Relevance for unreal-ng

- Compiled MASON tunes are ordinary Z80 AY software. Emulating them needs
  only a correct AY / TurboSound with the `#FF`/`#FE` chip select and IM2
  with a `0xFF` data bus. No card-specific features are needed.
- An emulated Wild Sound card therefore needs no MASON support for these
  files. An "internal MASON player" cannot be implemented: its input format
  (`.may` body) and upload protocol are unknown.
- A loader for `.ay` files (not present in `core/src/loaders`) would have to
  (a) accept the 16-bit-wrapped block pointer of song 3, and (b) decide
  whether A gets HiReg (spec: no 2xAY) or LoReg (the author's intent: 2xAY
  works).
- Test material: the two reference files above (local downloads under
  `~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/downloads/zxart.ee/`)
  and other "+ 2xAY" `.ay` tunes by Robus on zxart (3123 Years Of Di Halt,
  Drink For Peace). None is in `testdata/` yet.

## Open questions

- `.may` body layout; the meaning of the 9-byte prefix, of `-213`, of `+2+`
  and of the stray `FD` in the note table.
- The internal Wild Sound MASON player: firmware, input format, upload path.
- Whether the 2013 `.COD` (init +0 / play +3) uses the same module layout
  as the rip (very likely, unverified).
- Use of sample flag bit 0 and of the `09 AA` marker.
- PWM ("Phase") and 4-AY output of MASON: claimed, not seen in any file.

## Sources

**Local** (Robus's repository, a clone of GitHub `RobusAsmCoder/Wild-Sound`, kept
under `~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/Wild-Sound`):
`STM_AYX32_TSL/TMT/MasonXM/MasonXM.pas:286,400`;
`STM_AYX32_TSL/TMT/MasonXM/BOOT/XMPLOAD/XMPLOAD.AZ8:423-499`;
`STM_AYX32_TSL/STM_AYX32_TSL_WILDSOUND/SRC/ChipAY_StreamData.h:19-26`.
zx-pk.com t=21431 page snapshot under `~/Downloads/zx-spectrum/sprinter/peripherals/web/`.

**Online**
- zxart author Robus: https://zxart.ee/eng/authors/r/robus/ (API: `https://zxart.ee/api/types:author/export:author/language:eng/filter:authorId=19741`)
- zxart 308023 DiHalt RealTime: https://zxart.ee/eng/authors/r/robus/dihalt-realtime/
- zxart 91986 Abrove Speed + 2xAY: https://zxart.ee/eng/authors/r/robus/abrove-speed-2xay-4/
- zxart 19742 (3123 Years Of Di Halt, file_id.diz): https://zxart.ee/api/types:zxMusic/export:zxMusic/language:eng/filter:zxMusicId=19742
- DiHalt Lite 2019 archive: https://files.scene.org/view/parties/2019/dihaltlite19/realtime_ay_music/robus-dihalt_realtime.zip
- Demozoo: https://demozoo.org/music/197960/, https://demozoo.org/sceners/9322/
- Mason-AY II thread: https://zx-pk.ru/threads/10768-mason-ay-ii-music-editor.html
- Virtual TR-DOS PC utilities: https://lysach.narod.ru/pcutilz.htm
- Wild Sound II firmware thread: https://forum.tslabs.info/viewtopic.php?t=883
- Sprinter boards thread (Robus 12.04.2022): https://zx-pk.com/forum/viewtopic.php?t=21431
- hype: https://hype.retroscene.org/blog/809.html
- AY (ZXAYEMUL) format, Ay_Emul documentation: https://documentation.help/AY-3-8910.12-ZX-Spectrum/documentation.pdf
- ZXTune: https://github.com/vitamin-caig/zxtune (`src/formats/chiptune/emulation/ay.cpp`, `src/module/players/aym/ayemul.cpp`)

## Document History

- 2026-10-09: Initial version. Compiled-module format and `.ay` packing decoded
  from the disassembled rip player and verified on its 4 modules. `.may`
  header only. Web facts collected from zxart, zx-pk, tslabs, hype and ZXTune.
