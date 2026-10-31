# Scorpion ZS 256 + GMX test fixtures

Software and media for the `GMX` model ("ZS Scorpion + GMX", 2048K), collected 2026-10-30 for the GMX work (design:
[docs/inprogress/2026-10-07-gmx/](../../../docs/inprogress/2026-10-07-gmx/README.md)). The card's firmware is not here:
it is a ROM and lives in `data/rom/` (`gmx644.rom`, `gmx.rom`; see `data/rom/README-ROMS.md` and the design's
[roms.md](../../../docs/inprogress/2026-10-07-gmx/roms.md)). Both programs below need `gmx644.rom`, OSZ needs its
FAT32 support and they were written for and run on the real board. What each one exercises, with source lines:
[research-2026-10-30-software.md](../../../docs/inprogress/2026-10-07-gmx/research-2026-10-30-software.md).

| Path | Size | SHA-256 (first 16) | Content |
|:--|--:|:--|:--|
| `edgegrinder/EGGMX.TRD` | 655 360 | `5d0151d711e576fe` | Edge Grinder for ZS GMX (izzx, release 2021-09-09), TR-DOS disk, boots with `boot` |
| `osz/OS.TRD` | 655 360 | `9f4319d2f88ea46f` | OSZ boot disk: `boot`, the `OS` kernel (loaded at `#C000`); the kernel then reads `\OSZ\` from the SD card |
| `osz/sdcard/OSZ/` | 75 KB | per file below | the `Release/OSZ` folder of OSZ, to be presented as the SD card (FAT32) |
| `osz/sdcard/OSZ/autorun.txt` | 7 | `398c55b2503f0afc` | **changed:** `nc.apg` only. Upstream starts `net.apg` first, which loops forever without the ZX Wi-Fi UART at `#EF` |
| `osz/osz-sd.zip` | 123 863 | `1e593722eb8afe41` | the SD set of the web Unreal Speccy (zxby.org, 2026-10-29): `osz.vhd` (64 MB fixed VHD, FAT32 at LBA 63, `\OSZ\` = the Release folder, upstream `autorun.txt`), `OS.TRD` (same as above), `osz.json` `{"model":"gmx","a":"OS.TRD"}` |
| `reference/edgegrinder-author-screenshot.png` | 13 350 | `a5326c23a91310ec` | the author's capture of gameplay (`Docs/Скрин.png`, 720x576 with border, from Unreal Speccy); by-eye reference, not a golden image |

Files of `osz/sdcard/OSZ/` (unchanged from upstream except `autorun.txt`): `os.$c` 15 633 (`3b69420040d4d033`,
"OS ver 2025.10.08"), `sys.osg` 1 047 (`b972bce7391b30f9`), `nc.apg` 9 638 (`84372368cdccda78`, None Commander),
`tetris.apg` 3 051 (`321039041be74311`), `artview.apg`, `moonr.apg`, `radio.apg`, `terminal.apg`, `update.apg`,
`net.apg`, `gp_gzip.bin`, `update.txt`, `ReadMe.txt` (cp866), `data/` (`example.pt3`, `example.scr`, `index.gph`,
`logo.scr`).

## Sources

- **EdgeGrinder:** <https://github.com/izzx-git/EdgeGrinder>, commit `6639aa1` (2025-05-16), file `EGGMX.TRD`. A port
  of the CPC game from the formatwar Collabortition sources (<http://formatwar.net/view_article.php?art=collabortition_1>).
  No licence file.
- **OSZ:** <https://github.com/izzx-git/OSZ>, commit `2e1c9fd` (2026-10-30), folder `Release/OSZ`. Multitasking OS for
  the ZS GMX by izzx (2024-2026), forum <https://zx-pk.ru/threads/35996-os-dlya-zs-gmx.html>. No top-level licence;
  bundled third-party parts keep their own (Moon Rabbit: Nihirash's "Coffeeware").
- **osz-sd.zip:** <https://www.zxby.org/unreal/sd/osz.zip>, fetched 2026-10-30, unchanged.

## What they can test

| Fixture | Scenario | Pins |
|:--|:--|:--|
| `EGGMX.TRD` | boot -> loader picture in the extended mode | TR-DOS loading on GMX, `#7EFD = #C8`, pixel pages `#39 / #3B`, attributes `#79` |
| same | map pre-render, fade between screens | the 7-bit `#C000` page formula over pages `#10-#67`, RAM page 0 at `#0000`, `#78FD` XOR 2 |
| same | title screen with AY music | IM 1 in RAM page 0, 50 Hz INT, screen pair flipped by `#7FFD` bit 3 every frame |
| same | gameplay, key R | the frame counter: 25 on a correctly timed 7 MHz board (the author measured 16 before his speed-ups, 25 after, on the real board) |
| `OS.TRD` + `osz/sdcard/` | boot to the None Commander panel | RST 8 into the shadow monitor (`OUT #1FFD,#12` takes effect before the next fetch), monitor FAT32 over the Z-Controller, port `#00` writes |
| same | console filled past row 25 | hardware scroll `#7AFD/#7CFD`: byte offset in multiples of 640, wrap at 16000 |
| same | task switch (SS+Enter), Ext+1 | read-backs of `#7AFD` and `#78FD` on every interrupt |
| `osz-sd.zip` | the same boot from the raw image | VHD footer and MBR handling, parity with the web emulator |
