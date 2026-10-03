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

# NedoOS SD card with network programs (`sdcard-net/`)

The W5300 kernel with the programs the network tests run.
`NetworkManager_Test.NedoOsGetsALeaseAndPingsTheGateway` inserts it with the
ZXNETUSB card fitted: `wizcfg.com` gets a DHCP lease from the virtual network,
then `ping -c 1 10.0.2.2` is typed.

| File | Origin |
|------|--------|
| `sdcard-net/SD_BOOT.$C` | `release/sd_boot.$C` (ZX-Evo W5300 kernel, INETDRV=1) |
| `sdcard-net/bin/term.com`, `cmd.com` | `release/bin/` |
| `sdcard-net/bin/wizcfg.com`, `net.ini` | `release/bin/` (network setup; `net.ini` asks for DHCP) |
| `sdcard-net/bin/ping.com` | `release/bin/` |
| `sdcard-net/bin/autoexec.bat` | ours: runs `wizcfg.com`, then echoes `UNREALNGNETREADY` |

Source: https://github.com/alfishe/NedoOS, revision `44049473`, folder `release/`.


# NedoOS CD player (`cdplay/`)

The NedoOS audio CD player for the CD audio test (PLAN #83):
`ZXEvoErs_Test.NedoOsCdplayPlaysAudioTracks` builds an SD card folder from
`sdcard/` plus this program and an `autoexec.bat` that starts it, puts a CUE/BIN
disc with audio tracks into the CD drive (the IDE slave) and plays, pauses and
stops a track with the player's keys.

| File | Origin |
|------|--------|
| `cdplay/cdplay.com` | `release/bin/cdplay.com` ("Audio CD Player", source `src/kapps/cdplay/main.c`: READ TOC, PLAY AUDIO MSF, PAUSE / RESUME, READ SUB-CHANNEL, STOP on the slave drive, NemoIDE or ATM ports) |

Source: https://github.com/alfishe/NedoOS, revision `44049473`, folder `release/`.
