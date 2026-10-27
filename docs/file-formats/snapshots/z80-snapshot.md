# Z80 Snapshot Format

The `.z80` format is among the most widely supported by emulators on every platform. A `.z80` file is a memory snapshot: an
image of the Spectrum's memory and registers at one instant. It cannot reproduce the original tape, but it loads almost
instantly.

It was developed by Gerton Lunter for his Z80 emulator. Three versions are in use, as saved by Z80 1.45 and earlier, 2.x and
3.x and later; they are called **versions 1, 2 and 3** here. Other emulators extended the format with more machine types.

All multi-byte values are little-endian.

---

## Version 1 header (48K only)

Version 1 saves 48K snapshots only. The header is 30 bytes:

| Offset | Length | Description |
|-------:|-------:|-------------|
| 0 | 1 | A register |
| 1 | 1 | F register |
| 2 | 2 | BC register pair (C first) |
| 4 | 2 | HL register pair |
| 6 | 2 | Program counter |
| 8 | 2 | Stack pointer |
| 10 | 1 | Interrupt register (I) |
| 11 | 1 | Refresh register (R); bit 7 is not significant |
| 12 | 1 | Bit 0: bit 7 of R<br>Bits 1-3: border color<br>Bit 4: 1 = Basic SamRom switched in<br>Bit 5: 1 = the block of data is compressed<br>Bits 6-7: no meaning |
| 13 | 2 | DE register pair |
| 15 | 2 | BC' register pair |
| 17 | 2 | DE' register pair |
| 19 | 2 | HL' register pair |
| 21 | 1 | A' register |
| 22 | 1 | F' register |
| 23 | 2 | IY register |
| 25 | 2 | IX register |
| 27 | 1 | Interrupt flip-flop: 0 = DI, otherwise EI |
| 28 | 1 | IFF2 |
| 29 | 1 | Bits 0-1: interrupt mode (0, 1 or 2)<br>Bit 2: 1 = Issue 2 emulation<br>Bit 3: 1 = double interrupt frequency<br>Bits 4-5: 1 = high video synchronization, 3 = low, 0 or 2 = normal<br>Bits 6-7: joystick: 0 = Cursor / Protek / AGF, 1 = Kempston, 2 = Sinclair 2 left (or user defined in v3), 3 = Sinclair 2 right |

For compatibility, a byte 12 of 255 has to be read as 1.

The 48K of memory follows the 30 bytes, compressed if bit 5 of byte 12 is set.

### Compression

Repetitions of at least five equal bytes are replaced by a four-byte code `ED ED xx yy`: byte `yy` repeated `xx` times. Only
runs of at least five are coded, with one exception: a run of `ED` bytes is coded even when it is two long (`ED ED 02 ED`).
Every byte directly following a single `ED` is left out of a run: `ED` followed by six zeros becomes `ED 00 ED ED 05 00`, not
`ED ED ED 06 00`. In version 1 the data ends with the marker `00 ED ED 00`; versions 2 and 3 have no end marker.

---

## Versions 2 and 3

The file starts with the same 30 bytes, except that bits 4 and 5 of byte 12 have no meaning and the program counter (bytes 6
and 7) is **zero**: that is how a version 2 or 3 file is recognized. An additional header follows:

| Offset | Length | Description |
|-------:|-------:|-------------|
| 30 | 2 | Length of the additional header (23 for version 2; 54 or 55 for version 3) |
| 32 | 2 | Program counter |
| 34 | 1 | Hardware mode (below) |
| 35 | 1 | SamRam: the bitwise state of the 74LS259 (bit 6 = 1 after `OUT 31,13`)<br>128 mode: the last `OUT` to #7FFD<br>Timex mode: the last `OUT` to #F4 |
| 36 | 1 | #FF if Interface 1 ROM is paged; Timex mode: the last `OUT` to #FF |
| 37 | 1 | Bit 0: 1 = R register emulation on<br>Bit 1: 1 = LDIR emulation on<br>Bit 2: AY sound in use, even on 48K machines<br>Bit 6: (with bit 2) Fuller Audio Box emulation<br>Bit 7: modify hardware (below) |
| 38 | 1 | The last `OUT` to #FFFD (the sound chip register number) |
| 39 | 16 | The sound chip registers |
| 55 | 2 | Low T-state counter *(version 3)* |
| 57 | 1 | High T-state counter *(version 3)* |
| 58 | 1 | Flag byte used by Spectator (the QL-hosted Spectrum emulator); ignored by Z80 when loading, zero when saving |
| 59 | 1 | #FF if the MGT ROM is paged |
| 60 | 1 | #FF if the Multiface ROM is paged. Should always be 0 |
| 61 | 1 | #FF if addresses 0-8191 are ROM, 0 if RAM |
| 62 | 1 | #FF if addresses 8192-16383 are ROM, 0 if RAM |
| 63 | 10 | Five keyboard mappings for the user-defined joystick |
| 73 | 10 | Five ASCII words: the keys the mappings correspond to |
| 83 | 1 | MGT type: 0 = Disciple + Epson, 1 = Disciple + HP, 16 = Plus D |
| 84 | 1 | Disciple inhibit button: 0 = out, #FF = in |
| 85 | 1 | Disciple inhibit flag: 0 = ROM pageable, #FF = not |
| 86 | 1 | The last `OUT` to #1FFD *(only when the length at 30 is 55)* |

Version 2 has the fields up to byte 54. Version 3 adds the T-state counters and everything after.

### T-state counters

The high counter counts up modulo 4: just after the ULA's 20 ms interrupt it is 3, and it increases by one every 5 emulated
milliseconds. In each 1/200 s interval the low counter counts down from 17471 to 0 (17726 in the 128K modes), which makes
69888 (70908) T-states per frame. Together they give the position inside the frame.

### Keyboard mappings

The five ASCII words at 73-82 are the keys for joystick left, right, down, up and fire; Shift, Symbol Shift, Enter and Space are
written as `[`, `]`, `/` and `\`. The ASCII values are only for display. The five mapping words decide which key is pressed: the
low byte is the keyboard row (0-7), the high byte a mask for the column. Enter is `0x0106` (row 6, column 1); `g` is
`0x1001`.

### Hardware mode (byte 34)

| Value | Meaning in version 2 | Meaning in version 3 |
|------:|----------------------|----------------------|
| 0 | 48K | 48K |
| 1 | 48K + Interface 1 | 48K + Interface 1 |
| 2 | SamRam | SamRam |
| 3 | 128K | 48K + M.G.T. |
| 4 | 128K + Interface 1 | 128K |
| 5 | — | 128K + Interface 1 |
| 6 | — | 128K + M.G.T. |

(The documentation of Z80 3.00 to 3.02 had the SamRam and 48K + M.G.T. entries reversed in the second column; files written by
those versions follow the table above.)

Other emulators extended the table. While most write version 3, some write version 2, so any value can be seen in either:

| Value | Machine |
|------:|---------|
| 7 | Spectrum +3 |
| 8 | (written by some versions of XZX-Pro for a +3) |
| 9 | Pentagon (128K) |
| 10 | Scorpion (256K) |
| 11 | Didaktik-Kompakt |
| 12 | Spectrum +2 |
| 13 | Spectrum +2A |
| 14 | TC2048 |
| 15 | TC2068 |
| 128 | TS2068 |

If bit 7 of byte 37 is set the hardware is modified slightly: a 48K machine becomes a 16K, a 128K becomes a +2 and a +3 becomes a +2A.

### Memory blocks

After the additional header come memory blocks, each holding one 16 KB page:

| Byte | Length | Description |
|-----:|-------:|-------------|
| 0 | 2 | Length of the compressed data (without this 3-byte header). **0xFFFF**: the data is 16384 bytes long and not compressed |
| 2 | 1 | Page number |
| 3 | n | The data |

The compression is the one above, without the end marker. Page numbers depend on the hardware mode:

| Page | 48K mode | 128K mode | SamRam mode |
|-----:|----------|-----------|-------------|
| 0 | 48K ROM | ROM (BASIC) | 48K ROM |
| 1 | Interface 1, Disciple or Plus D ROM, as set | Same | Same |
| 2 | — | ROM (reset) | SamRam ROM (BASIC) |
| 3 | — | RAM page 0 | SamRam ROM (monitor, ...) |
| 4 | 8000-BFFF | RAM page 1 | Normal 8000-BFFF |
| 5 | C000-FFFF | RAM page 2 | Normal C000-FFFF |
| 6 | — | RAM page 3 | Shadow 8000-BFFF |
| 7 | — | RAM page 4 | Shadow C000-FFFF |
| 8 | 4000-7FFF | RAM page 5 | 4000-7FFF |
| 9 | — | RAM page 6 | — |
| 10 | — | RAM page 7 | — |
| 11 | Multiface ROM | Multiface ROM | — |

In 48K mode pages 4, 5 and 8 are saved; in SamRam mode pages 4 to 8; in 128K mode all pages from 3 to 10. Pentagon snapshots
are like 128K ones; Scorpion snapshots have the 16 RAM pages in pages 3 to 18.

### Notes

- Byte 60 must be zero: the Multiface RAM is not saved, so a program that had the Multiface paged will probably crash on load.
- Bytes 61 and 62 are a function of the other flags (bytes 34, 59, 60 and 83).

---

## In Unreal-NG

Snapshot overview and how loading works: [README](README.md).

**Reading.**

- The version comes from the header: a nonzero PC in bytes 6-7 is version 1; otherwise the length at byte 30 says version 2 (23)
  or version 3 (54 or 55). Anything else is refused. Version 1 is always 48K.
- The hardware byte chooses the memory layout. 48K: 0 and 1 (and 3 in version 3). 128K: 3 and 4 in version 2, 4 to 6 in version 3, and
  7, 8, 9, 12 and 13 in both. 256K Scorpion: 10. Unknown values are read as 48K. The "modify hardware" bit, the joystick, keyboard and Multiface
  / MGT / Disciple fields are not used.
- RAM pages go to the 128K bank numbers the table above gives (a 48K file keeps its three pages under banks 5, 2 and 0). The
  last `OUT` to #7FFD, #FFFD and, for a 55-byte header on a +2A, +3 or Scorpion, #1FFD are written through the model's port decoder;
  on other models a stored #1FFD is not applied.
- The AY registers are loaded when the hardware is a 128K family or bit 2 of byte 37 is set. The register selected by the last `OUT`
  to #FFFD is restored.
- A version 3 file's T-state counters set the position inside the frame (counted from the interrupt, as libspectrum and Fuse
  count them).
- A snapshot with a SamRam hardware byte, or one that carries a **ROM block** (a block for a ROM page: a custom ROM), is refused
  before anything is written: the reason says which. Corrupt or truncated files are refused with a warning in the log.

**Saving.** Always version 3, written from the machine's 128K view (see the [README](README.md#how-a-snapshot-is-saved)), with a
55-byte additional header when the model has #1FFD (+2A, +3, Scorpion) and 54 otherwise. The hardware byte follows the machine the
snapshot names: 48K 0, 128K 4, +2 12, +2A 13, +3 7, Pentagon 9, Scorpion 10 (a Sprinter in a ZX mode is named by its mode: P128.ZX
a Pentagon, SC256.ZX a Scorpion, the others a 128K). A 48K machine, a 48K mode, and a 128K-family machine locked with bank 0 on top
and the normal screen are written as 48K (three pages); the others keep all banks. The AY registers are written on machines that
have an AY; for a 48K program on such a machine bit 2 of byte 37 marks them as in use. The format has no Pentagon 512 / 1024: they
are refused with the way out (save as .szx).

---

## References

- [World of Spectrum FAQ: Z80 file format](https://worldofspectrum.org/faq/reference/z80format.htm) (the text of this page follows it)
- [Sinclair Wiki: Z80 format](https://sinclair.wiki.zxnet.co.uk/wiki/Z80_format)
- [libspectrum: z80.c](https://github.com/speccytools/libspectrum/blob/master/z80.c), the Fuse emulator's reader and writer
- [zx-evo-docs: z80.txt](https://github.com/tslabs/zx-evo-docs/blob/main/Formats/z80.txt) (TS-Labs' notes on the format)
- [Z80 file format notes by zasm](https://k1.spdns.de/Develop/Projects/zasm/Info/z80format.htm) (a mirror of the specification with the extensions for other machines)
