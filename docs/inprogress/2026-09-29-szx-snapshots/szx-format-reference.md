# SZX (zx-state) snapshot format, v1.5: byte-level reference

- **Date:** 2026-09-29 (all spec pages fetched that day).
- **Purpose:** the byte-level reference for the SZX reader and writer; the design is [design.md](design.md).
- **Local emulator paths** are relative to the local emulator source collection (e.g. `ZXMAK2/src/...`).

## 0. Sources and citation keys

The official spec now lives on a Sphinx site. The old `.shtml` URLs redirect with HTTP 302 to `.html` (checked 2026-09-29: `intro.shtml` -> `intro.html`). Every page was fetched directly on 2026-09-29, so the Wayback Machine was not needed. The site footer reads "Copyright 2001-2025, Jonathan Needle ... Revision c4287de" and the title reads "The zx-state File Format 1.5".

| Key | URL |
|---|---|
| [INTRO] | https://www.spectaculator.com/docs/zx-state/intro.html |
| [VER] | https://www.spectaculator.com/docs/zx-state/version.html |
| [BASIC] | https://www.spectaculator.com/docs/zx-state/basic_info.html |
| [HDR] | https://www.spectaculator.com/docs/zx-state/header.html |
| [BLK] | https://www.spectaculator.com/docs/zx-state/block.html |
| [TYPES] | https://www.spectaculator.com/docs/zx-state/block_types.html |
| [p:NAME] | https://www.spectaculator.com/docs/zx-state/NAME.html (per-block page, e.g. [p:z80regs] = .../z80regs.html) |
| [LSZ] | https://raw.githubusercontent.com/speccytools/libspectrum/master/szx.c (libspectrum, used by Fuse) |
| [LZL] | https://raw.githubusercontent.com/speccytools/libspectrum/master/zlib.c |
| [WIKI] | https://sinclair.wiki.zxnet.co.uk/wiki/ZX-State_format (the URL `.../wiki/SZX_format` does not exist; that page is empty) |
| [WMEM] | https://sinclair.wiki.zxnet.co.uk/wiki/Memory_paging |
| [WTIMEX] | https://sinclair.wiki.zxnet.co.uk/wiki/Timex_2000_series |
| [WSE] | https://sinclair.wiki.zxnet.co.uk/wiki/ZX_Spectrum_SE |
| [F-P1024] | https://raw.githubusercontent.com/speccytools/fuse/master/machines/pentagon1024.c |
| [F-P512] | https://raw.githubusercontent.com/speccytools/fuse/master/machines/pentagon512.c |
| [F-SCORP] | https://raw.githubusercontent.com/speccytools/fuse/master/machines/scorpion.c |
| [F-PERIPH] | https://raw.githubusercontent.com/speccytools/fuse/master/machines/machines_periph.c |
| [F-SCLD] | https://raw.githubusercontent.com/speccytools/fuse/master/peripherals/scld.c |
| [ZERO] | local emulator source collection: `Zero-Emulator/Ziggy/Peripherals/SZXFile.cs` (upstream: https://github.com/ArjunNair/Zero-Emulator) |
| [ZXSP] | local emulator source collection: `zxsp/Source/Uni/Files/file_szx.info` |
| [ZXMAK2] | local emulator source collection: `ZXMAK2/src/ZXMAK2.Engine/Serializers/SnapshotSerializers/SzxSerializer.cs` |
| [BIZ] | local emulator source collection: `BizHawk/src/BizHawk.Emulation.Cores/Computers/SinclairSpectrum/Media/Snapshot/SZX/SZX.Objects.cs` |

Conventions in this document:
- **Offsets are payload-relative.** Offset 0 is the first byte after the 8-byte `ZXSTBLOCK` header. To get the struct offset, add 8.
- "var" marks a variable-length tail. In the C headers it is declared `chData[1]`, so `sizeof(struct)` includes one byte of it. That is why the spec's size formulas contain `- 1`.
- Every multi-byte value is little-endian [BASIC].

---

## 1. General rules [BASIC]

- File extensions are `.szx` and `.zx-state`.
- Structures are byte-packed. There is no padding.
- Every value is little-endian (Intel order). Every string is null-terminated.
- Types: BYTE is u8, CHAR is s8 (printable, used in strings), WORD is u16, DWORD is u32.
- Each block starts with a `ZXSTBLOCK` header. **A parser must skip unknown blocks without reporting an error.**
- A parser must accept current and future files with the same major version. The spec says: "Do not use the chMajorVersion and chMinorVersion values to decide on parsing strategies. Use the ZXSTBLOCK at the beginning of each block to navigate through the file."
  - Practice differs: libspectrum does branch on the version. It uses `version = major<<8 | minor` to size KEYB (4 bytes before 1.1, otherwise 5) and to decode the tail of Z80R [LSZ].
- Block order is generally not significant, so a parser must not depend on it. The one guarantee is that **ZXSTZ80REGS and ZXSTSPECREGS come before the other hardware-state blocks.**
- If the emulator has a piece of hardware enabled that no block in the file describes, the spec suggests disabling that hardware.
- Header layout [HDR]: the file header comes first, then an optional CRTR block, then Z80R, then SPCR, then zero or more further blocks.

### 1.1 Compression [p:rampage] [LSZ] [LZL]
- Payloads use the "Zlib compression library". libspectrum compresses with zlib `compress2(..., Z_BEST_COMPRESSION)`, which produces an RFC 1950 zlib-wrapped stream, and reads with `inflateInit` [LZL].
- Whether a payload is compressed is decided per block by a flag bit. Each block defines its own constant, for example `ZXSTRF_COMPRESSED`, `ZXSTBETAF_COMPRESSED` or `ZXSTMF_COMPRESSED`. The individual block sections below list them.
- On write, libspectrum sets the compressed flag only if the compressed output is smaller than the input, unless the caller passes `ALWAYS_COMPRESS` [LSZ `compress_data`]. A reader must therefore handle both forms.
- The size of the compressed data is never stored for RAM, ROM or other fixed-size tail payloads. It is `dwSize - fixed_part_size`, as shown by the spec formulas, for example `compressedSize = blk.dwSize - (sizeof(ZXSTRAMPAGE) - sizeof(ZXSTBLOCK) - 1)`, which equals `dwSize - 3` for RAMP.

---

## 2. File header `ZXSTHEADER` (8 bytes) [HDR]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwMagic | Bytes `'Z','X','S','T'` (hex `5A 58 53 54`). ZXMAK2 compares it as the LE u32 `0x5453585A` [ZXMAK2]. |
| 4 | BYTE | chMajorVersion | 1 |
| 5 | BYTE | chMinorVersion | The page still says "Currently 4" (stale text) even though the spec is 1.5 [HDR] [VER]. libspectrum writes 1.5 (`SZX_VERSION_MINOR = 5`) [LSZ]. Zero writes and supports 1.4 [ZERO]. ZXMAK2 writes 1.4 [ZXMAK2]. |
| 6 | BYTE | chMachineId | See 2.1 |
| 7 | BYTE | chFlags | See 2.2 |

libspectrum reads the version as `(major<<8)|minor` [LSZ].

### 2.1 Machine IDs [HDR]

| Value | Constant | Model (spec wording) | Since |
|---|---|---|---|
| 0 | ZXSTMID_16K | 16k ZX Spectrum | 1.0 |
| 1 | ZXSTMID_48K | 48k ZX Spectrum or ZX Spectrum+ | 1.0 |
| 2 | ZXSTMID_128K | ZX Spectrum 128 | 1.0 |
| 3 | ZXSTMID_PLUS2 | ZX Spectrum +2 | 1.0 |
| 4 | ZXSTMID_PLUS2A | ZX Spectrum +2A/+2B | 1.0 |
| 5 | ZXSTMID_PLUS3 | ZX Spectrum +3 | 1.0 |
| 6 | ZXSTMID_PLUS3E | ZX Spectrum +3e | UNCONFIRMED (not stated; probably 1.0 or 1.3) |
| 7 | ZXSTMID_PENTAGON128 | Pentagon 128 | 1.2 or earlier (UNCONFIRMED exact) |
| 8 | ZXSTMID_TC2048 | Timex Sinclair TC2048 | 1.2 (SCLD added for TC2048/TC2068 in 1.2 [VER]; ID introduction version UNCONFIRMED) |
| 9 | ZXSTMID_TC2068 | Timex Sinclair TC2068 | 1.2 (same caveat) |
| 10 | ZXSTMID_SCORPION | Scorpion ZS-256 | UNCONFIRMED (Scorpion info was added to SPCR/AY/ROM/RAMP in 1.3 [VER]) |
| 11 | ZXSTMID_SE | ZX Spectrum SE | 1.3 [VER] |
| 12 | ZXSTMID_TS2068 | Timex Sinclair TS2068 | 1.3 [VER] |
| 13 | ZXSTMID_PENTAGON512 | Pentagon 512 | 1.3 [VER] |
| 14 | ZXSTMID_PENTAGON1024 | Pentagon 1024 (the page's table has the typo `ZXSTMID_PENTAGON1204`) | 1.3 [VER] |
| 15 | ZXSTMID_NTSC48K | 48k ZX Spectrum (NTSC) | 1.4 [VER][HDR] |
| 16 | ZXSTMID_128KE | ZX Spectrum 128Ke | 1.4 [VER][HDR] |

Cross-checks:
- libspectrum, BizHawk and ZXMAK2 use the same 0..16 values [LSZ] [BIZ] [ZXMAK2]. libspectrum rejects any other ID as "unknown machine type" [LSZ].
- **Zero-Emulator bug:** its `ZXTYPE` enum omits `ZXSTMID_NTSC48K`, which gives `ZXSTMID_128KE` the value **15** instead of 16 [ZERO]. Do not copy that enum.
- ZXMAK2 has an internal `ZXSTMID_CUSTOM = 255`. Before writing, it falls back to PENTAGON128 or 48K where it can [ZXMAK2]. 255 is not a spec value.

### 2.2 Header flags [HDR]

| Bit value | Constant | Meaning | Since |
|---|---|---|---|
| 1 | ZXSTMF_ALTERNATETIMINGS | Set means alternate timings ("one cycle later than normal timings"). Clear means standard timings. The spec says it applies **only to 16K, 48K and 128K**. | 1.4 |

libspectrum also honors this flag for `ZXSTMID_NTSC48K` [LSZ `libspectrum_szx_read`]. That goes slightly beyond the spec's list. ZXMAK2 sets this flag by default on write [ZXMAK2].

---

## 3. Block header `ZXSTBLOCK` (8 bytes) [BLK]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwId | Four ASCII bytes in file order, for example `'C','R','T','R'`. Short IDs are padded with NUL, for example `'A','Y',0,0`. |
| 4 | DWORD | dwSize | Size of the data that follows. It **excludes** the 8-byte header: `dwSize = sizeof(ZXSTxxx) - sizeof(ZXSTBLOCK)`. |

libspectrum compares IDs with `memcmp(id, "XXXX", 4)`. An unknown ID is skipped by `dwSize`, and a block whose length runs past the end of the file is an error [LSZ `read_chunk`].

---

## 4. Summary table of all blocks

| Struct | ID bytes | Fixed payload | Var tail | Since | Official? |
|---|---|---|---|---|---|
| ZXSTCREATOR | `CRTR` | 36 | creator data | 1.1 | yes |
| ZXSTZ80REGS | `Z80R` | 37 | - | 1.0 | yes |
| ZXSTSPECREGS | `SPCR` | 8 | - | 1.0 | yes |
| ZXSTRAMPAGE | `RAMP` | 3 | 16 KB page | 1.0 | yes |
| ZXSTAYBLOCK | `AY\0\0` | 18 | - | 1.0 | yes |
| ZXSTKEYBOARD | `KEYB` | 5 (4 in 1.0) | - | 1.0 | yes |
| ZXSTJOYSTICK | `JOY\0` | 6 | - | 1.1 | yes |
| ZXSTMOUSE | `AMXM` | 7 | - | 1.0 | yes |
| ZXSTBETA128 | `B128` | 10 | custom TR-DOS ROM | 1.2 | yes |
| ZXSTBETADISK | `BDSK` | 7 | file name or image | 1.2 | yes |
| ZXSTPLUS3 | `+3\0\0` | 2 | - | 1.0 | yes |
| ZXSTDSKFILE | `DSK\0` | 7 | file name | 1.0 | yes |
| ZXSTTAPE | `TAPE` | 28 | file name or tape image | 1.0 | yes |
| ZXSTGS | `GS\0\0` | 46 | custom GS ROM | 1.2 | yes |
| ZXSTGSRAMPAGE | `GSRP` | 3 | 32 KB page | 1.2 | yes |
| ZXSTCOVOX | `COVX` | 4 | - | 1.2 | yes |
| ZXSTSPECDRUM | `DRUM` | 1 | - | 1.0 | yes |
| ZXSTSCLDREGS | `SCLD` | 2 | - | 1.2 | yes |
| ZXSTROM | `ROM\0` | 6 | ROM image | 1.0 | yes |
| ZXSTIF1 | `IF1\0` | 40 | custom IF1 ROM | 1.0 | yes |
| ZXSTMCART | `MDRV` | 12 | file name | 1.0 | yes |
| ZXSTIF2ROM | `IF2R` | 4 | zlib 16 KB cart | 1.0 | yes |
| ZXSTMULTIFACE | `MFCE` | 2 | 8 KB or 16 KB RAM | 1.0 | yes |
| ZXSTATASP | `ZXAT` | 8 | - | 1.3 | yes |
| ZXSTATARAM | `ATRP` | 3 | 16 KB page | 1.3 | yes |
| ZXSTCF | `ZXCF` | 4 | - | 1.3 | yes |
| ZXSTCFRAM | `CFRP` | 3 | 16 KB page | 1.3 | yes |
| ZXSTSIDE | `SIDE` | 0 | - | 1.3 | yes |
| ZXSTPLUSD | `PLSD` | 19 | RAM + optional ROM | 1.3 | yes |
| ZXSTPLUSDDISK | `PDSK` | 7 | file name or image | 1.3 | yes |
| ZXSTOPUS | `OPUS` | 23 | RAM + optional ROM | 1.4 | yes |
| ZXSTOPUSDISK | `ODSK` | 7 | file name or image | 1.4 | yes |
| ZXSTUSPEECH | `USPE` | 1 | - | 1.0 | yes |
| ZXSTZXPRINTER | `ZXPR` | 2 | - | 1.0 | yes |
| ZXSTDOCK | `DOCK` | 3 | 8 KB page | 1.3 | yes |
| ZXSTLEC | `LEC\0` | 4 | - | 1.5 | yes (1.5) |
| ZXSTLECRAMPAGE | `LCRP` | 3 | 32 KB page | 1.5 | yes (1.5) |
| ZXSTSPECTRANET | `SNET` | official 1.5: 56 + tails; Fuse: 54 | see section 6.3 | 1.5 | yes, but **two incompatible layouts** |
| ZXSTSPECTRANETFLASHPAGE | `SNEF` | 5 | flash | - | unofficial (Fuse) |
| ZXSTSPECTRANETRAMPAGE | `SNER` | 5 | RAM | - | unofficial (Fuse) |
| ZXSTPALETTE | `PLTT` | 66 (67 with ULAplus 1.1a reg) | - | - | unofficial |
| ZXSTDIVIDE | `DIDE` | 4 | EPROM | - | unofficial |
| ZXSTDIVIDERAMPAGE | `DIRP` | 3 | 8 KB page | - | unofficial |
| ZXSTDIVMMC | `DMMC` | 4 | EPROM | - | unofficial |
| ZXSTDIVMMCRAMPAGE | `DMRP` | 3 | 8 KB page | - | unofficial |
| ZXSTZXMMC | `ZMMC` | 0 | - | - | unofficial |

The official index [TYPES] lists exactly these 38 structs: ATASP, ATARAM, AYBLOCK, BETA128, BETADISK, CF, CFRAM, COVOX, CREATOR, DOCK, DSKFILE, GS, GSRAMPAGE, IF1, IF2ROM, JOYSTICK, KEYBOARD, LEC, LECRAMPAGE, MCART, MOUSE, MULTIFACE, OPUS, OPUSDISK, PLUS3, PLUSD, PLUSDDISK, RAMPAGE, ROM, SCLDREGS, SIDE, TAPE, SPECREGS, SPECTRANET, USPEECH, SPECDRUM, Z80REGS and ZXPRINTER. "ZXSTIF2" does not exist; the Interface II struct is ZXSTIF2ROM. PLTT, DIDE, DIRP, DMMC, DMRP, ZMMC, SNEF and SNER are **not** in the official spec.

Byte-size cross-check: libspectrum's length checks give Z80R == 37, SPCR == 8, AY == 18, SCLD == 2, COVX == 4, JOY == 6, AMXM == 7, ZXAT == 8, ZXCF == 4, USPE == 1, DRUM == 1, SIDE == 0, ZMMC == 0, KEYB == 4 or 5 by version, CRTR >= 36, B128 >= 10, IF1 >= 40, OPUS >= 23, PLSD >= 19, ROM >= 6, RAMP-style >= 3, DIDE/DMMC >= 4, SNET >= 54, SNEF/SNER >= 5, PLTT >= 66 [LSZ].

---

## 5. Official blocks in detail

### 5.1 ZXSTCREATOR `CRTR`: optional, since 1.1 [p:creator]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | CHAR[32] | szCreator | Emulator or utility name, null-terminated |
| 32 | WORD | chMajorVersion | Named `ch...` but the type is WORD |
| 34 | WORD | chMinorVersion | WORD |
| 36 | BYTE[var] | chData | Creator-specific data |

libspectrum quirk: libspectrum versions up to 0.5.0 wrote Z80R with A and F swapped, and likewise A' and F'. The current reader looks for the string `"libspectrum: X.Y.Z"` in the CRTR custom data. If it finds version 0.x.y with (y < 5) or (0.5.0), it swaps A and F back [LSZ `read_crtr_chunk`, `swap_af`] [WIKI].

### 5.2 ZXSTZ80REGS `Z80R`: required, 37 bytes, since 1.0 [p:z80regs]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | AF | Little-endian, so F is at offset 0 and A at offset 1 [LSZ writer] |
| 2 | WORD | BC | |
| 4 | WORD | DE | |
| 6 | WORD | HL | |
| 8 | WORD | AF1 | AF' (F' at 8, A' at 9) |
| 10 | WORD | BC1 | |
| 12 | WORD | DE1 | |
| 14 | WORD | HL1 | |
| 16 | WORD | IX | |
| 18 | WORD | IY | |
| 20 | WORD | SP | |
| 22 | WORD | PC | |
| 24 | BYTE | I | |
| 25 | BYTE | R | |
| 26 | BYTE | IFF1 | Guaranteed to be 0 or 1 |
| 27 | BYTE | IFF2 | Guaranteed to be 0 or 1 |
| 28 | BYTE | IM | 0, 1 or 2 |
| 29 | DWORD | dwCyclesStart | T-state count within the frame when the snapshot was taken. It counts up from 0 to the model's T-states per frame. |
| 33 | BYTE | chHoldIntReqCycles | T-states left on restart during which an interrupt can still be accepted. The ULA holds INT low for up to 48 T-states, depending on the model. The AMX mouse can also assert INT, so this can be non-zero away from the start of the frame. |
| 34 | BYTE | chFlags | See the flag table below |
| 35 | WORD | wMemPtr | MEMPTR (WZ). Write 0 if not supported. |

Flags (chFlags):

| Value | Constant | Meaning |
|---|---|---|
| 1 | ZXSTZF_SUPPRESS_INTS (named ZXSTZF_EILAST before 1.5) | Interrupts are currently not accepted: the last instruction was EI, or an invalid $DD/$FD prefix or a run of them. The rename in 1.5 is binary-compatible. |
| 2 | ZXSTZF_HALTED | The last instruction executed was HALT; the CPU executes NOPs until the next interrupt. Mutually exclusive with SUPPRESS_INTS. |
| 4 | ZXSTZF_FSET | The last instruction set F. This is needed for the SCF/CCF undocumented-flag (Q register) behavior. Added in 1.5 (credited to Patrik Rak). |

Version history of the byte range 33..36 [p:z80regs] [VER] [LSZ]:
- **1.0:** the spec says "three reserved BYTEs occupying this space" for the 1.1 fields, with chHoldIntReqCycles at offset 33. libspectrum skips all four bytes (33..36) when the version is below 1.1.
- **1.1..1.3:** 33 chHoldIntReqCycles, 34 chFlags, 35 chBitReg (hidden register for BIT n,(HL) flags 5/3), 36 chReserved.
- **1.4+:** bytes 35..36 hold `wMemPtr`, replacing chBitReg and chReserved. The block size is unchanged. libspectrum reads a MEMPTR only when the version is at least 0x0104; otherwise it skips the 2 bytes.
- 1.3 added the note that EILAST must be set when the Z80 will not accept an interrupt before the next instruction.
- 1.5 added FSET and the EILAST to SUPPRESS_INTS rename.

The libspectrum writer computes `chHoldIntReqCycles = 48 - tstates` if tstates < 48, else 0 [LSZ `write_z80r_chunk`].

### 5.3 ZXSTSPECREGS `SPCR`: required, 8 bytes, since 1.0 [p:specregs]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chBorder | Border color, 0..7 |
| 1 | BYTE | ch7ffd | Last value written to port $7FFD. Write 0 on 16K and 48K. |
| 2 | BYTE | ch1ffd / chEff7 (union) | Last $1FFD value on +2A/+3 and ZS Scorpion. On Pentagon 1024 it holds the last $EFF7 value (since 1.3). Write 0 on other models. |
| 3 | BYTE | chFe | Last value written to port $FE. Only bits 3 and 4 (MIC, EAR) are guaranteed valid; take the border from chBorder. Added in 1.1 in place of a reserved byte. |
| 4 | BYTE[4] | chReserved | 0 |

Implementation notes:
- libspectrum reads chBorder & 7. It merges chFe & 0xF8 into its ULA-out value only for version 1.1 and later.
- libspectrum loads offset 2 only for machines with +3, Scorpion or Pentagon 1024 memory. On write it stores the full `out_ula` byte in chFe [LSZ].
- 1.3 updated this block for the Pentagon 1024, Timex and Scorpion [VER].
- The last $1FFD and $7FFD values are also what Multiface emulation uses [p:multiface].

#### 5.3.1 Paging semantics of the stored ports (not defined by SZX; from hardware references)

**128K / +2 / Pentagon 128 $7FFD** [WMEM]:
- bits 0-2: RAM page at $C000
- bit 3: screen (page 5 or 7)
- bit 4: ROM select
- bit 5: paging lock until reset

**+2A/+3 $1FFD** [WMEM]:
- bit 0: special (all-RAM) mode
- In normal mode (bit 0 = 0), bit 2 is the high bit of the ROM number: ROM = (1FFD.bit2 << 1) | 7FFD.bit4, selecting ROM 0..3.
- In special mode (bit 0 = 1), bits 1-2 select a configuration:

  | Bits 2-1 | Pages at $0000 / $4000 / $8000 / $C000 |
  |---|---|
  | 00 | 0 / 1 / 2 / 3 |
  | 01 | 4 / 5 / 6 / 7 |
  | 10 | 4 / 5 / 6 / 3 |
  | 11 | 4 / 7 / 6 / 3 |

- bit 3: disk motor
- bit 4: printer strobe

Fuse decodes $7FFD on +2A/+3 with mask 0xC002 / value 0x4000, and $1FFD with mask 0xF002 / value 0x1000 [F-PERIPH].

**Pentagon 512** (Fuse model) [F-P512]:
- Page = (7FFD & 7) + ((7FFD & 0xC0) >> 3), so bits 6-7 are page bits 3-4, giving pages 0..31.
- ROM: TR-DOS ROM when Beta is active and 7FFD.bit4 = 0.

**Pentagon 1024 $7FFD + $EFF7** (Fuse model; the comment calls it a "post-1996 Pentagon (a 1024k v2.2 1024SL?)") [F-P1024] [F-PERIPH]:
- Port decode: 7FFD with mask 0xC002 / value 0x4000; EFF7 with mask 0xF008 / value 0xE000.
- If EFF7.bit2 = 0: page = (7FFD & 7) + ((7FFD & 0xC0) >> 3) + (7FFD & 0x20). So 7FFD bits 6, 7 and 5 supply page bits 3, 4 and 5, giving pages 0..63.
- If EFF7.bit2 = 1 (128K compatibility): only 7FFD bits 0-2 select the page, and 7FFD.bit5 becomes the lock bit.
- EFF7.bit3: RAM page 0 mapped at $0000.
- EFF7.bit0: 16-color (hardware multicolor) display mode, in Fuse.
- ROM: TR-DOS ROM when Beta is active and 7FFD.bit4 = 0, else 7FFD.bit4 selects between the two 128 ROMs.

**Scorpion ZS-256 $1FFD** (Fuse model) [F-SCORP]:
- bit 0: RAM page 0 at $0000
- bit 1: ROM 2 (service ROM) at $0000
- bit 4: RAM page bit 3, so page = ((1FFD & 0x10) >> 1) | (7FFD & 7), giving pages 0..15
- Fuse uses the +3-style memory port decode for Scorpion.
- The meaning of other $1FFD bits (for example printer) is UNCONFIRMED here.

**Timex (TC2048/TC2068/TS2068/SE)** use the SCLD block (section 5.18), not SPCR offset 2. The SE also uses $7FFD for the HOME bank [WSE].

### 5.4 ZXSTRAMPAGE `RAMP`: required (one per page), since 1.0 [p:rampage]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTRF_COMPRESSED = 1` means the data is zlib-compressed |
| 2 | BYTE | chPageNo | Page number |
| 3 | BYTE[var] | chData | 16384 bytes uncompressed. Compressed size = dwSize - 3. |

Pages saved per model [p:rampage]:

| Model | Pages |
|---|---|
| 16K | 5 only (the $4000-$7FFF page) |
| 48K and Timex TS/TC | 5 ($4000), 2 ($8000), 0 ($C000) |
| 128K family and Pentagon 128 | 0..7 |
| Pentagon 512 | 0..31 |
| Pentagon 1024 | 0..63 |
| ZS Scorpion 256 | 0..15 |

Pages may appear in any order. The Pentagon 512/1024 page counts were added in 1.3, and the Timex and Scorpion counts were added in 1.3 [VER].

libspectrum notes:
- It accepts page numbers 0..63 [LSZ `read_ramp_chunk`].
- For the **Spectrum SE** it writes pages 0..7 **plus page 8** (`SE_MEMORY` capability) [LSZ `write_ram_pages`]. The spec's page list does not mention the SE; the spec stores 128 KB of SE RAM in DOCK blocks (section 5.34). Whether Spectaculator writes RAMP page 8 for the SE is UNCONFIRMED.
- Page counts for +3e and 128Ke are not listed separately. libspectrum treats both as 128K memory (pages 0..7) [LSZ].

### 5.5 ZXSTAYBLOCK `AY\0\0`: 18 bytes, since 1.0 [p:ay]

Present on all 128K models, Pentagons, Scorpions and Timex machines. On 16K/48K it appears only when a Fuller Box or Melodik is enabled.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chFlags | `ZXSTAYF_FULLERBOX = 1`, `ZXSTAYF_128AY = 2` (Melodik: an AY on the 128 ports for older machines). Set 0 on machines with a built-in AY. |
| 1 | BYTE | chCurrentRegister | 0..15 |
| 2 | BYTE[16] | chAyRegs | Unused bits are 0 |

There is **no TurboSound (2xAY) flag or second register set** in the spec. A TurboSound extension is UNCONFIRMED and does not appear in libspectrum [LSZ] or [WIKI]. 1.3 updated this block for Timex and Scorpion [VER].

### 5.6 ZXSTKEYBOARD `KEYB`: 5 bytes (4 in v1.0), since 1.0 [p:keyboard]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTKF_ISSUE2 = 1`: Issue 2 keyboard. Only valid on 16K/48K; 0 on other models. |
| 4 | BYTE | chKeyboardJoystick | Added in 1.1 |

chKeyboardJoystick values: 0 KEMPSTON, 1 FULLER, 2 CURSOR (AGF/Protek), 3 SINCLAIR1 (IF2 port 1 or +2A/+3 joystick 1), 4 SINCLAIR2, 5 SPECTRUMPLUS (cursor keys of the Spectrum+/128/+2/+2A/+3), 6 TIMEX1, 7 TIMEX2, 8 NONE. TIMEX1/2 and NONE were added in 1.3 [VER]. libspectrum's enum is `ZXJT_*` with the same values [LSZ].

### 5.7 ZXSTJOYSTICK `JOY\0`: 6 bytes, since 1.1 [p:joystick]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTJOYF_ALWAYSPORT31 = 1`, **deprecated** (an emulator option: read of port 31 returns 0 when Kempston is off) |
| 4 | BYTE | chTypePlayer1 | See values below |
| 5 | BYTE | chTypePlayer2 | See values below |

Types: 0 KEMPSTON, 1 FULLER, 2 CURSOR, 3 SINCLAIR1, 4 SINCLAIR2, 5 **COMCON** (programmable, added in 1.3), 6 TIMEX1, 7 TIMEX2 (added in 1.3), 8 DISABLED. Value 5 differs from KEYB's value 5 (SPECTRUMPLUS).

### 5.8 ZXSTMOUSE `AMXM`: 7 bytes, since 1.0 [p:mouse]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chType | 0 NONE, 1 AMX, 2 KEMPSTON |
| 1 | BYTE[3] | chCtrlA | Z80 PIO CTRLA registers (AMX) |
| 4 | BYTE[3] | chCtrlB | Z80 PIO CTRLB registers (AMX) |

### 5.9 ZXSTBETA128 `B128`: since 1.2 [p:beta128]

Covers the Beta 128 interface and the built-in clones in Pentagon and Scorpion.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | See the flag table below |
| 4 | BYTE | chNumDrives | 1..4 |
| 5 | BYTE | chSysReg | Last write to the system register, port $FF |
| 6 | BYTE | chTrackReg | WD179x track register, port $3F |
| 7 | BYTE | chSectorReg | Sector register, port $5F |
| 8 | BYTE | chDataReg | Data register, port $7F |
| 9 | BYTE | chStatusReg | Status register, port $1F |
| 10 | BYTE[var] | chRomData | Custom TR-DOS ROM, 16384 bytes uncompressed. Compressed size = dwSize - 10. |

Flags:

| Value | Constant | Meaning |
|---|---|---|
| 1 | ZXSTBETAF_CONNECTED | Interface is connected. Always set on Pentagon and Scorpion. |
| 2 | ZXSTBETAF_CUSTOMROM | A custom ROM is present at chRomData. The default ROM is TR-DOS 5.03. |
| 4 | ZXSTBETAF_PAGED | The TR-DOS ROM is paged in now |
| 8 | ZXSTBETAF_AUTOBOOT | Auto-boot (48K only) |
| 16 | ZXSTBETAF_SEEKLOWER | The FDC seek direction is toward lower cylinders |
| 32 | ZXSTBETAF_COMPRESSED | The custom ROM is zlib-compressed |

BDSK blocks follow this block.

### 5.10 ZXSTBETADISK `BDSK`: since 1.2 [p:betadisk]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTBDF_EMBEDDED = 1`, `ZXSTBDF_COMPRESSED = 2` (only meaningful with EMBEDDED), `ZXSTBDF_WRITEPROTECT = 4` |
| 4 | BYTE | chDriveNum | 0..3 |
| 5 | BYTE | chCylinder | Head position, 0..86 |
| 6 | BYTE | chDiskType | 0 TRD, 1 SCL, 2 FDI, 3 UDI |
| 7 | union | szFileName / chDiskImage | A file name (not embedded) or an image that may be compressed |

Detect the image format from chDiskType, not from the file extension. libspectrum only skips this block [LSZ].

### 5.11 ZXSTPLUS3 `+3\0\0`: 2 bytes, since 1.0 [p:plus3disk]

The C struct tag is `_tagZXSTPLUS3DISK`.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chNumDrives | 1 or 2 |
| 1 | BYTE | fMotorOn | 0 or 1 |

DSK blocks follow this block. No uPD765 FDC register state is stored.

### 5.12 ZXSTDSKFILE `DSK\0`: since 1.0 [p:dskfile]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTDSKF_COMPRESSED = 1` and `ZXSTDSKF_EMBEDDED = 2` are both "Not implemented": images are always links to external .dsk or .ipf files. `ZXSTDSKF_SIDEB = 4`: side B is active for a double-sided image in a single-sided drive (added in 1.4). |
| 2 | BYTE | chDriveNum | 0 = A:, 1 = B: |
| 3 | DWORD | dwUncompressedSize | **Length of the file name** at chData |
| 7 | BYTE[var] | chData | .dsk or .ipf file name |

### 5.13 ZXSTTAPE `TAPE`: since 1.0 [p:cassette_recorder]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wCurrentBlockNo | Tape head position as a 0-based block number. Spectaculator splits .wav and .voc files into 5-second chunks. |
| 2 | WORD | wFlags | `ZXSTTP_EMBEDDED = 1`, `ZXSTTP_COMPRESSED = 2` |
| 4 | DWORD | dwUncompressedSize | Only valid if embedded |
| 8 | DWORD | dwCompressedSize | Size of chData; for a linked file, the length of the file name |
| 12 | CHAR[16] | szFileExtension | Extension of the embedded file. `"tapw"` means a Warajevo .tap. |
| 28 | BYTE[var] | chData | Linked file name, or the embedded tape (zlib-compressed if COMPRESSED is set) |

The block is omitted when the recorder is empty. libspectrum only skips it [LSZ].

### 5.14 ZXSTGS `GS\0\0`: since 1.2 [p:gs]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chModel | 0 = GS 128 KB, 1 = GS 512 KB |
| 1 | BYTE | chUpperPage | 32 KB page mapped at $8000-$FFFF (0 = ROM) |
| 2 | BYTE[4] | chGsChanVol | 6-bit volumes; bits 6-7 are 0 |
| 6 | BYTE[4] | chGsChanOut | Channel output levels |
| 10 | BYTE | chFlags | See the flag table below |
| 11 | WORD x12 | AF, BC, DE, HL, AF1, BC1, DE1, HL1, IX, IY, SP, PC | Offsets 11..34 |
| 35 | BYTE | I | The spec text says "contents of the R register" here, a copy-paste typo |
| 36 | BYTE | R | |
| 37 | BYTE | IFF1 | |
| 38 | BYTE | IFF2 | |
| 39 | BYTE | IM | |
| 40 | DWORD | dwCyclesStart | Counted per 50 Hz frame |
| 44 | BYTE | chHoldIntReqCycles | |
| 45 | BYTE | chBitReg | Hidden register. There is no MEMPTR field here. |
| 46 | BYTE[var] | chRomData | Custom GS ROM, 32768 bytes uncompressed. Compressed size = dwSize - 46. |

Flags:

| Value | Constant | Meaning |
|---|---|---|
| 1 | ZXSTZF_EILAST | Last instruction was EI |
| 2 | ZXSTZF_HALTED | Last instruction was HALT |
| 64 | ZXSTGSF_CUSTOMROM | Custom ROM present. The default ROM is 1.04. |
| 128 | ZXSTGSF_COMPRESSED | The custom ROM is zlib-compressed |

The fixed payload is 46 bytes, computed from the struct. It is not stated in the spec and libspectrum does not parse GS (it skips the block) [LSZ]. GSRP blocks follow this block.

### 5.15 ZXSTGSRAMPAGE `GSRP`: since 1.2 [p:gsrampage]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTGSRF_COMPRESSED = 1` |
| 2 | BYTE | chPageNo | 32 KB page: 0..3 on GS128, 0..14 on GS512 |
| 3 | BYTE[var] | chData | 32768 bytes uncompressed. Compressed size = dwSize - 3. |

### 5.16 ZXSTCOVOX `COVX`: 4 bytes, since 1.2 [p:covox]

| Off | Type | Name |
|---|---|---|
| 0 | BYTE | chCurrentVolume (DAC level) |
| 1 | BYTE[3] | chReserved (0) |

The block does not say which port the Covox uses. Fuse emulates Pentagon ($FB) and Scorpion ($DD) variants (`PERIPH_TYPE_COVOX_FB` / `COVOX_DD`) [F-P512] [F-SCORP]; SZX does not distinguish them.

### 5.17 ZXSTSPECDRUM `DRUM`: 1 byte, since 1.0 [p:specdrum]

Offset 0 holds `chCurrentVolume`, the signed DAC level from -128 to +127. If the block is absent, SpecDrum emulation should be disabled.

### 5.18 ZXSTSCLDREGS `SCLD`: 2 bytes, since 1.2 [p:scld]

The ID constant is `ZXSTBID_TIMEXREGS`.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chF4 | Last value of port $F4 (horizontal MMU page-in mask). libspectrum calls it `scld_hsr`. |
| 1 | BYTE | chFf | Last value of port $FF (screen mode, interrupt disable, hi-res colors, MMU bank select). libspectrum calls it `scld_dec`. |

Semantics [WTIMEX] [WSE]:
- **$FF** bits 0-2 set the screen mode: 000 screen 0, 001 screen 1 ($6000), 010 hi-color, 110 hi-res.
- **$FF** bits 3-5 set the hi-res ink/paper pair.
- **$FF** bit 6 disables the frame interrupt.
- **$FF** bit 7 selects the MMU source: 0 = DOCK, 1 = EX-ROM.
- **$F4** bit n = 1 pages 8 KB chunk n ($0000 + n*$2000) from DOCK or EX-ROM, chosen by $FF bit 7, instead of HOME.
- On the SE, the odd 128K pages at $C000 take priority over DOCK/EX, and $F4 bits 2-3 also affect an odd page selected at $C000 [WSE].
- Fuse decodes both ports with mask 0x00FF [F-SCLD].

### 5.19 ZXSTROM `ROM\0`: since 1.0 [p:custom_rom]

Present only when a custom ROM is installed.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTRF_COMPRESSED = 1` |
| 2 | DWORD | dwUncompressedSize | See sizes below |
| 6 | BYTE[var] | chData | ROMs concatenated in ascending order. Compressed size = dwSize - 6. |

Sizes listed by the spec:

| Model | Size |
|---|---|
| 16K/48K | 16384 |
| 128/+2 | 32768 |
| +2A/+3 | 65536 |
| Pentagon 128 | 32768 |
| ZS Scorpion | 65536 |
| TS/TC2048 | 16384 |
| TS2068 | 24576 |
| SE | 32768 |

libspectrum's writer validates a wider table [LSZ `write_rom_chunk`]:
- 48K NTSC: 16 KB
- 128Ke: 32 KB
- TC2068: 24 KB
- +3e: 64 KB
- **Pentagon 512 and 1024: 48 KB (3 ROMs)**

The spec itself gives no size for Pentagon 512/1024, 128Ke, +3e or TC2068 (UNCONFIRMED what Spectaculator uses).

When no ROM block is present, use the standard ROMs: UK ROMs, with v4.0 for +2A/+3. For clones, use the model's standard ROM. The Timex and Scorpion sizes were added in 1.3 [VER].

### 5.20 ZXSTIF1 `IF1\0`: since 1.0 [p:interface1]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTIF1F_ENABLED = 1`, `ZXSTIF1F_COMPRESSED = 2`, `ZXSTIF1F_PAGED = 4` |
| 2 | BYTE | chNumMicrodrives | 1..8 (never 0) |
| 3 | BYTE[3] | chReserved | 0 |
| 6 | DWORD[8] | dwReserved | 0 |
| 38 | WORD | wRomSize | 8192 or 16384 for a custom ROM; 0 means use the standard v2 ROM |
| 40 | BYTE[var] | chRomData | Compressed size = dwSize - 40 |

The IF1 block precedes the MDRV blocks.

### 5.21 ZXSTMCART `MDRV`: since 1.0 [p:microdrive]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTMDF_COMPRESSED = 1`, `ZXSTMDF_EMBEDDED = 2`, both "Not implemented" (always a link to an external .mdr) |
| 2 | BYTE | chDriveNum | 1..8 |
| 3 | BYTE | fDriveRunning | 0 or 1 |
| 4 | WORD | wDrivePos | Head position within the file |
| 6 | WORD | wPreamble | Preamble bytes left to skip |
| 8 | DWORD | dwUncompressedSize | Length of the file name |
| 12 | BYTE[var] | chData | .mdr file name |

### 5.22 ZXSTIF2ROM `IF2R`: since 1.0 [p:if2rom]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwCartSize | **Compressed** size of chData |
| 4 | BYTE[var] | chData | Always zlib-compressed; 16 KB uncompressed |

The block is present only when a cartridge is loaded.

### 5.23 ZXSTMULTIFACE `MFCE`: since 1.0 [p:multiface]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | ch48kModel | Model used on 16K/48K: 0 = ZXSTMFM_1 (Multiface 1), 1 = ZXSTMFM_128 |
| 1 | BYTE | chFlags | See the flag table below |
| 2 | BYTE[var] | chData | Multiface RAM: 8 KB, or 16 KB in 16KRAMMODE. Compressed size = dwSize - 2. |

Flags:

| Value | Constant | Meaning |
|---|---|---|
| 0x01 | ZXSTMF_PAGEDIN | ROM and RAM are paged in |
| 0x02 | ZXSTMF_COMPRESSED | chData is zlib-compressed |
| 0x04 | ZXSTMF_SOFTWARELOCKOUT | MF128/MF3 only |
| 0x08 | ZXSTMF_REDBUTTONDISABLED | Red button disabled |
| 0x10 | ZXSTMF_DISABLED | MF1 physical disable switch |
| 0x20 | ZXSTMF_16KRAMMODE | 8 KB ROM + 8 KB RAM replaced by 16 KB RAM (for SoftCrack) |

Name clash: `ZXSTMF_*` is also the prefix of the header flag `ZXSTMF_ALTERNATETIMINGS`. The last $1FFD and $7FFD values come from SPCR. The Multiface 3 model is implied by the +2A/+3 machine type.

### 5.24 ZXSTATASP `ZXAT`: 8 bytes, since 1.3 [p:atasp]

Sami Vehmaa's ZXATASP interface, for 16K, 48K, 128, +2, +2A and +3.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTAF_UPLOADJUMPER = 1`, `ZXSTAF_WRITEPROTECT = 2` |
| 2 | BYTE | chPortA | Port $009F |
| 3 | BYTE | chPortB | Port $019F |
| 4 | BYTE | chPortC | Port $029F |
| 5 | BYTE | chControl | Port $039F |
| 6 | BYTE | chNumRamPages | 16 KB pages |
| 7 | BYTE | chActivePage | Page at $0000-$3FFF; 255 = none |

libspectrum names the flags `ZXSTZXATF_UPLOAD` and `ZXSTZXATF_WRITEPROTECT` [LSZ]. ATRP blocks follow. No IDE drive image is referenced; the IDE interface state is not stored.

### 5.25 ZXSTATARAM `ATRP`: since 1.3 [p:ataspram]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTAF_COMPRESSED = 1` (same value as UPLOADJUMPER, but a different block) |
| 2 | BYTE | chPageNo | 0..7 (128 KB) or 0..31 (512 KB) |
| 3 | BYTE[var] | chData | 16384 bytes uncompressed |

### 5.26 ZXSTCF `ZXCF`: 4 bytes, since 1.3 [p:zxcf]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTCF_UPLOADJUMPER = 1` |
| 2 | BYTE | chMemCtrl | Memory control register |
| 3 | BYTE | chNumRamPages | Number of 16 KB pages, normally 32 (512 KB) or 64 (1 MB) |

The spec text labels the ID constant `ZXSTBID_ZXATASP` (copy-paste); the bytes are `'Z','X','C','F'`.

### 5.27 ZXSTCFRAM `CFRP`: since 1.3 [p:zxcfram]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTCRF_COMPRESSED = 1` |
| 2 | BYTE | chPageNo | 0..31 or 0..63 |
| 3 | BYTE[var] | chData | 16384 bytes uncompressed |

### 5.28 ZXSTSIDE `SIDE`: 0 bytes, since 1.3 [p:simple8bitide]

The Simple 8-bit IDE interface (Garry Lancaster's, as used in the original +3e). The block has no payload: its presence means the interface is fitted. libspectrum requires dwSize == 0 [LSZ].

### 5.29 ZXSTPLUSD `PLSD`: since 1.3 [p:plusd]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTPLUSDF_PAGED = 1`, `ZXSTPLUSDF_COMPRESSED = 2` (RAM and custom ROM), `ZXSTPLUSDF_SEEKLOWER = 4` |
| 4 | DWORD | dwcbRam | Stored RAM size, 8192 uncompressed |
| 8 | DWORD | dwcbRom | Stored custom ROM size, 8192 uncompressed |
| 12 | BYTE | chRomType | 0 GDOS (G+DOS 1.A), 1 UNIDOS, 2 CUSTOM |
| 13 | BYTE | chCtrlReg | Port $EF |
| 14 | BYTE | chNumDrives | 1 or 2 |
| 15 | BYTE | chTrackReg | WD1772, port $EB |
| 16 | BYTE | chSectorReg | Port $F3 |
| 17 | BYTE | chDataReg | Port $FB |
| 18 | BYTE | chStatusReg | Port $E3 |
| 19 | BYTE[var] | chRam | RAM, then the custom ROM if chRomType == CUSTOM |

PDSK blocks follow.

### 5.30 ZXSTPLUSDDISK `PDSK`: since 1.3 [p:plusddisk]

The layout matches BDSK:

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTPDDF_EMBEDDED = 1`, `COMPRESSED = 2`, `WRITEPROTECT = 4` |
| 4 | BYTE | chDriveNum | 0..1 |
| 5 | BYTE | chCylinder | 0..86 |
| 6 | BYTE | chDiskType | 0 MGT, 1 IMG, 2 FLOPPY0 (host's first real drive), 3 FLOPPY1 |
| 7 | union | szFileName / chDiskImage | Unused for FLOPPYx |

The version table [VER] names it "ZXSTPLUSDISK"; the block page names it ZXSTPLUSDDISK.

### 5.31 ZXSTOPUS `OPUS`: since 1.4 [p:opus]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | DWORD | dwFlags | `ZXSTOPUSF_PAGED = 1`, `ZXSTOPUSF_COMPRESSED = 2`, `ZXSTOPUSF_SEEKLOWER = 4`, `ZXSTOPUSF_CUSTOMROM = 8` (the text table calls it `ZXSTOPUSRT_CUSTOM`) |
| 4 | DWORD | dwcbRam | Stored RAM size, 2048 uncompressed |
| 8 | DWORD | dwcbRom | Stored custom ROM size, 8192 uncompressed |
| 12 | BYTE | chCtrlRegA | 6821 PIA |
| 13 | BYTE | chPeripheralRegA | |
| 14 | BYTE | chDataDirRegA | |
| 15 | BYTE | chCtrlRegB | |
| 16 | BYTE | chPeripheralRegB | |
| 17 | BYTE | chDataDirRegB | |
| 18 | BYTE | chNumDrives | 1 or 2 |
| 19 | BYTE | chTrackReg | WD1770, address $3001 |
| 20 | BYTE | chSectorReg | $3002 |
| 21 | BYTE | chDataReg | $3003 |
| 22 | BYTE | chStatusReg | $3000 |
| 23 | BYTE[var] | chRam | RAM, then the custom ROM |

ODSK blocks follow.

### 5.32 ZXSTOPUSDISK `ODSK`: since 1.4 [p:opusdisk]

The layout matches PDSK. Flags `ZXSTOPDF_*` are 1, 2 and 4. Types: 0 OPD, 1 OPU, 2 FLOPPY0, 3 FLOPPY1. chDriveNum is 0..1 and chCylinder is 0..86.

### 5.33 ZXSTUSPEECH `USPE` (1 byte) and ZXSTZXPRINTER `ZXPR` (2 bytes): since 1.0 [p:uspeech] [p:zxprinter]

- USPE offset 0: `fPagedIn`, 1 when the uSpeech ROM is paged in. If the block is absent, uSpeech should be disabled.
- ZXPR offset 0: WORD `wFlags`. `ZXSTPRF_ENABLED = 1` enables ZX Printer emulation.

### 5.34 ZXSTDOCK `DOCK`: since 1.3 [p:dock]

Holds Timex TS2068/TC2068/UK2068 cartridge memory, and 128 KB of the Spectrum SE's RAM.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTDF_COMPRESSED = 1`, `ZXSTDF_RAM = 2` (page is read-write, else read-only), `ZXSTDF_EXROMDOCK = 4` (1 = DOCK bank, 0 = EXROM bank) |
| 2 | BYTE | chPageNo | 8 KB chunk 0..7 within the bank |
| 3 | BYTE[var] | chData | 8192 bytes uncompressed |

Notes:
- Only pages supplied by a cartridge are written. For example, no page is written for Timex ROM 1 at EXROM page 0.
- The SE has 8 DOCK and 8 EXROM RAM pages.
- "Only those pages modified from their state at reset should be saved."
- libspectrum names the flags `ZXSTDOCKF_RAM = 2` and `ZXSTDOCKF_EXROMDOCK = 4` [LSZ].
- zxsp notes that EXROMDOCK "was 3 on the SZX webpage" at some point [ZXSP]; the current page says 4.

### 5.35 ZXSTLEC `LEC\0`: 4 bytes, since 1.5 [p:lec]

The LEC RAM interface adds 64/256/512 KB to a 16K/48K machine.

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTLECF_PAGED = 0x01`: RAM is paged into $0000-$7FFF |
| 2 | BYTE | chMemPaged | 32 KB page at $0000-$7FFF (0..15) |
| 3 | BYTE | chNumRamPages | Number of 32 KB pages: 2, 8 or 16 (80/272/528 KB versions) |

LCRP blocks follow. libspectrum only skips LEC and LCRP [LSZ]; as of 2017, Fuse did not support LEC [WIKI].

### 5.36 ZXSTLECRAMPAGE `LCRP`: since 1.5 [p:lecrampage]

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTLCRPF_COMPRESSED = 1` |
| 2 | BYTE | chPageNo | 0..1, 0..7 or 0..15 |
| 3 | BYTE[var] | chData | 32768 bytes uncompressed |

---

## 6. Unofficial extensions and the Spectranet conflict

These blocks come from [WIKI], which says that "with the official specification being somewhat stalled, some emulators have added unofficial extensions", and from their implementation in [LSZ].

### 6.1 ZXSTDIVIDE `DIDE` and ZXSTDIVMMC `DMMC`

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `EPROM_WRITEPROTECT = 1`, `PAGED = 2`, `COMPRESSED = 4` (ZXSTDIVIDE_* and ZXSTDIVMMC_* use the same values) |
| 2 | BYTE | chMemCtrl | Control register (port $E3) |
| 3 | BYTE | chNumRamPages | Number of 8 KB pages. DivIDE is normally 4 (32 KB); DivMMC is 16, 32 or 64. |
| 4 | BYTE[var] | chData | EPROM image. Compressed size = dwSize - 4. |

**EPROM size conflict:** [WIKI] says the EPROM is 16 KB uncompressed. libspectrum reads and writes **8 KB (0x2000)** and rejects any other decompressed length [LSZ `read_divxxx_chunk`, `write_divxxx_chunk`]. Follow libspectrum (8 KB) for compatibility with Fuse. Support: DIDE since Fuse 0.10.0 / libspectrum 0.5.0; DMMC since 1.4.0 [WIKI].

### 6.2 ZXSTDIVIDERAMPAGE `DIRP` and ZXSTDIVMMCRAMPAGE `DMRP`

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | WORD | wFlags | `ZXSTDRF_COMPRESSED = 1` |
| 2 | BYTE | chPageNo | 0..3 on DivIDE; 0..15, 0..31 or 0..63 on DivMMC |
| 3 | BYTE[var] | chData | 8192 bytes uncompressed |

Sources: [WIKI], and [LSZ], which uses `read_ram_page` with length 0x2000.

### 6.3 ZXSTSPECTRANET `SNET`: two incompatible layouts

The **official 1.5 layout** [p:spectranet]:

| Off | Type | Name |
|---|---|---|
| 0 | DWORD | dwFlags |
| 4 | BYTE | chPageA (page at $1000-$1FFF) |
| 5 | BYTE | chPageB (page at $2000-$2FFF) |
| 6 | WORD | wTrap |
| 8 | BYTE[0x30] | chRegisters (W5100 core registers) |
| 56 | DWORD | dwcbFlashLength |
| 60 | BYTE[var] | lpchFlash (0x20000 bytes uncompressed) |
| 60+F | DWORD | dwcbRamLength |
| 64+F | BYTE[var] | lpchRam (0x20000 bytes uncompressed) |

Official flags: PAGED 0x01, PAGED_VIA_IO 0x02, PROGRAMMABLE_TRAP_ACTIVE 0x04, PROGRAMMABLE_TRAP_MSB 0x08, ALL_DISABLED 0x10, RST8_DISABLED 0x20, DENY_DOWNSTREAM_A15 0x40, **FLASH_COMPRESSED 0x80**, **RAM_COMPRESSED 0x100**. The member text calls the flags field "wFlags" while the struct says DWORD.

The **Fuse/libspectrum layout**, which is what actually ships [LSZ `read_snet_chunk`/`write_snet_chunk`] [WIKI]:

| Off | Type | Name |
|---|---|---|
| 0 | **WORD** | flags |
| 2 | BYTE | pageA |
| 3 | BYTE | pageB |
| 4 | WORD | trap |
| 6 | BYTE[0x30] | W5100 registers |

Total 54 bytes. There is no flash or RAM inside the block.

Fuse flags: bits 0x01..0x40 as in the official list, but **0x80 = NMI_FLIPFLOP** ("suppress NMI" flip-flop). Flash and RAM go in separate blocks:
- **`SNEF`**: BYTE flags (`ZXSTSNEF_FLASH_COMPRESSED = 1`), DWORD length, then data. Flash is 0x20000 bytes uncompressed.
- **`SNER`**: BYTE flags (`ZXSTSNER_RAM_COMPRESSED = 1`), DWORD length, then data. RAM is 0x20000 bytes uncompressed.

[WIKI] shows the SNEF and SNER structs with an extra unexplained `DWORD ???` and a DWORD flags field in SNET. Those do not match the code; the code has one BYTE of flags, then a DWORD length [LSZ `write_snef_chunk`]. [WIKI] also warns that the draft spec "does not represent what has been implemented in Fuse".

**Consequence:** a reader must tell the two SNET forms apart, for example by dwSize (54 means Fuse; at least 64 with embedded lengths means official). Whether Spectaculator actually writes the official form is UNCONFIRMED. Fuse and libspectrum support dates from 1.1.0 [WIKI].

### 6.4 ZXSTPALETTE `PLTT` (ULAplus)

| Off | Type | Name | Notes |
|---|---|---|---|
| 0 | BYTE | chFlags | `ZXSTPALETTE_DISABLED = 0`, `ZXSTPALETTE_ENABLED = 1` (64-color mode active) |
| 1 | BYTE | chCurrentRegister | Selected palette register, 0..63 |
| 2 | BYTE[64] | chPaletteRegs | Palette register values |
| 66 | BYTE | (optional) mode/ff register | ULAplus 1.1a. libspectrum reads it only if dwSize > 66 and always writes it (67 bytes); [WIKI] does not document it [LSZ `read_pltt_chunk`/`write_pltt_chunk`]. |

The block may be present for any machine [WIKI]. Zero writes and reads PLTT with the 66-byte struct [ZERO]. libspectrum support dates from 1.2.0 [WIKI].

### 6.5 ZXSTZXMMC `ZMMC`
This block has no payload (dwSize = 0); its presence means the ZXMMC interface is fitted [WIKI] [LSZ]. Supported since Fuse and libspectrum 1.4.0 [WIKI].

### 6.6 Absent from all sources
None of the sources define SZX blocks for TurboSound (2xAY), ZX-Evo/TS-Conf, NeoGS, Kay, Profi, ATM, SMUC, NemoIDE, Kempston mouse coordinates (AMXM stores only the type and the PIO control registers), or the ZX Spectrum Next [p:*] [WIKI] [LSZ]. An emulator for those machines would need its own private block IDs, which spec-conformant readers skip.

---

## 7. Machine-to-block matrix (typical writers)

Assembled from the per-block pages. A "+" means the block is present or relevant.

| Model | Z80R/SPCR | RAMP pages | AY | SCLD | B128 | +3/DSK | ROM (custom) | Other |
|---|---|---|---|---|---|---|---|---|
| 16K | + | 5 | Fuller/Melodik only | - | optional (Beta 128) | - | 16K | KEYB issue2 |
| 48K / NTSC48K | + | 5,2,0 | Fuller/Melodik only | - | optional | - | 16K | KEYB issue2 |
| 128 / +2 / 128Ke | + | 0..7 | + (flags 0) | - | optional | - | 32K | |
| +2A / +3 / +3e | + (1ffd) | 0..7 | + | - | - | +3 only | 64K | SIDE (+3e) |
| Pentagon 128 | + | 0..7 | + | - | + (CONNECTED always) | - | 32K | COVX |
| Pentagon 512 | + | 0..31 | + | - | + | - | UNCONFIRMED (libspectrum: 48K) | |
| Pentagon 1024 | + (eff7) | 0..63 | + | - | + | - | UNCONFIRMED (libspectrum: 48K) | |
| Scorpion ZS-256 | + (1ffd) | 0..15 | + | - | + | - | 64K | COVX |
| TC2048 | + | 5,2,0 | Fuller/Melodik only | + | - | - | 16K | |
| TC2068 / TS2068 | + | 5,2,0 | + (flags 0) | + | - | - | 24K | DOCK |
| SE | + (7ffd) | 0..7 (+8 in libspectrum) | + | + | - | - | 32K | DOCK (8 DOCK + 8 EXROM pages) |

Row sources:
- RAMP pages: [p:rampage]
- AY presence: [p:ay]
- Beta always connected on Pentagon/Scorpion: [p:beta128]
- ROM sizes: [p:custom_rom], [LSZ]
- SE DOCK: [p:dock]
- SE RAMP page 8: [LSZ]
- The optional Beta 128 on 16K/48K is inferred from AUTOBOOT being "48k ZX Spectrum only" [p:beta128].

---

## 8. Version-by-version delta summary [VER]

- **1.0** (Spectaculator 2.5): the original format.
- **1.1** (first public release, Spectaculator 5.1). "New software is only required to support version 1.1 upwards."
  - Added CRTR and JOY.
  - KEYB gained chKeyboardJoystick (+1 byte).
  - Z80R gained chFlags, chBitReg and chReserved in place of 3 reserved bytes.
  - SPCR gained chFe in place of a reserved byte.
- **1.2** (Spectaculator 6.0, Fuse 0.6.x): SCLD (TC2048/TC2068), COVX, GS, GSRP, B128, BDSK.
- **1.3** (Fuse 0.9.x, Spectaculator 7.00):
  - New blocks: DOCK, ZXCF, CFRP, ZXAT, ATRP, PLSD, PDSK, SIDE.
  - New machine IDs: Pentagon 512/1024, SE, TS2068.
  - SPCR gained chEff7 for the Pentagon 1024. RAMP gained the Pentagon 512/1024 page counts.
  - JOY gained Comcon and Timex. KEYB gained Timex joysticks and ZXSTKJT_NONE.
  - SPCR, AY, ROM and RAMP were updated for Timex and Scorpion.
  - Added comments on Multiface 16K RAM mode and the EILAST clarification.
- **1.4:**
  - New blocks: OPUS, ODSK.
  - DSK gained ZXSTDSKF_SIDEB.
  - New machine IDs: NTSC48K and 128Ke.
  - Header gained ZXSTMF_ALTERNATETIMINGS.
  - Z80R gained wMemPtr.
- **1.5** (last updated 2025-03-15):
  - Z80R gained ZXSTZF_FSET (Patrik Rak).
  - New blocks: LEC and LCRP (from JSpeccy), SNET (from the Fuse team; see section 6.3 for the layout conflict).
  - EILAST renamed to SUPPRESS_INTS (binary-compatible).

---

## 9. Implementation pitfalls (collected)

1. The minor-version text in [HDR] still says 4; write 5 for 1.5 (libspectrum does) [LSZ].
2. Z80R AF is a little-endian WORD, so F comes first. Files from libspectrum 0.5.0 and earlier have A and F swapped; detect them through CRTR [LSZ] [WIKI].
3. Parse Z80R bytes 35..36 as MEMPTR only when the version is at least 1.4. In earlier files they are chBitReg and reserved [p:z80regs] [LSZ].
4. KEYB is 4 bytes in v1.0 files [p:keyboard] [LSZ].
5. The same bit value has different meanings in different blocks, for example value 1 is COMPRESSED in RAMP but EMBEDDED in BDSK/PDSK/ODSK and TAPE, and in DSK value 1 is COMPRESSED while 2 is EMBEDDED. Always decode against the specific block.
6. SNET has two incompatible layouts (section 6.3).
7. The DIDE/DMMC EPROM is 8 KB per libspectrum versus 16 KB per the wiki (section 6.1).
8. Zero-Emulator's 128Ke = 15 is wrong; the spec value is 16 [ZERO] [HDR].
9. The spec has label typos: GS "I = contents of the R register", `ZXSTMID_PENTAGON1204`, ZXCF labeled `ZXSTBID_ZXATASP`, and `ZXSTOPUSRT_CUSTOM` versus `ZXSTOPUSF_CUSTOMROM` [p:gs] [HDR] [p:zxcf] [p:opus].
10. Unknown blocks must be skipped silently [BASIC]. libspectrum logs "unknown chunk id" but continues [LSZ].
