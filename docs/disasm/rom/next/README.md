# ZX Spectrum Next ROM listings

Byte-complete disassemblies of the code a ZX Spectrum Next runs before and around its operating system:
the FPGA boot ROM, the NextZXOS DivMMC ROM and the four NextZXOS ROMs. They are the reference for every boot trace of the
Next machine ([design](../../../inprogress/2026-10-07-zx-next/README.md), especially
[esxdos-and-sd.md](../../../inprogress/2026-10-07-zx-next/esxdos-and-sd.md)). No source of NextZXOS is published, and the
original esxDOS 0.8.x is closed too, so these listings are how we learn what the guest does to the card and to the
NextREGs.

| Listing | Image | Size | CRC32 | Shown at | What |
|:--|:--|--:|:--|:--|:--|
| [boot/next-boot.asm](boot/next-boot.asm) | the byte array in `cores/zxnext/src/rom/bootrom.vhd` ([FPGA repository](https://gitlab.com/SpectrumNext/ZX_Spectrum_Next_FPGA)) | 8 192 | `ccbd55ba` | `#0000` while `bootrom_en` | the loader that reads `TBBLUE.FW` from the SD card and starts it at `#6000` |
| [nxtmmc/next-nxtmmc.asm](nxtmmc/next-nxtmmc.asm) | `machines/next/enNxtmmc.rom` ([tbblue](https://gitlab.com/thesmog358/tbblue)) | 8 192 | `9acd52bf` | `#0000-#1FFF` by the DivMMC automap | NextZXOS's DivMMC ROM: the `RST $08` gateway, NMI, block driver for the SD card |
| [nextzx/next-rom0.asm](nextzx/next-rom0.asm) | `enNextZX.rom`, bytes `#0000-#3FFF` | 16 384 | `31936f3c` | `#0000-#3FFF` | ROM 0: the 128K editor |
| [nextzx/next-rom1.asm](nextzx/next-rom1.asm) | `#4000-#7FFF` | 16 384 | `ae896146` | same | ROM 1: the 128K syntax checker |
| [nextzx/next-rom2.asm](nextzx/next-rom2.asm) | `#8000-#BFFF` | 16 384 | `8a954e23` | same | ROM 2: +3DOS / IDEDOS / NextZXOS API (106 named entries) and **the SD card driver and its initialization** |
| [nextzx/next-rom3.asm](nextzx/next-rom3.asm) | `#C000-#FFFF` | 16 384 | `8d0a6f4e` | same | ROM 3: 48K BASIC (a modified 48K ROM; names carried from the classic ROM map where a unique byte window matches) |

The images themselves are in [scripts/](scripts/) (`next-*.bin`); they come from the `tbblue` repository (the Next distribution
tree) and the FPGA sources, extracted by `scripts/extract.py`. **The listings rebuild the images byte for byte**
(`scripts/checkcov.py` is the gate: 6 of 6 OK).

## Status (first pass, 2026-10-08)

The listings are generated, not yet hand-annotated. What is named by hand today:

| Listing | Named by hand |
|:--|:--|
| ROM 2 | all 106 API entries (`DOS_OPEN`, `IDE_SECTOR_READ`, `IDE_BANK`, ... from the NextZXOS API document), the SD driver: `SdCardInit` (CMD12, CMD0 `#95`, CMD8 `#87`, ACMD41 loop, CMD58, CMD16), `SdCommand`, `SdReadBlock` (CMD17), `SdWriteBlock` (CMD24 + CMD13), `SdStopTransmission`, `SdReadOcr`, `SectorToCardAddress` |
| DivMMC ROM | the vectors, `ReadNextReg`, `MapBankCToMmu67` (`NEXTREG #56/#57`), the SD block driver (same conversation as ROM 2), `SetConMem` |
| boot ROM | the reset path |
| ROM 3 | the names carried from `data/symbols/48k_rom.map` |

Code versus data comes from a reachability walk (vectors, API jump table, hand entries) plus **tentative** entries found
by scanning the leftovers (runs of `JP nn`, tables of plausible code addresses, gaps that start with six plausible
instructions). A `T` label and the note "tentative entry" mark those; they are not verified and some are data that
happens to decode (a few runs in the editor ROMs, the BASIC token tables). A hand pass promotes an address into
`ENTRIES` or `FORCEDATA` of the `dict_<target>.py` file; the generator keeps hand names over generated ones.

| Listing | Code bytes found | Data blocks |
|:--|--:|--:|
| boot | 87.5 % | 15 |
| DivMMC ROM | 93.0 % | 32 |
| ROM 0 | 93.4 % | 69 |
| ROM 1 | 89.3 % | 91 |
| ROM 2 | 96.3 % | 40 |
| ROM 3 | 85.6 % | 96 |

(The percentages count tentative code; treat them as an upper bound.)

## Symbols

One file per listing in [`data/symbols/next/`](../../../../data/symbols/next/) (`NAME: equ 0xADDR`, hand names and entry
points) for the emulator's label loader. The DivMMC ROM and the ROM pages are shown at `#0000`, so the symbol file of a
page applies while that page is mapped there (ROM pages in the Next are selected by the 128K / +3 paging ports or the
alternate-ROM registers; the DivMMC ROM by the automap).

## How the pieces fit (the card path)

```
RST $08 : DEFB hook  ->  automap maps nxtmmc (#0008)  ->  OS code in DivMMC RAM / ROM 2 API
                                                                |
          card init + block read / write (ROM 2 #18D6-#1A82, DivMMC ROM #1EC6-#1FDD)
                                                                |
                    OUT (#E7) select, OUT/IN (#EB) data  ->  SPI master (FPGA)  ->  SD card
```

## Regenerating

```
python3 scripts/extract.py --sd <tbblue clone> --vhdl <FPGA clone>/cores/zxnext/src/rom/bootrom.vhd
Z88DK_DIS=<z88dk>/bin/z88dk-dis python3 scripts/gen.py all
python3 scripts/checkcov.py
```

`z88dk-dis` decodes each code run (`-mz80n`); `LDIRSCALE` (`ED B6`, unknown to it) is printed as `ldirscale`. The
walker and the Z80N lengths are in `scripts/z80n.py`.

## Next steps

1. Hand pass over the DivMMC ROM: the `RST $08` dispatcher at `#0512` and its hook table, the stack switch to DivMMC RAM,
   the drive / partition tables, and the paths that call `SdReadBlock`.
2. ROM 2: the IDEDOS partition code, `IDE_BANK` (8K bank allocation in ZX and DivMMC memory), `IDE_MOUNT`.
3. Boot ROM: the loader's FAT reader and the SD init (compare with `tbblue/src/firmware/loader/src`, which is the C source
   of the same program: a transfer script like the Sprinter's `transfer.py` can carry its names).
4. ROM 0 / 1 (editor): only what the machine needs (the NextBASIC entry points used by dot commands and the browser).
