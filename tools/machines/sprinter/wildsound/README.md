# Wild Sound tools: XM / S3M converter and EXE linker

Tools for the ISA Wild Sound card on the Sprinter
([design](../../../../docs/inprogress/2026-10-09-wildsound/design.md) §9, requirements R41 / R42).

| File | What it does |
|:--|:--|
| `wsconv.py` | XM or S3M → the card's upload chunks in the MasonXM layout; `--check-against` compares with MasonXM's own outputs |
| `wslink.py` | the chunks of one track → a Sprinter DSS EXE that plays it, relinked from `prosiak.exe` |
| `lzsa2.py` | LZSA2 raw blocks: the decoder of the prosiak disassembly, a compressor, an in-place check |
| [`wsay/`](wsay/README.md) | a PT3 player for the card's two AY chips (single or TurboSound), built per track with sjasmplus |
| `test_wildsound_tools.py` | the tests: `python3 -m unittest -v test_wildsound_tools` from this folder |

Python 3, standard library only. Run them with `python3 -I` on downloaded modules.

```bash
python3 -I wsconv.py song.xm -o out/            # out/song.trk00.., out/song.ins00, out/song.ins01..
python3 -I wslink.py out/song -o song.exe       # prosiak.exe from testdata/sound/wildsound/ or ~/Downloads/...
python3 -I wslink.py out/song -o song.exe --prosiak prosiak.exe --banner "My Song"
python3 -I wslink.py --unpack song.exe dir/     # blocks and script of any EXE of this layout
python3 -I wsconv.py --check-against .../MasonXM/Music
```

## The chunks (`wsconv.py`)

The formats are in [reference-firmware.md](../../../../docs/inprogress/2026-10-09-wildsound/reference-firmware.md)
§C. Per track:

| File | Content | Card command |
|:--|:--|:--|
| `NAME.trk00`, `.trk01`, ... | u32 total pack size, then the track pack (`tPatternsPacked`: 292-byte header, `PatNum` × 10-byte pattern records, XM-packed pattern data), cut into 32 768-byte chunks; the prefix counts in chunk 00 (MasonXM's `xmPatPack00`) | `XMAST` |
| `NAME.ins00` | the instrument table, one 21-byte record per instrument (sample 0 only) | `XMBST` |
| `NAME.ins01`, ... | the 8-bit unsigned sample stream (#80 = silence, no #00 byte), 32 768-byte chunks | `XMBDO` each |

`NAME` defaults to the module's file name, the 20-byte track name to the module's song name (`--title`
replaces it, like MasonXM's second argument). The summary lists channels, stored patterns, orders,
instruments, the bytes the card's storage takes (pack + table + per instrument length + 48, the blank
instrument 0) and the warnings.

### XM: what differs from MasonXM

The converter follows MasonXM (`RobXM.pas`) and fixes what it gets wrong. `--masonxm` switches the
fixes off; that output is byte-identical to MasonXM's for all six converted tracks (below), so the
reader is the same and the differences are only these:

| | MasonXM | here |
|:--|:--|:--|
| 16-bit samples | delta-decodes byte by byte (no carry), then keeps every second byte | delta-decodes 16-bit words, keeps the high byte of each decoded value |
| `SType` | the raw XM type byte (`#11` = 16-bit forward loop, which the firmware treats as "no loop") | `type & 3`; 0 when the loop is empty, the type is 3 or the loop starts past the end |
| a pattern used in the order list with no data (XM packed size 0) | left out: `SIZE` 0, `OFS` not relocated (the firmware reads garbage) | stored: `Channels` × #80 per row |
| unused pattern records below `PatNum` | all zero | `OFS` = header size (a valid pointer; the firmware takes `PatNum` from record 0's `OFS`, so record 0 must be that even when pattern 0 is unused) |
| an order entry naming a pattern that does not exist | left out | an empty 64-row pattern, with a warning |
| ping-pong loop | unrolled: the loop plus a reversed copy of it minus its last byte, the tail after the loop dropped, `Loop + 1`, `SType` 2 kept, so the firmware ping-pongs over a shifted region of the unrolled data | stored as it is with `SType` 2: the firmware's mixer (and the emulated card) plays ping-pong itself (fw §C.3, §D.4); the data and the tail after the loop stay as in the XM |

The rest is as MasonXM does it: only the patterns of the order list (first `SongLen` entries) are
stored, in pattern-number order, the XM packed bytes verbatim; `PatNum` = highest stored pattern + 1;
the whole 256-byte order table and the raw XM flags (`FreqTable`) are copied; instruments without
samples get an all-zero record (its `OFS` 0 never matches a stream position in the firmware's routing);
signed → XOR #80, then #00 → #01; no loop → `Loop = LoopEnd = Length`.

Warnings: instruments with more than one sample (only sample 0 is used: format limit), envelopes,
key-off notes (the firmware plays note 97 as a note), a linear frequency table (the firmware uses Amiga
periods for every module), more than 32 channels, more than 61 instruments (64 descriptors), more than
999 424 bytes of storage.

### S3M

The S3M is read completely (header, channel settings, default pan table, orders, PCM instruments,
patterns) and turned into the same XM-like module:

- **Channels**: the enabled PCM channels (settings 0-15), in order; AdLib and disabled channels are
  dropped. `Channels` = their number.
- **Orders**: 254 (marker) is skipped, 255 ends the list; `Bxx` targets are renumbered accordingly.
  `PatRes` 0. Initial speed → `TMP`, initial tempo → `BPM`. `FreqTable` 0 (Amiga).
- **Instruments**: each PCM sample becomes a one-sample instrument at the same number; empty and AdLib
  slots become empty records. Volume (clamped to 64), C2Spd → relative note + finetune (C2Spd 8363 =
  0 / 0), panning #80. Samples: signed or unsigned by the header's `Ffi`, 16-bit → the high byte,
  stereo → the left channel, loops (flag 1) with `SType` 1. Packed (DP30ADPCM) samples are left empty.
- **Patterns**: 64 rows, re-encoded in XM packing. Note = octave × 12 + semitone + 1 (C-4 = 49);
  note cut (`^^`) → volume column 0 (`#10`), else `C00`, because the firmware plays the XM key-off
  (97) as a note. Volume → `#10 + volume`.
- **Header global volume and channel panning**: `Gxx` and `8xx` on row 0 of the first pattern played,
  in cells whose effect is free (pans first; a warning counts what did not fit). Stereo only (master
  volume bit 7); pans from the default pan table when present, else 3 (left) / 12 (right) × 17.
  The card's firmware ignores both effects; they matter for other XM players.

| S3M | XM | | S3M | XM |
|:--|:--|:--|:--|:--|
| `Axx` speed | `Fxx` (xx ≤ #1F; `A00` dropped) | | `Jxy` | `0xy` (`J00` dropped: no XM memory) |
| `Txx` tempo | `Fxx` (xx ≥ #20; below: dropped) | | `Kxy` / `Lxy` | `6xy` / `5xy` (fine slides: slide dropped) |
| `Bxx` | `Bxx` (renumbered order) | | `Oxx` | `9xx` |
| `Cxy` (decimal row) | `Dxy` (x×10+y > 63 → `D00`) | | `Qxy` | `Rxy` |
| `D0y` / `Dx0` / `D00` | `A0y` / `Ax0` / `A00` | | `Rxy` | `7xy` |
| `DxF` / `DFy` | `EAx` / `EBy` | | `S1x` `S2x` `S3x` `S4x` | `E3x` `E5x` `E4x` `E7x` |
| `Dxy` both set | `A0y` (ST3 slides down), counted | | `S8x` | `8xx` (x × 17) |
| `Exx` / `EFx` / `EEx` | `2xx` / `E2x` / `X2x` | | `SBx` `SCx` `SDx` `SEx` | `E6x` `ECx` `EDx` `EEx` |
| `Fxx` / `FFx` / `FEx` | `1xx` / `E1x` / `X1x` | | `Xxx` (0-#80) | `8xx` (xx × 2, max #FF) |
| `Gxx` `Hxy` | `3xx` `4xy` | | `Vxx` | `Gxx` (max 64) |
| `Ixy` tremor | `Txy` | | others (`U`, `W`, `Y`, `Z`, `S0x`, `S5x`-`S7x`, `S9x`, `SAx`, `SFx`, surround) | dropped, counted |

Effect memory differs between the formats (S3M shares one memory among several effects, XM keeps one
per effect) and is not converted.

## The EXE (`wslink.py`)

The layout of `prosiak.exe` is described in the prosiak disassembly
(`docs/disasm/software/sprinter/prosiak-wildsound/README.md`, branch `disasm-prosiak`). The new file:

| File offset | Content |
|:--|:--|
| `#0000` | prosiak's header; `+4` = the new loader offset |
| `#0100` | the new VM script |
| `#0180` | block 0, prosiak's record byte for byte (the player at `#4500`); further up when the script is longer than `#80` bytes (more than 10 chunks, e.g. DISCHARZ: script 160 bytes, block 0 at `#1A0`; the first script entry names the offset) |
| ... | one LZSA2 record per chunk, back to back |
| end | prosiak's primary loader (`#6AC` bytes; stage 2, the VM, is packed inside it) |

The script:

```text
05 A DE HL BC  24 00 45      block 0, PlayerInit (probe the card)
05 ...         24 55 47      per track chunk: LoadTrack
05 ...         24 96 47      ins00: LoadInsHeaders
05 ...         24 AB 47      per sample chunk: LoadSamples
24 C0 47  24 1C 01           Play (DIGPLAY, waits for a key), VmExit
```

`05 A DE HL BC` reads `BC` bytes from file offset `#100 × (DE + 1) + A` to `HL` and unpacks the record
(`[u16 unpacked length][LZSA2 raw block]`) in place: output start = record end − unpacked length. The
VM saves the 32 bytes behind the record, moves the stream up by 32 bytes (`LDDR`) and decodes forward
from there, so writes must stay behind reads with a 32-byte margin. Placement, as prosiak's own records:
every chunk unpacks to `#4E00` (just above the player and its variables, `#4500-#4DF4`); `HL = #4E00 +
max(length, record length) − record length`, so the record never starts below `#4E00` and the record end
+ 32 stays below `#CE30` (a chunk is at most `#8004` bytes, a record at most 9 bytes longer; the VM's
other path for records whose 32 saved bytes would pass `#FFFF` is never needed). The player copies from window 3 through window 2
when the data reaches `#C000` (`IsaOutBlock`), as for prosiak's own `#8000`-byte blocks.

**Two track chunks** (DISCHARZ, 50 206 bytes): `LoadTrack` (`#4755`) keeps a call counter (`#4754`).
On the first call it reads the u32 prefix as the total, sends `NOP`, `CLEAR`, `ERASE`, `XMAST` with that
size and uploads the rest of the chunk; every call subtracts what it uploaded from the 32-bit total and
sends the closing empty packet when it reaches 0. A second chunk is uploaded as it is. That is exactly
what MasonXM's `DISCHARZ.CMD` does (two `UpLoadAY_TRKN` calls, then `UpLoadAY_INSHDR`, then one
`UpLoadAY_INS` per sample chunk, then `UpPlayer`), so the script calls `#4755` once per track chunk.
`wslink` checks that the prefix equals the bytes of all track chunks.

**Banner**: stage 2 prints the zero-terminated string at `#44D4` in the loader (`!!! WOW HOW DOW !!!`,
CR LF; the line after "This Text From Shadow Page ..."). It is data, not code (nothing else refers to
it), so `wslink` replaces it with the track name or `--banner` (printable ASCII, 19 characters at most,
CR LF and zeros after it). `--keep-banner` leaves the loader byte for byte. The player itself prints no
track name ("XM Init ... OK", "Wild Sound ... ISA #n", "XM TRK Data", "XM INS HDR", "XM INS", "XM PLAY"
are all it has), and the "Boot Loader For Sprinter By Rob F." banner is kept.

**Checks after linking** (always): the new file is parsed again (header, script), every record is
decoded with the Python decoder and its in-place unpacking is emulated on a 64 KB memory image the way
the VM does it (record at `HL`, 32 bytes saved, stream moved, LZSA2 decode in the same buffer, nothing
written outside the record area); block 0 must be prosiak's record, the loader prosiak's (or with the
new banner), the calls in order, the decoded chunks equal to the inputs. `--lzsa` also decodes every
record with the official `lzsa` tool. `--compare-with OTHER.EXE` compares the decoded blocks with
another EXE's.

### LZSA2 compressor

`lzsa2.compress` writes raw blocks with the end-of-data marker: hash chains over 3-byte prefixes (24
candidates), the repeat offset tried first, lazy matching one byte ahead, a cost model by offset class
(5 / 9 / 13 / 16 bit, repeat) and length extension; matches at most 255 bytes (no 16-bit match length
form), literal runs of any length (16-bit literal length above 255; the Z80 depacker reads both forms the
same way as the Python decoder, `DepackLength` at `#3DB7`). The output is 2-3 % larger than the
official tool's optimal parse (prosiak's samples: 31 755 vs 31 105 bytes for a 32 KB chunk).

`compress_in_place(data, 32)` makes the block safe for the VM: when the emulated in-place unpacking
fails (the output overtakes the input where well-packed data is followed by data that does not pack), the
data from the command where the output leads the input most is stored as literals, and the check runs
again; the last resort is a block of literals only (always safe, 7 bytes longer than the data).

## Verification

`python3 -m unittest test_wildsound_tools`: 24 tests, about 6 s: LZSA2 round trips (empty, short,
zeros, noise, text, a 70 KB literal run, 60 KB offsets) with the Python decoder, the official `lzsa -d -r
-f 2` (when built at `scratch/prosiak/ext/lzsa/lzsa`) and the in-place emulation; generated XM modules
(header, `PatREC[0].OFS`, explicit empty pattern, unused records, 8- and 16-bit samples, `SType`, #00 →
#01, loops, chunking with the prefix, limit warnings, `--masonxm` behaviour); a generated S3M (orders
with 254 / 255, notes, note cuts, every mapped effect, dropped ones, injected pans / global volume,
signed / unsigned / 16-bit samples, C2Spd); linking against a synthetic prosiak-shaped file (round
trip, banner, long script moving block 0, two track chunks, bad input). With the author's folder
and `prosiak.exe` present: the MasonXM comparison and the real relink below.

### MasonXM comparison

`wsconv.py --check-against .../TMT/MasonXM/Music` (Wild-Sound repository, 2021-05-23), 2026-10-09:

```text
CONSUL.XM -> Consul: --masonxm output byte-identical to MasonXM's (1 track chunk(s), 6 instrument chunk(s))
  track pack: identical
  instrument table and samples: identical
PROSIAK.XM -> Prosiak: --masonxm output byte-identical to MasonXM's (1 track chunk(s), 6 instrument chunk(s))
  track pack: identical
  instrument table and samples: identical
FASTER-2.XM -> FASTER-2: --masonxm output byte-identical to MasonXM's (1 track chunk(s), 5 instrument chunk(s))
  track pack: identical
  instrument 12: ping-pong kept as a loop (MasonXM unrolled it: Length 20760 -> 12890, Loop 5020 -> 5019, LoopEnd 12891 -> 12890)
BALLADAL.XM -> BALLADAL: --masonxm output byte-identical to MasonXM's (1 track chunk(s), 3 instrument chunk(s))
  track pack: identical
  instrument table and samples: identical
DISCHARZ.XM -> DISCHARZ: --masonxm output byte-identical to MasonXM's (2 track chunk(s), 11 instrument chunk(s))
  track pack: identical
  instrument table and samples: identical
CHECKNO1.XM -> checkno1: --masonxm output byte-identical to MasonXM's (1 track chunk(s), 5 instrument chunk(s))
  track pack: 2 byte(s) differ
    record 22: unused: MasonXM OFS 0, here OFS 562 (the header size)
  instrument table and samples: identical
unexplained differences: 0
```

The track name of each comparison is taken from MasonXM's pack (Prosiak and checkno1 were converted
with a second argument). The author's six XMs have no 16-bit samples and no used-but-empty patterns, so
of the fixes only the ping-pong loop (FASTER-2 instrument 12, the loop runs to the sample's end) and the
unused record (CHECKNO1 pattern 22) show up; the 16-bit decoding, `SType & 3` on 16-bit loops and the
explicit empty patterns are covered by the generated modules of the tests.

### prosiak.exe relinked

`wslink.py .../Music/Prosiak -o prosiak-relink.exe --keep-banner --compare-with prosiak.exe --lzsa ...`
(MasonXM's chunks): all 8 decoded blocks equal the original's (block 0 the same record; blocks 1-7
the same data, addresses and calls), the loader and header identical except the loader offset; 153 336
bytes against the original's 150 003 (the compressor, see above). Every record of the original also
passes the in-place emulation. DISCHARZ from `wsconv` output: 13 chunks, script 160 bytes, block 0 at
`#1A0`, 279 176 bytes, checked with `lzsa` as well.

## Limits and open points

- Not run on a machine or in the emulator yet: the card is not emulated on this branch, and prosiak.exe
  stops at "Not Found" without it. The EXE is checked statically (script, records, in-place emulation).
- The banner patch is read from the code (stage 2 `#0106`: `LD HL,#44D4; LD C,#5C; RST #10`), not seen
  on screen.
- One sample per instrument, no envelopes, no key-off, no 8xx / G in the firmware (fw §D.2): the
  converter warns, the card plays what its firmware plays.
- S3M: effect memory, `U` fine vibrato, panning effects beyond 8xx and AdLib instruments are not
  converted; no S3M test files from real trackers were at hand, only generated ones.
- A chunk larger than `#8004` bytes is refused (wsconv never makes one).
