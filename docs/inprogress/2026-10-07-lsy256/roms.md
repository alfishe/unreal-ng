# LSY256: firmware image

**Date:** 2026-10-07 · part of [README.md](README.md) · analysis: [research-rom-analysis.md](research-rom-analysis.md)

One image, 64K = four 16K pages. It is already in the repository.

| Item | Value |
|:--|:--|
| Repository file | `data/rom/lsy256.rom` (tracked; listed in `data/rom/README-ROMS.md` among the clone firmware) |
| Size | 65536 |
| CRC32 | `75E444BE` |
| MD5 | `c4d396ebdbd84dbc2a6236b09af3ab83` |
| SHA-256 | `1437500be9c1fea75cb680b7acc160cfbae088564ce3ab5f0ec7b811de8f5324` |
| Local collection | the zx-evo Unreal tree, `cfg/rom/lsy256.rom` (byte-identical) under `tsconf/gamedev/unreal-tsfm/` of the local ZX collection |
| Upstream | <https://github.com/tslabs/zx-evo> `pentevo/unreal/Unreal/cfg/rom/lsy256.rom` |
| Config key | `[ROM] LSY=` (read by `config.cpp`; already in the shipped INIs as `LSY=rom\lsy256.rom`) |

## Pages

| Page | Role (`rom.cpp`) | Content | CRC32 | MD5 (first 12) |
|:--|:--|:--|:--|:--|
| 0 | 128 | a Pentagon-128-class editor ROM (147 bytes differ from `data/rom/pentagon128k.rom`) | `82E269F6` | `1dd5e3e9362c` |
| 1 | 48 (SOS) | 48 BASIC with a BK-08 keyboard table and an NMI hook | `E8A7C0EE` | `4be129e38ccd` |
| 2 | SYS (LSY-Setup) | 8K system ROM mirrored twice: RAM test, menu, NMI hook, far-call stub | `BFB7220C` | `3aada543551a` |
| 3 | TR-DOS | TR-DOS 5.04T, identical to `data/rom/trdos504t.rom` | `E212D1E0` | `b4c9634312b7` |

`rom.cpp` already maps these roles (`base_128_rom` = page 0, `base_sos_rom` = 1, `base_sys_rom` = 2, `base_dos_rom` = 3), the
same as Unreal's `apply_memory`.

## Needed work

- Nothing to copy. A tagged-ROM identification entry (so the debugger names the four pages) follows the pattern of the other
  models; the signature bytes are chosen in phase 1.
- No second image exists. Are there factory images with other BK-08 firmware? Unknown (open question Q9).
- Do not copy ROMs or collection files into the repository.
