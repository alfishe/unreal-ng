# MDOS test images

| File | What |
|---|---|
| `bootdisk.d80` | MDOS3's boot disk: slot 0 of `mdos3-discs1.md3` as a D80 (80 x 2 x 9 x 512 = 737 280 bytes, the disk's own geometry). Disk "bootdisk", 109 files, 7 free sectors |
| `mdos3-3slots.md3` | The first 3 slots of `mdos3-discs1.md3` (3 x 1693 sectors): "BOOT 20.1.2006", "ARTSTUDIO - ARTIST2", "GRAFIKA 1" (94 cylinders) |

Source: https://velesoft.speccy.cz/zx/divide/systems/mdos3/mdos3-images.zip (the MDOS3 distribution's disk images).
Used by `core/src/3rdparty/unreal-disk/tests/mdosfs_test.cpp` and `tools/verification/mcp/test_media.py`. Layout:
`docs/emulator/design/disk-file-systems.md`.
