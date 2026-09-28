# NedoOS SD card (minimal)

The smallest host folder that boots NedoOS on ZX-Evo from the SD card to its
command shell. `ZXEvoErs_Test.NedoOsBootsFromAHostFolder` inserts `sdcard/`
as the Z-Controller card; the media manager presents it as a FAT16 volume.

| File | Origin |
|------|--------|
| `sdcard/SD_BOOT.$C` | `release/sd_boot.$C` (ZX-Evo build: PS/2 keyboard, NemoIDE, NeoGS SD) |
| `sdcard/bin/term.com` | `release/bin/term.com` (terminal the kernel starts first) |
| `sdcard/bin/cmd.com` | `release/bin/cmd.com` (command line interpreter) |
| `sdcard/bin/autoexec.bat` | ours: echoes the marker `UNREALNGSDBOOT` |

Source: https://github.com/alfishe/NedoOS, revision `cc0c7f98`, folder `release/`.

The kernel runs `term.com cmd.com autoexec.bat` from `bin/`; the shell then
prints the marker and the prompt `M:/bin>`.
