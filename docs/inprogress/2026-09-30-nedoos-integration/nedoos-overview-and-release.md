# NedoOS: what it is and where to get the release

Written 2026-10-04. Part of [NedoOS integration](README.md).

NedoOS is a multitasking operating system for the ZX Spectrum family of clones
with a large memory and a block device (SD card, IDE disk). It runs on the
ATM Turbo 2 and on the ZX-Evo / ATM Turbo 3 board ("BaseConf" in this project
is the ZX-Evo FPGA configuration, model `ATM3`). This page is the short
introduction and the download recipe; the kernel internals are in
[nedoos-kernel-reference.md](nedoos-kernel-reference.md), the planned analyzer
support in [requirements-nedoos-layer.md](requirements-nedoos-layer.md).

## Where to get it

There is one archive, always the latest build, on two mirrors:

| What | Link |
|------|------|
| Release archive (main mirror) | <http://nedoos.ru/images/release.zip> |
| Release archive (second mirror) | <http://kulich.su/images/release.zip> |
| Project site | <http://nedoos.ru/> |

The archive is rebuilt in place (the file name never changes), so a download
is identified by its checksum and the date of the newest file inside, not by
a version number. Our copy of 2026-10-04: 31 016 955 bytes, 6648 files,
SHA-256 `581abee8e2fbb1be3975696e07168b99926d1649ee62e6f3d6a680156d41dc97`,
newest kernels dated 2026-10-03. It lives in the local collection
`~/Downloads/zx-spectrum/os/nedoos/` (outside the repository, like every
source we analyze).

The tests do not use the whole release. They use small slices of it kept in
`testdata/machines/zxevo/nedoos/` (see the README there), taken from the
[alfishe/NedoOS](https://github.com/alfishe/NedoOS) repository, folder
`release/`.

## What is in the release

The archive is a ready SD card: copy the folders and one kernel file to the
root of a FAT card and boot from it.

| Item | Meaning |
|------|---------|
| `sd_boot.$C`, `sd_bootesp.$C` | Kernel for the ZX-Evo board booting from the Z-Controller SD card. The `esp` one drives the ESP network module on the COM port instead of the Wiznet card |
| `osatm3sd.$C`, `osatm3hd.$C` | The same for ATM3 with an SD card or an IDE disk (NemoIDE) |
| `osatm2hd.$C`, `osatm2hdesp.$C`, `osatm2hm.$C`, `osatm2hmesp.$C` | ATM Turbo 2 kernels (ATM IDE; the `esp` ones for the ESP module; the exact meaning of `hm` is not documented in the release) |
| `osp26sd.$C` | Another board's kernel (SD); not used by us |
| `bin/` | 156 programs and files: shell (`term.com`, `cmd.com`), file manager `nc.com`, editor `texted.com`, compilers and assemblers, network tools (`wizcfg`, `ping`, `telnet`, `wget`, the `moon.com` browser, IRC and FTP clients), players (see below), emulators of other machines (`z80.com`, `x86.com`, `vic20.com`), CPU test programs (`zexall.com`, `zexdoc.com`, `z80doc.com`) |
| `doc/` | Manuals (`nedoos_en.md`, `nedoos.txt`, API texts: `api_base.txt`, `api_net.txt`) |
| `ini/` | Settings: network (`network.ini`, `espcom.ini`), file associations (`nc.ext`) |
| `nedodemo/`, `nedogame/`, `downloads/` | Demos, games, a folder for downloaded files |

A `.$C` file is the kernel itself: the loader of the board reads it from the
card and starts it, so exactly one of them is needed in the root, and it must
match the board (ZX-Evo / BaseConf: `sd_boot.$C`).

Other facts from the manual that matter for us:

- Drive letters: `A`-`D` TR-DOS floppies, `E`-`H` IDE master, `I`-`L` IDE
  slave, `M` the Z-Controller SD card, `N` the NeoGS SD card, `O` USB flash.
  The card of the ZX-Evo is therefore `M:`.
- Up to 16 tasks, 8 open FAT files, 8 TR-DOS files, 8 pipes.
- The keyboard on ZX-Evo comes only from the AVR's PS/2 scan codes
  ([ZX-Evo PS/2 keyboard](../2026-09-15-atm-baseconf-highres-ports/tdd-evo-control-and-avr.md) §6.1).
- The manual recommends: Kempston mouse, DDp 4+4+4 palette, Mr. Gluk's RTC,
  ZXNETUSB, General Sound or NeoGS, TurboSound FM.

## Music players in the release

| Program | Plays | Hardware it needs |
|---------|-------|-------------------|
| `ngsplay.com` ("NeoGS Player") | `.S3M`, `.MOD`, `.MP3` from `M:/` folders | NeoGS: it uploads its own driver (`ngsdrv`, `neopg2`) to the NeoGS and the sound card does the mixing |
| `modplay.com`, `ptgs.com` | MOD and PT (per the manual, "NedoPlayer and modplay"; `ptgs` not checked) | not checked |
| `pt.com`, `player.com`, `BBPLayer.com`, `ps6_play.com`, `s98_play.com`, `tgvplay.com`, `rcpplay.com`, `NSChip.com` | other music formats (names only; formats and hardware not checked yet) | not checked |
| `cdplay.com` | Audio CD ([CD audio](../2026-10-02-cd-audio/TODO.md)) | an ATAPI CD drive |

**There is no MoonSound (OPL4) player in the release** (checked 2026-10-04 on
both mirrors: no file or name with "moonsound", "opl4", "mwm", "moonblaster").
Files called `moon*.com` (`moon.com`, `moonua.com`, `moonue.com`) are the
**Moon Rabbit** web browser by Alexander Nihirash (start page
`browser/index.gph`, a Gopher-style menu), unrelated to the sound card. The
player the owner meant on 2026-10-04 is `ngsplay.com`, the **NeoGS** player: its
2026-10-04 build can pause a MOD file and has reworked pause / play buttons.
The MoonSound pack (MoonBlaster `.MWM`, OPL `.VGZ`) is not playable by it; a
MoonSound player would have to be written or ported.

### The two mirrors differ

`kulich.su` serves an older build (33 215 041 bytes, 6656 files, newest
change 2026-10-04 20:10 GMT) than `nedoos.ru` (31 016 955 bytes, 6648 files,
20:30 GMT). The only list difference: `kulich.su` also carries the `*.trd`
kernel images (`osatm2.trd`, `osatm3.trd`, `osp26.trd`, ...). Prefer `nedoos.ru`.

## Booting it on BaseConf (`ATM3`)

The recipe is in [`.recipe/machines/atm/atm3-zxevo-baseconf.md`](../../../.recipe/machines/atm/atm3-zxevo-baseconf.md)
("NedoOS from the SD card"). In short: put `sd_boot.$C` and `bin/` (at least
`term.com`, `cmd.com`, `autoexec.bat`) into a host folder, insert the folder as
the Z-Controller card (`sd.zc`), choose the SD boot in the ERS menu. The kernel
runs `term.com cmd.com autoexec.bat` from `bin/`, then prints the prompt
`M:/bin>`.

The release's own `bin/autoexec.bat` is written for a real board (it starts
`wizcfg.com -S`, then the file manager `nv.com`, then the radio client). For a
test card use a small `autoexec.bat` of our own, as the existing test cards do.

## Running it in the emulator: what we saw (2026-10-04)

ATM3 (BaseConf) with the whole release as the `sd.zc` host folder, the NeoGS
card fitted by the shipped config (`GSType=NGS`):

- **The new kernel boots.** `sd_boot.$C` from 2026-10-03 reaches the shell and
  `nc.com`; `ngsplay.com` starts, uploads its driver to the NeoGS and plays
  MOD files (`allnite.mod` from `M:/downloads/mods`: four DAC channels move).
- **A card with the games folder does not boot.** With `nedogame/` (6245 files,
  67 MB) on the card, NedoOS stops with "SD card lost, Press RESET" (the ERS
  shows its "GO SLEEP, STUPID USER" box before that); the old and the new kernel
  behave the same, so it is the card, not the kernel. Without `nedogame/`
  (about 9 MB, 400 files) it boots. Cause not found yet: the folder volume
  (`HostFolderFat`, FAT16 with an MBR) or the kernel's FAT reader; to bisect by
  file count and size. Until then use a trimmed card.
- **Wait for the player.** After `ngsplay.com` is typed, the NeoGS reset and
  driver upload take about 15 s of emulated time; keys sent earlier land in the
  player's list at random.
- **Automation typing into the shell.** `cd ../downloads/mods` before starting
  the player did not change the player's start folder (it began in `M:/bin`);
  navigate inside the player (`Enter` on `..`).
- A second `unreal-qt` beside one that owns the default ports needs its own
  ports (`UNREAL_WEBAPI_PORT` and friends, `.recipe/_common/setup.md`).

## Related

- [nedoos-kernel-reference.md](nedoos-kernel-reference.md) - memory model,
  kernel calls, symbols.
- [network-adapters-catalog.md](network-adapters-catalog.md) - the network
  hardware NedoOS drives.
- [nedoos-bugs.md](nedoos-bugs.md) - bugs found in the release.
- [2026-09-13-moonsound](../2026-09-13-moonsound/TODO.md) - the MoonSound
  (OPL4) device in the emulator.
