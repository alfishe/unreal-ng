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
| Puzzle Bobble | 2024.08.08, izzx | `$Z/Games/P/Puzzle Bobble (2024)(izzx)/2024.08.08(EN)(GS, GMX).trd`, `$G/PB-*.zip PB.TRD` | works | loads | Reaches the difficulty menu within 60 s (28 files, ~1700 sectors) and uploads 415 KB of samples to the General Sound. Plays with sound on the config's NeoGS and on a classic GS with 512 KB (`slots plug zxbus.1 gs ram=512k`); a 128 KB GS stays silent (the samples do not fit) |
| HeroQuest | v1.00, GMX version | `neogs-collection/zxart/gs/600900-HeroQuest/HeroQuestGMX_v1.00.1924trd.zip` | works | works | 640x200 title, language choice |
| GMX Pic View | v1.2.2, izzx | `$Z/System Software/G/GMX Pic View (2020)(-)/v1.2.2(EN)(GMX)(Disk 1 of 2).trd` | works | works | Catalog, 640x200 pictures |
| ANSI Viewer/Player | v0.6b, Alone Coder | `$Z/Applications/A/ANSI ViewerPlayer (2006)(Alone Coder)/v0.6b(EN)(GMX, ATM2).trd` | works | works | Menu and file panel |
| Proteus | 2.31 | `vtrd/files/system/P2315328.zip: GMX/ProteusGMX.trd` | works | works | Drive panels |
| Proteus File Manager | v1.0 (2023) | `$Z/System Software/P/Proteus File Manager (2023)(-)/v1.0(EN)(GMX).trd` | works | refuses | 5.00: "you need GMX with firmware at least v5.XX.29 with two SIMM modules" |
| Phanes | v1.0 (2024), PLM | `$Z/System Software/P/Phanes (2024)(PLM)/v1.0(EN)(GMX, KM).trd` | works | refuses | 5.00: "at least v5.XX.40 with 2Mb memory" |
| ZX Word 3 | v3.00gmx (2024), PLM | `$Z/Applications/Z/ZX Word 3 (2024)(-)/v3.00gmx(EN)(GMX, KM)[h 2024 PLM].trd` | works | refuses | 5.00: the same v5.XX.40 message |
| FATALL | v0.25 (2009), Savelij; GMX hack 2020 | `$Z/System Software/F/FATALL (2009)(-)/v0.25(EN)(128K, GMX)[h 2020 Evgeniy Muchkin, Savelij13].trd` | works | works | Without a card "FAT not found"; with the SD folder in `sd` it lists `E: ZC SDC FAT32` |
| Video player (VIDEO03, VPlayer 0.1.1) | izzx | `$G/Video-*.zip VIDEO03/DISK.TRD` | works | - | Plays its sample `VIDEO.GMV` (160x200, 10 fps) from the SD folder; device menu "Select 0-0: SD ZC". Sound through the General Sound (its own code on the card, 11.25 kHz): plays on the NeoGS and on a GS 512 KB, silent on a GS 128 KB, which is the hardware: the card code streams into MPAG pages 2-15 (fixed in `GS_Code.C`, the host checks only for 3+ pages), and on 128 KB the pages 5, 9 and 13 are the first RAM pair holding the code |
| VPlayer | v0.1.7 (2026), izzx | `$G/Video-*.zip VPlayer/DISK.TRD` | works | - | Plays a GMV in the 0.1.5+ layout (512-byte header, 4096 sound + 16384 picture per frame) from the SD folder. Sound on all three ports of its `S` choice (pressed on the panel before ENTER): `#B3` through the NeoGS and a GS of 128 or 512 KB (LPT Covox mode of the card), `#FB` through the SounDrive, `#DD` through the Covox on the printer port (built-in `covox-dd`, 2026-11-03: `snd_port` `#8DE0` = `#DD`, the `covox` source plays) |
| VPlayer | v0.1.6 (2022/23), izzx | `$Z/System Software/V/VPlayer (2022)(-)/v0.1.6(EN)(GMX).trd` | works | - | Plays a headerless GMV in the 0.1.5 layout (4096 sound + 16384 picture per frame), the native sample `DD-ak` from `$G/Video-*.zip VIDEO04.02`, from the SD folder. Sound on port `#B3` (its `S` choice "B3 (GS)") through the NeoGS and a GS 128 KB; and on "DD (LPT)" through the Covox on the printer port (built-in `covox-dd`, 2026-11-03): the `covox` capture and the NeoGS one give the same envelope match to the sample (same rate and start frame, r 0.338 / 0.336). Without the speed calibration of 0.1.7 it runs at 12.4 fps on the GMX |
| CP/M 2.2 (CPMZSGMX) | izzx after FK0'99 | `$G/CPM-*.zip CPMZSGMX.trd` | works | - | Drive E: is the SMUC HDD subpartition `CPMZS0` (MicroDOS type, default 32767 KB) inside an MFS partition, made in the 6.44 Hard disk utility (its ReadMe). The manager leaves it zero-filled, so `DIR` lists blank entries until `INSTALL` item 3 restores the distribution's data (2304 sectors from the floppy, about 5 minutes). Then the shell's `CPM` boots to `e>`, `DIR` lists 12 files, `STAT` gives "E: R/W, SPACE: 1724k", Ladder plays on the 80x25 GMX screen. Without the subpartition: "Bdos Err On E: Bad Sector". HDD boot (`INSTALL` item 1, monitor "HDD boot") not tried |
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
  Partition manager formats MFS. Images were generated for the test and deleted afterwards. The CP/M disk: a blank
  128 MB image, Magic (`nmi` with `magic: true`), `H` (Hard disk utility), `M` `A` (detect), `0`, `P` (Partition
  manager), the HDD, Unused -> Create MFS -> `Y`, the size; the MFS -> Local partitions -> Unused -> Create
  <MicroDOS>, the name typed with Caps Shift (`keyboard/combo` `["CAPS", "C"]`: a plain tap gives lower case),
  the size 32767 KB. The catalogue then holds type 2 `CPMZS0` at LBA 65, 65024 sectors.

## How it was checked

Scripts in the worktree's `scratch/gmx-soft/` (not committed): a WebAPI loop that starts a `GMX` emulator per disk,
mutes it, inserts the disk with autostart (and the SD folder when needed), screenshots at 8 s and 25 s, then taps the
keys a program asks for. Hangs were looked at with the port trace and memory reads (`/profiler/porttrace`,
`/registers`, `/disasm`, `/memory/ram/{page}`), for example:

- Puzzle Bobble's sound: the GS port trace (`/state/audio/gs/porttrace`) counts the host's data bytes and the card's
  DAC fetches, a GS-only capture (`/audio/capture`, `source: gs`) during the game reads RMS 0.046 (NeoGS) and 0.066
  (GS 512 KB), 0 with GS 128 KB.
- Video players' sound: a GS-only (or `covox`) capture while the video plays, matched against the file's sound track.
  VIDEO03 plays at a steady clock: the normalized cross-correlation of a 0.25 s window over a scan of rates peaks at
  r = 0.84 (NeoGS) and 0.91 (GS 512 KB) at 11.25 kHz. VPlayer 0.1.7 emits bytes while it reads the card and in its
  wait loop, so the clock varies inside a frame; there the 10 ms RMS envelope of the capture is matched against the
  track's envelope, scanning frame rate and start: r = 0.83-0.85 at 10.0 fps on all four devices, against 0.47-0.50 at
  random rates for another video (the control). VPlayer 0.1.6 on `#B3`: r = 0.77 at 12.4 fps on the NeoGS and the
  GS 128 KB, the same frame and rate on both. The sample is the native 0.1.5 file `DD-ak` from
  `$G/Video-*.zip VIDEO04.02` (2248 sound bytes per frame) as it is for 0.1.6, with a 0.1.7 header put in front for 0.1.7.
- Puzzle Bobble's "Loading..." at 25 s: the trace showed TR-DOS reading the catalog for each sample (`#5CDD` = the
  file name), and at 60 s the code sits in its menu loop (`HALT`, key read, `CP #36` / `#37` / `#20`).
