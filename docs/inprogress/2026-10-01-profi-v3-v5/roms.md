# Profi v3 / v5: firmware images

**Date:** 2026-10-01 · part of [README.md](README.md)

Every Profi system ROM is 64K: four 16K pages in the order **SYS (BIOS), TR-DOS, 128 BASIC, 48 BASIC**. The
contents of every image, xpeccy-plus `prfMapMem`, ZXMAK2 `MemoryProfi1024.GetRomIndex` and our `rom.cpp`
`PROFI_ROLES` all give that order.

## 1. Factory images (shipped in `data/rom/profi/`)

The primary source is [speccy4ever, Profi page](https://speccy4ever.speccy.org/_PR.htm). It names each file by its
CRC32 and says what each image holds. Each file was downloaded unchanged, and its CRC32 matches its name there.
The provenance table is in [data/rom/README-ROMS.md](../../../data/rom/README-ROMS.md).

| File | Board | SYS page | DOS page | 128 page | 48 page | Second source |
|:--|:--|:--|:--|:--|:--|:--|
| `kramis-v02.rom` | v3 | JV "KRAMIS" V0.2, 10.1990 (`CEDBE816`) | TR-DOS 5.03, Kramis build (`C43D717F`) | Pentagon 128 editor (`124AD9E0`) | Sinclair (`B96A36BE`) | xpeccy-plus `profi-kramis02.rom` |
| `kramis-v03.rom` | v3 | "Computer Profi" V0.3, 10.1990 (`12D32D79`) | TR-DOS 5.04T (`E212D1E0`) | `124AD9E0` | `B96A36BE` | ZXMAK2 `PROFI_v03.ROM` |
| `bios10-930505.rom` | v5 | Micco ROM Bios 1.0 of 05.05.93 (`18E12224`) | 5.04T | `124AD9E0` | `B96A36BE` | none |
| `bios10.rom` | v5 | Bios 1.0 of 21.09.93 (`9AD55C0E`) | 5.04T | `124AD9E0` | `B96A36BE` | ZXMAK2 `profi_v10.rom` |
| `bios10-kondor504.rom` | v5 | as `bios10.rom`, read off a Kondor 5.04 board | 5.04T | `124AD9E0` | `40EF6484`: `#006D` = `JR Z` | xpeccy-plus `profi-bios10.rom` |
| `bios20.rom` | v5 | Bios 2.0 of 17.04.94 (`3A185C8D`) | 5.04T | `124AD9E0` | `B96A36BE` | xpeccy-plus `profi-bios20.rom`, ZXMAK2 `PROF-M.ROM` |
| `bios20-font.rom` | v5 | `3A185C8D` | 5.04T | `124AD9E0` | `551E1FD4`: one font glyph, `#3D99`-`#3D9A` | none |

What the SYS page code uses tells v3 and v5 apart. This came from a byte-pattern scan whose hits were each checked
at their address; it is not a full disassembly:

| BIOS | Hi-res (`#DFFD` bit 7) | Palette writes (`#xx7E`) | IDE / extended ports | Runs on |
|:--|:--|:--|:--|:--|
| Kramis V0.2, V0.3 | no (`#DFFD` gets only `#30` / `#10` / `#00`) | no | no | v3 |
| Bios 1.0 | yes (`#CF`) | none found | no | v5-era; nothing in it needs v5 hardware |
| Bios 2.0 | yes (`#80`, at `#0337`) | yes, the loop at `#0469` | no | v5 (needs the palette) |
| Bios 2.1-2.3 (2013-2021, community) | yes | yes | `#xxCB` / `#xxEB`, `#06AB`, HDD loader at `#28CE`; 2.3 also `#83` / `#3F` | v5 with IDE |

## 2. Non-factory images we already have

| File | md5 | What it is | Used by |
|:--|:--|:--|:--|
| `data/rom/profi.rom` (default) | `547a56d0...` (CRC32 `A932676F`, speccy4ever `PB20POS`) | Bios 2.0 + TR-DOS 6.08 "PROFI+" (1998) + STS 3.2 monitor (1997) + 48K; no 128 editor | `profi_boot_test` (the hi-res splash and the FDC BUSY loop at `#0797` are Bios 2.0 code) |
| `testdata/machines/profi/rom/profi_mainrom_standart.rom` (untracked) | `321f2c6b...` (`PB21`) | Bios 2.1 of 9.12.2013, TR-DOS 5.04T + RAM disk | `profi_hdd_test` (`#28B3`, `#28CE`) |
| `testdata/machines/profi/rom/profi_v450.ROM` (untracked) | `81cb4529...` (`PB45TREX`) | P.C.C.C. "Award" BIOS 4.50PG (1996) | none |

`data/rom/profi.rom` stays the default of the v5 model: the tests depend on it, and UnrealSpeccy, zx-evo-unreal
and ZX-M8XXX ship the same file. The v3 model defaults to `profi/kramis-v03.rom` (TR-DOS 5.04T) since 2026-10-03:
`kramis-v02.rom`'s TR-DOS 5.03 double-steps on Klug CP/M's 5 x 1024-byte disk and cannot load it
(traced 2026-10-03, see TODO.md); V0.2 stays selectable through `[ROM] PROFI3=`.

## 3. Sync PROMs, manuals and everything else used

The same page has the 2K video sync PROMs: `VR3-*` for v3/4 (five dumps, two of them under the wrong CRC in the
file name), and `VR5-57D728AD` (SAMX12) and `VR5-D2D4A7C8` (Kondor 5.04) for v5. It also has the v3.2, v4.01 and
v5.0 manuals. They are the evidence for [cross-check.md](cross-check.md) sections 4-5. They are not in the
repository: download them from the page above. The analysis kept a copy, together with the Profi ROM images from
other emulators, in a materials folder outside the repository (see [README.md](README.md)).
