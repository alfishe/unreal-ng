# Quorum: firmware

**Date:** 2026-10-07 · part of [README.md](README.md)

## 1. The image

| File | Size | md5 | sha1 | CRC32 |
|:--|--:|:--|:--|:--|
| `qu7v42.rom` | 65536 | `f559d4f0137b8ed062899afbc209f211` | `f8e22672722b0038689c6c8bc4acf5392acc9d8c` | `E950EEE5` |

Where it lives: `data/rom/qu7v42.rom` in the repository already (listed in `data/rom/README-ROMS.md` among the clone
firmware); the same bytes ship with UnrealSpeccy (`QUORUM=qu7v42.rom` in `x32/unreal.ini`, "standard quorum 64K rom"),
ZXMAK2 (`ROMS/QUORUM/QU7V42.ROM`, file date 1997-06-25) and
[ZX-M8XXX](https://github.com/Bedazzle/ZX-M8XXX/blob/HEAD/roms/qu7v42.rom) (checked: md5 equal in all three). It is
not in the local collection (`zx-spectrum/`), which has no Quorum material. Nothing needs to be copied.

## 2. Pages (16K each, the order of `rom.cpp` and of UnrealSpeccy and ZXMAK2)

| Page | Role | CRC32 | md5 | What was seen |
|:--|:--|:--|:--|:--|
| 0 | SYS (service menu) | `06515CF7` | `9f9b3be2374b30504fdc529bc0716aa5` | starts `F3 21 00 00 F9` (DI; LD HL,0); string "ROM-MENU QUORUM V.4.2 27.06.1997" at `#07F0` with the author name "Karimov Kamill"; "1994 Quorum Ltd." at `#0DFD`; "Copy 128" at `#1342`; a ROM/RAM test routine (`#0700`); NMI handler at `#0066` |
| 1 | TR-DOS | `9F1F7687` | `c4cbaa5f336233c1c1346a864952e4d5` | starts `F3 11 FF FF`; accesses the FDC at `#80..#83` and `#85` only |
| 2 | 128 BASIC editor | `FF74B991` | `1082fd0f5cae4eb222c105b4e79e3971` | "1994 Quorum Lt" at `#0565`; `LD BC,#7FFD` and AY ports `#FFFD / #BFFD` |
| 3 | 48 BASIC (patched Sinclair) | `5DA04C81` | `ac452e2adf4a31168679d5a84a0146f9` | starts `F3 AF 11 FF FF C3 CB 11`; patched with `OUT (0)` code at `#39E0..#39FF` (RAM at 0 with `#01`, 48K lock with `#60`) |

These are byte-pattern scans and string searches, not a disassembly.

## 3. Variants and other images

- The 128K and 1024K machines use the same 64K image (UnrealSpeccy `RAMSize` 128 / 1024 with one `QUORUM=` key; ZXMAK2
  "Quorum 256" uses ROM set "Quorum128"). No second factory image is known.
- ZXMAK2's "Quorum 64" names a ROM set "Quorum64" (one 16K page); no such image is in any source tree read. Out of scope.
- A synthetic tagged ROM (page k filled with `#C0 | k`) is used by the unit tests, as the Profi tests do; the real ROM
  is used in the boot tests only.

## 4. Provenance to add

`data/rom/README-ROMS.md` lists `qu7v42.rom` without a source line. A phase 1 task adds one: author and version from
the ROM's own menu string, and the three emulator bundles above as second sources.
