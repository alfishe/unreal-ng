# Kay: firmware images

**Date:** 2026-10-07 · part of [README.md](README.md)

Do not copy ROMs into the repository by this work. Two images are already tracked in `data/rom/`; the rest are listed
with their origin so that a later phase can add them with a line in `data/rom/README-ROMS.md`.

## 1. The 64K image

The Kay-1024 board carries one 27512 (64 KB), four 16 KB pages. The vendor's own four files confirm the content: the
local collection `kay-1024/` has `BASIC128.$C`, `BASIC48.$C`, `DOS.$C` and `KRAMIS.$C` (HOBETA files, a 17-byte
header and 16384 bytes). Their payload CRC32 equal the pages of `kay1024.rom` exactly.

| Page of `kay1024.rom` | CRC32 | Content | Same as |
|:--|:--|:--|:--|
| 0 | `72E96D9A` | 128K editor, "2000 NEMO KAY 1024" | `BASIC128.$C` |
| 1 | `632000B3` | 48K BASIC (a Kay build, not `B96A36BE`) | `BASIC48.$C` |
| 2 | `77BACCBB` | TR-DOS 5.04T ("1986 Technology Research Ltd.") | `DOS.$C` |
| 3 | `A25F9B82` | JV "KRAMIS" V.03 service / menu | `KRAMIS.$C` |

## 2. Images and checksums

| File | Size | CRC32 | md5 | Pages (CRC32) | Where it lives |
|:--|--:|:--|:--|:--|:--|
| `kay1024.rom` (Kramis V0.3; MAME calls it `kay1024s.rom`, SHA-1 `1d9c0606b380c000ca1dfa33f90a122ecf9df1f1`) | 65536 | `67351CAA` | `b73346ac326ed486c5df4e1ac1f3d012` | `72E96D9A`, `632000B3`, `77BACCBB`, `A25F9B82` | tracked: `data/rom/kay1024.rom`; [speccy4ever](https://speccy4ever.speccy.org/rom/kay1024.rom) |
| `kay1024b.rom` (Kramis V0.2; MAME SHA-1 `cfa9e6553aea72956fce4f0130c007981d684734`) | 65536 | `AB99C31E` | `8091ac5cda92b2d1a040da1fc0257470` | `CEDBE816` (Kramis 0.2), `77BACCBB`, `DE54C99F` (128), `632000B3` | tracked: `data/rom/kay1024b.rom` |
| `kay1024C.rom` (Unreal fork: `kay1024_v2_1_las.rom`) | 65536 | `878B4D9C` | `f84eaa6b31100cbe3f7fc8509369547d` | `56A7F2CC`, `632000B3`, `A25F9B82`, `DF5B5ECB` | not in the repository; speccy4ever, pentevo `unreal_fix` |
| `kay1024cll.rom` | 65536 | `7073FAA8` | `41ddca7e06ab588e7cab1b1655946aed` | `56A7F2CC`, `4310277F`, `A25F9B82`, `0614AA64` | speccy4ever |
| `kay1024CN.rom` | 65536 | `3247BED6` | `3f80415fcecddb44e5ec11f4d35738be` | `A25F9B82`, `DF5B5ECB`, `56A7F2CC`, `632000B3` | speccy4ever |
| `KAY98.ROM` (Kay-1024 "v1", 1998; MAME adds `trd503.rom` as page 3) | 32768 | `7FBF2D43` | `321b110a61f5ab8d3f9da2c553c15d84` | `DE54C99F` (128), `632000B3` | speccy4ever |
| `KAY256_0_128.ROM`, `KAY256_1_48.ROM`, `KAY256_2_DOS.ROM` (Kay-256, 1994: 48 KB in three files) | 16384 each | `E95F6B7E`, `9AA55289`, `55F36AA5` | `487d30ab...`, `6c1b21ef...`, `62976782...` | one page each | speccy4ever |

The two ROM layouts, `kay1024.rom` ([128, 48, DOS, KRAMIS]) and `kay1024b.rom` ([KRAMIS, DOS, 128, 48]), are the same
four roles in two orders. The schematic has a jumper (JP5) next to the XOR gate that makes the ROM's A15 ([research](research-kay-reference-consensus.md)
section 2), which explains why both exist. Unreal Speccy carries the order in its INI (`[ROM.KAY1]` and `[ROM.KAY2]`).

## 3. Role table: what unreal-ng has and what is open

`core/src/emulator/memory/rom.cpp` already maps `kay1024.rom` as base 128 = page 0, 48 = page 1, DOS = page 2,
SYS = page 3 (case `MM_KAY`), the same names as Unreal Speccy's INI. The role index of the paging logic is
`(ROMS xor TR-DOS) * 2 + 7FFD.4` with 0 = 128, 1 = 48, 2 = SYS, 3 = DOS (Unreal's formula).

The schematic reading gives a simpler physical rule, file page = `(ROMS xor DOS-term) * 2 + 7FFD.4`, which for
`kay1024.rom` would put TR-DOS in 128 mode and Kramis in 48 mode, the opposite of Unreal's roles. Which one is right
is decided by running the image (open question Q1, [TODO.md](TODO.md)): in 48 BASIC, `RANDOMIZE USR 15616` must
print the TR-DOS banner. Phase 1 makes the role-to-page table a four-entry array so either answer is one line, and
`[KAY] RomLayout=` picks the `kay1024b.rom` order (`2, 3, 0, 1` instead of `0, 1, 3, 2`).

## 4. Config

`[ROM] KAY=rom\kay1024.rom` in `data/configs/kay/unreal.ini` (`config.kay_rom_path` exists already). Default image:
V0.3. `kay1024b.rom` is the second choice, not the default.
