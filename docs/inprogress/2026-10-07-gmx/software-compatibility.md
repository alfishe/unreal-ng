# GMX software compatibility

Programs written for the Scorpion ZS + GMX, run on the `GMX` model (2026-11-01/02, wt-gmx-impl, offscreen
unreal-qt driven through WebAPI). Each disk is inserted with autostart at power-on: the deferred TR-DOS start waits for
the firmware boot (see [.recipe/machines/gmx.md](../../../.recipe/machines/gmx.md), "Disks at power-on"). Both
firmwares were tried: `gmx644.rom` (ProfROM + GMX 6.44, the default) and `gmx.rom` (Boot ROM V 5.00).

Sources: `$Z` = `zx-spectrum/software/zxart.ee-offline/files/GMX`, `$G` = `zx-spectrum/gmx/izzx-github` (snapshots of
the `izzx-git` repositories) in the local collection. None of the images is stored in the repo.

## Summary

| Program | Version, author | Source | 6.44 | 5.00 | Notes |
|:--|:--|:--|:--|:--|:--|
| Puzzle Bobble | 2024.08.08, izzx | `$Z/Games/P/Puzzle Bobble (2024)(izzx)/2024.08.08(EN)(GS, GMX).trd`, `$G/PB-*.zip PB.TRD` | works | loads | Reaches the difficulty menu within 60 s (28 files, ~1700 sectors). Its samples are for a General Sound, which the model refuses on a GMX (Ermolaev's article), so the game is silent |
| HeroQuest | v1.00, GMX version | `neogs-collection/zxart/gs/600900-HeroQuest/HeroQuestGMX_v1.00.1924trd.zip` | works | works | 640x200 title, language choice |
| GMX Pic View | v1.2.2, izzx | `$Z/System Software/G/GMX Pic View (2020)(-)/v1.2.2(EN)(GMX)(Disk 1 of 2).trd` | works | works | Catalog, 640x200 pictures |
| ANSI Viewer/Player | v0.6b, Alone Coder | `$Z/Applications/A/ANSI ViewerPlayer (2006)(Alone Coder)/v0.6b(EN)(GMX, ATM2).trd` | works | works | Menu and file panel |
| Proteus | 2.31 | `vtrd/files/system/P2315328.zip: GMX/ProteusGMX.trd` | works | works | Drive panels |
| Proteus File Manager | v1.0 (2023) | `$Z/System Software/P/Proteus File Manager (2023)(-)/v1.0(EN)(GMX).trd` | works | refuses | 5.00: "you need GMX with firmware at least v5.XX.29 with two SIMM modules" |
| Phanes | v1.0 (2024), PLM | `$Z/System Software/P/Phanes (2024)(PLM)/v1.0(EN)(GMX, KM).trd` | works | refuses | 5.00: "at least v5.XX.40 with 2Mb memory" |
| ZX Word 3 | v3.00gmx (2024), PLM | `$Z/Applications/Z/ZX Word 3 (2024)(-)/v3.00gmx(EN)(GMX, KM)[h 2024 PLM].trd` | works | refuses | 5.00: the same v5.XX.40 message |
| FATALL | v0.25 (2009), Savelij; GMX hack 2020 | `$Z/System Software/F/FATALL (2009)(-)/v0.25(EN)(128K, GMX)[h 2020 Evgeniy Muchkin, Savelij13].trd` | works | works | Without a card "FAT not found"; with the SD folder in `sd` it lists `E: ZC SDC FAT32` |
| Video player (VIDEO03, VPlayer 0.1.1) | izzx | `$G/Video-*.zip VIDEO03/DISK.TRD` | works | - | Plays its sample `VIDEO.GMV` (160x200, 10 fps) from the SD folder; device menu "Select 0-0: SD ZC" |
| VPlayer | v0.1.7 (2026), izzx | `$G/Video-*.zip VPlayer/DISK.TRD` | works | - | Plays a GMV in the 0.1.5+ layout (512-byte header, 4096 sound + 16384 picture per frame) from the SD folder |
| VPlayer | v0.1.6 (2022/23), izzx | `$Z/System Software/V/VPlayer (2022)(-)/v0.1.6(EN)(GMX).trd` | partly | - | Menu, SD and file list work. No sample in its own frame layout is available: the VIDEO03 sample and its 0.1.7 conversion both load at the wrong offset (the attribute page receives a sound packet) |
| CP/M 2.2 (CPMZSGMX) | izzx after FK0'99 | `$G/CPM-*.zip CPMZSGMX.trd` | boots | - | The shell starts `CPM`: "CP/M version 2.2 ... BIOS Ver 2.0 for Scorpion/Kay/Pentagon", then "Bdos Err On E: Bad Sector": drive E: is the SMUC HDD partition `CPMZS0` (MicroDOS type), which has to be created in the monitor first (its ReadMe) |
| GMX Commander | v1.0 (2002), FION; fix 2020 Evgeniy Muchkin | `$Z/System Software/G/GMX Comander (2002)(-)/v1.0fix(EN)(GMX)[h 2020 Evgeniy Muchkin].scl` | hangs | works | Needs a hard disk: without one "REQUIRED HARDWARE NOT FOUND!" on both firmwares. With an MFS disk on `ide0.master` it works on 5.00; on 6.44 it reads the disk geometry from ProfROM v5 RAM variables (`#DFFC` heads, `#DFFD` sectors, RAM page 8), gets garbage, computes a bad LBA and waits for the disk forever. The program's limit, not the model's |

Legend: works = reaches its working screen and reacts to keys; loads = still loading when the 25 s screenshot was
taken (not followed further); refuses = the program's own firmware check stops it with a message; `-` = not tried.

## Firmware

- 6.44 runs everything that names a minimum firmware (v5.XX.29, v5.XX.40). 5.00 is older than both minimums.
- GMX Commander is the only program that needs 5.00 (its RAM variables). Switch: `data/configs/gmx/unreal.ini`,
  `GMX=rom\gmx.rom`, then restart; each firmware keeps its own CMOS file.

## Media used

- SD card: a host folder in the media slot `sd` (`media insert sd <folder>`) is a FAT32 volume. The video players need
  an unfragmented file (their ReadMe); the 42 MB sample played from the folder volume as it is.
- Hard disk: an image on `ide0.master`; ProfROM finds it after a cold reset with Caps Shift + 5 held, and its
  Partition manager formats MFS. Images were generated for the test and deleted afterwards.

## How it was checked

Scripts in the worktree's `scratch/gmx-soft/` (not committed): a WebAPI loop that starts a `GMX` emulator per disk,
mutes it, inserts the disk with autostart (and the SD folder when needed), screenshots at 8 s and 25 s, then taps the
keys a program asks for. Hangs were looked at with the port trace and memory reads (`/profiler/porttrace`,
`/registers`, `/disasm`, `/memory/ram/{page}`), for example:

- Puzzle Bobble's "Loading..." at 25 s: the trace showed TR-DOS reading the catalog for each sample (`#5CDD` = the
  file name), and at 60 s the code sits in its menu loop (`HALT`, key read, `CP #36` / `#37` / `#20`).
- VPlayer 0.1.6: the pixel pages `#39` / `#3B` hold the `#0F` pattern the player writes, the attribute page `#79` holds
  1104 sound bytes at offset 512, so the frame offset, not the 640x200 output, is off.
