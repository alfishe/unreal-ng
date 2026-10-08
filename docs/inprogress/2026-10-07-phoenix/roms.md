# ZXM-Phoenix: firmware images

**Date:** 2026-10-07 · part of [README.md](README.md)

## 1. The image

One 64K file, four 16K pages. Nothing is copied into the repository by this design: the file is **already tracked** as
`data/rom/ZXM-Phoenix_bios.bin` (listed in `data/rom/README-ROMS.md` among the clone firmware, and every shipped
`data/configs/*/unreal.ini` already carries the line `PHOENIX=rom\ZXM-Phoenix_bios.bin`).

| Field | Value |
|:--|:--|
| File | `ZXM-Phoenix_bios.bin` (the Unreal Speccy name; Xpeccy+ ships the same bytes as `phoenix.rom`, older Xpeccy setups call it `zxm_bios_5_03.rom`) |
| Size | 65536 bytes |
| MD5 | `892a393093f373ad3b4c9453f529b523` (Xpeccy+ `config/roms/PROVENANCE.md` lists the same sum) |
| SHA-256 | `1e40d90e164ab497cf0f56b48c701ffc63d30137fc956942f6597150bf3e47b1` |
| Byte-identical copies checked | the repository file, the zx-evo Unreal copy in the local collection (two places), Xpeccy+ `phoenix.rom` |

## 2. Pages

Checked here by reading each 16K page, its strings, and its CRC32 against ROMs already in the repository.

| Page | Role in `rom.cpp` | CRC32 | Contents |
|:--|:--|:--|:--|
| 0 | SYS | `690B37D3` | **all `#FF`**: the page is erased in this image |
| 1 | TR-DOS | `C43D717F` | TR-DOS 5.03 ("1986 Technology Research Ltd."); the same page as page 1 of `data/rom/profi/kramis-v02.rom` (the Kramis build of 5.03) |
| 2 | 128K editor | `B259F7B9` | 128K editor ROM, "1986 Sinclair Research Ltd"; the same page as page 0 of `data/rom/128tr!.rom` |
| 3 | 48 BASIC | `B96A36BE` | Sinclair 48K BASIC; the same page as 48 BASIC in `pentagon.rom`, `profi.rom` and `128.rom` |

The role names and the page order are what the repository's `rom.cpp` already has for `MM_PHOENIX` (`base_sys_rom` = page 0, `base_dos_rom`
= page 1, `base_128_rom` = page 2, `base_sos_rom` = page 3), and what Unreal (`config.cpp` 903-908) and Xpeccy
(`phxMapMem`) use. Add `PHOENIX_ROLES` ("SYS ROM", "TR-DOS ROM", "128K ROM", "48K BASIC ROM") to `ROM::GetROMPageRole` the way
`PROFI_ROLES` is done.

**Consequence of the erased SYS page.** `#1FFD` bit 1 maps page 0, and the 128K-editor / TR-DOS alternates reach it too. With this image
that region reads `#FF` (`RST 38h` loops). A program that selects it on a real board is either running a different image or is
broken; the emulator must reproduce `#FF`, not substitute something. Q1 asks whether a fuller image exists.

## 3. Where the files live (local collection, relative to its root)

| File | Path |
|:--|:--|
| The Unreal-line copy | `tsconf/ft812/materials/emulators/zx-evo-unreal/Unreal/cfg/rom/ZXM-Phoenix_bios.bin` |
| The same, second copy | `tsconf/gamedev/unreal-tsfm/zx-evo/pentevo/unreal/Unreal/cfg/rom/ZXM-Phoenix_bios.bin` |
| Upstream | [`ZXM-Phoenix_bios.bin` in tslabs/zx-evo](https://github.com/tslabs/zx-evo/blob/master/pentevo/unreal/Unreal/cfg/rom/ZXM-Phoenix_bios.bin) |

No other Phoenix ROM was found in any folder of the collection: not by file name (`*phoenix*`, `*zxm*`), and a CRC32 scan of
every 16K page of the collection's `roms/` and `profi/materials/rom-images/` trees, of the Unreal `cfg/rom` folder and of the repository's `data/rom/` found the three pages above only in this image.

## 4. Other firmware the machine uses

| Need | File (already in `data/rom/`) | Source of the choice |
|:--|:--|:--|
| General Sound | `gs105b.rom` (Xpeccy+ names `gs105b.rom` for this machine; the older Xpeccy names `gs105a.rom`) | the shared GS device |
| TR-DOS variants | the Karabas-Pro firmware tree names `TR-DOS_6.11Q_ZXM_PHOENIX_RMD_A.ROM` (listed in [karabas-pro-hardware-analysis.md](../2026-09-21-profi/karabas-pro-hardware-analysis.md) section 5.1): a TR-DOS 6.11 build **for the Phoenix**; the file itself is not in the local collection | open (Q10) |

## 5. Provenance and licence

Same group as the other Russian clone firmware in `data/rom/README-ROMS.md` (property of the board's authors, freely distributed in
the community). The TR-DOS page is Technology Research Ltd's. No change to the licence audit is needed: the file is already named there.

## 6. What the design does with it

1. `data/configs/phoenix/unreal.ini` points `PHOENIX=` at `rom\ZXM-Phoenix_bios.bin` (the key `config.cpp` already reads).
2. Tests use the real file only in the boot test; every other test uses a synthetic ROM whose four pages are tagged
   (the `modelsregression_test.cpp` pattern), so page selection is checked without the image.
