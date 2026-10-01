# ROM page 8: the BIOS proper ("EXP")

Listing: [bios304-p8-exp.asm](bios304-p8-exp.asm) · symbols:
[bios304-p8-exp.map](../../../../../data/symbols/sprinter/bios304-p8-exp.map) · overview:
[../README.md](../README.md)

The page sits in window 0 (`#0000-#3FFF`) whenever the BIOS runs. It holds the start-up code, the
BIOS function dispatcher and most functions, the packed port table and the font.

## Layout

| Range | Name | Contents |
|---|---|---|
| `#0000` | `Reset` | `JP ColdStart` |
| `#0003-#0007` | `RomNumber`, `BoardId` | board id bytes; `#0005` = `#07` (ZXMAK2 copy: `#24`) |
| `#0008`, `#0018`, `#001B` | `Rst08FromRam`, `Rst18`, `Rst18Return` | the BIOS call entries (RST `#08` from RAM, RST `#18` from ROM) |
| `#0038`, `#0066` | `IntVector`, `NmiVector` | `EI : RETI`, `RETN` |
| `#00A0-#00AF` | `PostCodeTable` | self-test progress codes for the PIO (one per step, 7-segment style) |
| `#00C0` | `BiosVersionText` | `"Sprinter BIOS: ver 3.04"`, `"Sprinter"` |
| `#0100-#036D` | `ColdStart` … `RunSetup` | start-up: restart check, Z84C15 set-up, self test, port table, hand-over from the loader, SETUP |
| `#036E-#0415` | `PortsInit` | ISA, SIO A/B, CTC, PIO, Covox-Blaster, density / IDE / paging defaults |
| `#0416-#0570` | `SpectrumMode` … | Spectrum mode entry; `#046C-#04C0` runs at `#5B00`; `#04C1` the "Spectrum ROM not installed" message |
| `#0571-#08E8` | `HddFnDispatch`, `Fn40_HDD_INIT` … | BIOS functions `#40-#48` (old HDD API) |
| `#08E9-#0C1F` | `FnEE`, `FnF0`, `FnF1`, `FnF3`, `FnF2_FN_SYNC` | PLD configuration switches (Sp97 / AY / user), video registers, palettes |
| `#0C20-#0CA0` | `SCREEN_TABLES` | mode tables (Pentagon / Spectrum timings) |
| `#0CA1-#0D09` | `DcpInit`, `DCP_CONFIG` | **unpacks the port table into page `#40`**; function `#F4` |
| `#0D0A-#12C3` | `FnA1`…`FnA9`, `FnC0`…`FnC8`, `Fn92`…`Fn9F`, `FnEF` | graphics (`#0D0A-#0EF4`), memory manager, RAM disks, `FnEF_FN_VERSION` |
| `#1400-#27F3` | `DcpTablePacked` | the port table, packed (see below) |
| `#2800-#2FFF` | `Font8x8` | 256 × 8 bytes |
| `#3000-#30FF` | `FnTable80` | handler addresses of functions `#80-#FF` |
| `#3100-#31B2` | `BiosCallFromPage0`, `BiosDispatch`, `CallFnTable` | the dispatcher |
| `#31B3-#3CD7` | `Fn..` | RAM-disk, CMOS (`#F5-#F7`), turbo/density (`#8F`), text output (`#80-#8E`), windows (`#B0-#B8`), serial byte send/receive (`#E8`, `#E9`) |
| `#3D00-#3E9A` | `DOS_ON`, `DOS_OFF`, `FN_KBD_OUT` | TR-DOS entry glue, keyboard command output (`#EA`) |
| `#3F06-#3F8F` | `ROM_DISK` | ROM-disk reader |
| `#3FD0-#3FFF` | `BackFromPage0`, `ToPage0`, `Page0CallEntry` | page stubs (see [../README.md](../README.md#calling-the-bios)) |

## BIOS functions in 3.04

Implemented: `#40-#48`, and in the table `#80-#FF` every number except these 22, which return
Carry = 1 through `FnNotImplemented` (`#315D`): `#9B #9C #B9-#BF #E1-#E7 #EB #EC #F9 #FA #FC #FE`.
Functions `#D0-#DF` all point to `#3E20` (`SCF : RET`), so they are not implemented either. Several
numbers share a handler (`#90` = `#C0` GetMemSize, `#91` = `#C1`, `#A9-#AF`, `#FF` = `#EF`).

The names (`FnNN_NAME`) follow `Shared_Includes/constants/BIOS_equ.inc` of the current community
BIOS; numbers that file no longer lists keep the name carried from BIOS-TT 0271ac3 (for example
`FnE8_FN_SEND_BYTE`).

## The port table writer (`DcpInit`, `#0CA1`)

Format of the packed table at `#1400`: a flag byte, then for each of its 8 bits, most significant
first, either one literal byte (bit = 1) or a zero (bit = 0); 16 384 output bytes, written to
`#C000-#FFFF` (page `#40` in window 3). Worked example: the bytes `#60 #C4 #E8` give
`0, #C4, #E8, 0, 0, 0, 0, 0`. After unpacking, map 3 (`#F000-#FFFF`) is overwritten with four copies of
map 0's first KB (the quarter for "DOS on, PN5 = 0"), so map 3 decodes the floppy ports with or
without the DOS signal. The first port read afterwards (`IN A,(#E2)`) opens the decoder.

Result: byte for byte the dump `src/bios/old_files/DCP_PAGE.bin` of BIOS-TT 0271ac3. The differences
to the current BIOS-TT table (the one hardware-reference §4.4 was decoded from) are listed there.

## Hand-over from the loader

The PLD loader leaves `IY = #0107`, `IX = #FFFD`. `#02B3` checks IY and stores IX at `#C13E` of the
system page (`#FE`); with any other IY the BIOS uses `#FFFD` itself.

## Differs from the source

Code regions of 3.04 (≥ 24 bytes) with no matching run in BIOS-TT 0271ac3 (`scripts/diffregions.py`;
70 % of the 7 791 code bytes match). The named label is the nearest one before the range.

| Range | Bytes | Near label |
|---|---|---|
| `#00A9-#00D9` | 49 | `TABLE_X_v8` (POST table, version text) |
| `#00E5-#0153` | 111 | `BiosVersionText` (restart check, start of the cold start) |
| `#02B3-#02CB` | 25 | `PostLoaderHandOver` |
| `#02E1-#036D` | 141 | `PostLoaderHandOver` (system page check, `RunSetup`) |
| `#0416-#04C0` | 171 | `FnFB_GOTO_SPECTRUM` (Spectrum mode, the `#5B00` code) |
| `#0523-#055A` | 56 | `MsgNoSpectrumRom` |
| `#0571-#05A2` | 50 | `HddFnDispatch` |
| `#093D-#0955` | 25 | `FnF3_RST_CONF_CUSTOM` |
| `#09C6-#09EA` | 37 | `INIT_VIDEO_REG` |
| `#0CA1-#0D09` | 105 | `DcpInit` (BIOS-TT builds the table from records instead) |
| `#10C6-#10E2` | 29 | `Fn96_Unnamed` |
| `#11B8-#11F8` | 65 | `BLK_EXIT_1` |
| `#120F-#12C3` | 181 | `FnEF_FN_VERSION` |
| `#3100-#31B2` | 179 | `BiosCallFromPage0` (the dispatcher) |
| `#3200-#3328` | 297 | `FnCA_RAMD_CLEAR` (RAM-disk assignment, CMOS, turbo) |
| `#3372-#339C` | 43 | `FnFD_REINIT` |
| `#3C47-#3CD7` | 145 | `FnE8_FN_SEND_BYTE` (serial send / receive) |
| `#3E20-#3E4B` | 44 | `FnD0_Unnamed` |
| `#3F10-#3F28`, `#3F2F-#3F81` | 108 | `ROM_DISK_Pages` |

The packed table, the font and the text-output and window functions match the source.
