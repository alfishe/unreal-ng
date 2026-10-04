# MAME captures of the "Game" PLD configuration (GAME_00)

References for the Game configuration module
([game-configuration.md](../../../../../docs/inprogress/2026-09-28-sprinter/game-configuration.md)), taken
2026-10-03 with MAME 0.289 (`zxsp`, the `sprinter` driver; see [../README.md](../README.md)) and MAME's BIOS 3.06
(`-bios v3.06`, the MAME pack's `roms/`).

| File | What |
|:--|:--|
| `mame-game00-306.png` | `C:\DEMOS\GAME_00\GAME_00.EXE` on the Game configuration, MAME frame 1000 (the grid scrolls every frame) |
| `mame-game00-306-vram.bin` | MAME's 256 KB video RAM at the same frame end (mode table, palettes, the picture), RGMOD = 0 |
| `mame-game00-306-f1500.png` | the same run, frame 1500 |

Used by `SprinterGameVideo_Test.MameCapture_SamePictureFromMamesVideoRam` (our renderer on MAME's video RAM against
MAME's picture: 1.0 % of the graphics pixels differ, MAME's mid-square offset switch) and
`SprinterGameConfig_Test.RealHdd_Game00ReloadsThePld` (the closest frame of our own run: 0.8 %).

## How they were made

MAME's natural keyboard cannot type `_` into Flex Navigator (the PC keyboard's `-` has no shifted character in
MAME's port list) and a typed `cd demos\game_00` restarted the machine there, so the program is started by the disk
itself: a copy of the raw `sp_hdd_sys.img` whose `SYSTEM.BAT` ends with `cd demos\game_00` and `game_00` instead
of `fn`, converted to a CHD:

```bash
cp sp_hdd_sys.img game00.img
printf '@echo off\r\nset PATH=%%BOOTDSK%%\\;%%BOOTDSK%%\\BIN\\;%%BOOTDSK%%\\DSS\\;%%BOOTDSK%%\\FN\\;%%BOOTDSK%%\\FM\\;%%BOOTDSK%%\\UTILS\\;%%BOOTDSK%%\\ZX\\;%%BOOTDSK%%\\DEV\\SOLID\\;\r\ncd demos\\game_00\r\ngame_00\r\n' > SYSTEM.BAT
mcopy -o -i game00.img@@32256 SYSTEM.BAT ::/SYSTEM.BAT
chdman createhd -i game00.img -o game00.chd -f

cd tools/machines/sprinter/mame-capture
MAME_BIN=<zxsp> MAME_ROMPATH=<the pack's roms/> SPC_HARD1=game00.chd ZXK_SNAP_EVERY=25 \
ZXK_STEPS="1000|vram|game00-f1000;1000|snap|game00-f1000.png;1500|snap|game00-f1500.png;1500|end" \
    ./mame-zxsteps.sh auto-game00 50
```

The program starts at about frame 440 (BIOS, DSS 1.71.57, `SYSTEM.BAT`). The same disk copy with `cd demos\ldconf`,
`start` gives LDConf's `START.BAT`: on MAME it shows the Game grid of `SCROLL.EXE` and then restarts the BIOS over
and over (MAME's PLD reload shortcut, gap analysis §3 item 7), so it is not a reference.
