# Profi CP/M .pro test images

| File | What |
|---|---|
| `ReadFile.pro` | The bootable Profi CP/M disk of the Habr article "Работа с файлами в ОС CP/M на компьютере «Profi»" (Алексей Тарасов, 2024-08-21): a raw `.pro` dump, 80 x 2 x 5 x 1024 = 819 200 bytes. The Micco / Kondor system (user 15: `BDOS.BIN`, `BIOS.BIN`, `DSKKE9A.DRV` and the other drivers), `BOOTK.COM` (block 2: the boot sector, R = 9 on cylinder 0 side 0; boot word `#5FC4`, a v5 BIOS), the article's examples `EXER1..3.ASM` / `.COM`, `M80R.COM`, `L80.COM`, `WRITE3.COM`, `HC.COM` |

- **Source:** <https://yadi.sk/d/WyrEVfmywXMERQ>, linked from <https://habr.com/ru/articles/837664/> ("Образ диска
  ReadFile.pro – загрузочный диск, с исходными кодами примеров"; the article says ZXMAK2 and Unreal Speccy read the
  format). Fetched 2026-10-03.
- **CRC32:** `65E8CE5C`, **md5:** `10bb2e093f9f73d60f6d33d51777aedd`, **SHA-256:**
  `732766e3fa3d2ddb99c703b012acbe057465f06c4c0864bfaa045f1d1710c5b8`

Used by `core/src/3rdparty/unreal-disk/tests/formats_test.cpp`, `cpmfs_test.cpp`,
`core/tests/emulator/profi_boot_test.cpp` and `tools/verification/mcp/test_media.py`. Format:
`docs/file-formats/disk-images/pro.md`; file system: `docs/emulator/design/disk-file-systems.md#profi-cpm-layout`.
