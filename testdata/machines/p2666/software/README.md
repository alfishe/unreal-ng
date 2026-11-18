# Pentagon 2.666 LE: real software

Test programs for the unreal-ng Pentagon 2.666 LE (design:
[docs/inprogress/2026-11-11-pentagon-2666-le/](../../../../docs/inprogress/2026-11-11-pentagon-2666-le/)),
collected 2026-11-12/13. The disk images are taken from the local ZX test collection
(`Data/zx-spectrum/collected/consoles-zx/Tests`, folder named per file below) as unpacked from its archives.
The headless runner `P2666SoftwareProbe_Test.DISABLED_Run`
([p2666_software_probe_test.cpp](../../../../core/tests/emulator/machines/p2666/p2666_software_probe_test.cpp),
environment variables at its top) runs any of them with a key script and saves screenshots.

| File | Content | CRC32 | SHA-256 |
|:--|:--|:--|:--|
| `pentagon-tests.trd` | Jan Bobrowski's Pentagon timing tests: `ltime`, `btime`, `ptime-p`, `stime`, `TINT1.10` | `de7f058f` | `f1450036b80fa962…` |
| `aytest-0.2.trd` | AY/YM test v0.2 (bfox, 2017), the JeRrS TurboSound build | `c0f8b6eb` | `27b13245a6649813…` |
| `km-test.trd` | Kempston mouse sensitivity test (`kmzx0.trd` of km_test) | `67375312` | `5233839497f0940c…` |
| `aydetect.tap` | AY detect: names the sound chip (YM2149 / AY-3-891x / none) | `bc239d13` | `394c1cd63c535221…` |
| `nedoos/sdcard/SD_BOOT.$C` | NedoOS kernel, Pentagon 2.666 SD build (`osp26sd.$C`) | `7514aa50` | `2ca7ea22d1d36bbc…` |
| `nedoos/sdcard/bin/term.com` | NedoOS terminal, started first by the kernel | `611d0937` | `b744f2852b22deee…` |
| `nedoos/sdcard/bin/cmd.com` | NedoOS command line interpreter | `b8745382` | `e477a445e1f55d0a…` |
| `nedoos/sdcard/bin/autoexec.bat` | ours: `echo UNREALNGSDBOOT` | `c2a5a457` | `e62363a0196efbaf…` |
| `nedoos/sdcard/{bin,doc,ini,nedodemo,nedogame}/` | the rest of the NedoOS release: the demo SD card (21 MB, see below) | | |

## Sources

- `pentagon-tests.trd`: `Tests/Pentagon 128K/tests.zip` -> `tests.trd` (655 360 bytes, dated 2017-11-23). The boot
  screen reads "PROTECT REMOVED BY A.I.G 1994". The tests are BASIC programs; Q / A move the tested T-state, the
  defaults are the Pentagon values (`ltime` / `btime` 17761 / 17965, `stime` 14335; `ptime-p` switches the screen,
  `USR 49113`).
- `aytest-0.2.trd`: `Tests/TSFM test/AYtest_v0.2_JeRrS.7z` -> `AYtest_v0.2_JeRrS.trd`. The plain v0.2 is on zxart.ee
  as "AYYMTS test (2017)(bfox)".
- `km-test.trd`: `Tests/Kempston mouse sensitivity test/km_test-main.zip` -> `builds/km_test.zip` -> `kmzx0.trd`
  (sources in the archive's `src/`; the mouse code follows LessNick's CLi-for-WildCommander `mouse.asm`).
- `aydetect.tap`: `Tests/AY detect/aydete.zip` -> `aydete.TAP`.
- NedoOS: <https://github.com/alfishe/NedoOS>, revision `d1d011f8` (2026-06-20), `release/osp26sd.$C`,
  `release/bin/term.com`, `release/bin/cmd.com`. The kernel build is `src/mkpe26sd.bat` (`atm=3`, `sys_npages=128`,
  `NEMOIDE=1`, `KOE`, `NGSSD`).

## The NedoOS demo SD card (`nedoos/sdcard/`)

The whole NedoOS release at `d1d011f8` (`release/bin`, `doc`, `ini`, `nedodemo`, `nedogame`), except the release's
`bin/autoexec.bat` (ours echoes the boot marker and leaves the shell at `M:/bin>`) and five large visual novels
(`nedogame/{Mei,Saku_R,SanShimai,t_no_gogo,tenkosei}`, 46 MB). Insert the folder as the Z-Controller SD card and pick
"SD boot" (key 5) in Gluk. The media manager presents the folder as FAT16 (session writes only, the folder stays as
it is). `bin/` is under the repository's `**/[Bb]in` ignore rule: add new files there with `git add -f`.

### Every program checked (2026-11-14)

All 171 programs of the card were started one boot each through the probe (`autoexec.bat` = `cd` + the program),
on P2666 and, for every one that looked wrong, on the ZX-Evo (`zxevo-ers`, ATM3, 4 MB, NedoOS `sd_boot.$C`) for
comparison:

| Result | Programs |
|:--|:--|
| Works as on the ZX-Evo (132) | the games (Golden Axe, Barbarian, Wolf3D, Worms, Touhou Zero, Robocop, Black Raven, ...), the demos, the tools (`nv`, `nc`, `man`, `pt`, `tp`, `vic20`, `bk`, `scratch`, `cdplay`, `z80full`, ...) |
| Works, after the 7FFD.3 screen page fix | `ufo2`, `ufo2ega` (the game draws in page 7), `zxzvm` |
| Same screen as on the ZX-Evo: waits for keys, arguments or files | `2048`, `balls`, `rotozoom`, `mandelbr`, `showay`, `gfxtest`, `smb`, `more`, `nim`, `texted`, `x86`, `dhrystone`, `asm`, `866towin`, `NSChip`, `player`, `s98_play`, `ps6_play`, `view` |
| Needs a disk in drive A (the RAM disk) | `rdtrd`, `rdtrd2` (they read drive A through TR-DOS; with a TRD inserted both copy it) |
| Needs hardware the board has not | network (ESP / Wiznet): `net1`, `net2`, `zxbatcli`, `zxbatsrv`, `wget`, `getpic`, `myip`, `mrfua`, `3ws`, `updater`, `zxartrad`; General Sound: `modplay` |
| Fails on the ZX-Evo too | `q1_1`, `q1_2`, `yad` |

Board look, not bugs: `tetris`'s grey is 165/255 on P2666 (the board's DAC: a 4-bit component through a 5-bit DAC
whose LSB is 0, hardware-reference.md §6.2) against 202 on the ZX-Evo; `ufo2`'s border is black because the game
itself does `XOR A : OUT (254),A` (the ZX-Evo shows its own red there).

The Z80 test suites on the card (Patrik Rak's z80test 1.2a): `z80full`, `z80doc`, `z80flags`, `z80ccf`, `z80memptr`
pass (2026-11-16). `z80ccf` failed 4 of 160 before the core's Q latch fix on master (84adde805): every model, not a
P2666 matter.

## ATM Turbo disks on P2666

TR-DOS disks for the ATM Turbo 2+ (`testdata/machines/atm/`, the collection's `collected/consoles-zx/ATM Turbo`),
booted through Gluk's "trdos" entry from the RAM disk:

| Disk | Result |
|:--|:--|
| Ball Quest (`ballq_gr.scl`, `BQ   ATM`) | works: EGA title and game |
| `biver16c.trd` (`ATMbiver`, `16Cbiver`) | works: the 16-colour picture in both modes |
| NedoDemo 2, The Board 2, Tuneltro (Gagarin) | work |
| Honey Commander 4.00 / 4.00 fix | work |
| `2048.scl` | the same screen as on the ATM710 |
| `nedoos-cdplay.scl`, `zxbattle.trd` (NedoOS's ATM2 floppy kernel) | work, slowly: the CD player after ~80 s, Battle City after ~10 min (below) |
| `HCaut401.scl` (`HCaut4.0`, Honey Commander with an autoconfig) | falls to the 128 menu, as on the ATM710; works on the Pentagon |

The NedoOS ATM2 kernel writes its palette with `OUT (#FF)` in DOS. On this board that write is also the Beta Disk
system register (hardware-reference.md §4.1, the manual asks for bits 2 and 3 = 1 in palette writes): the kernel's
values reset the controller, and every reset loads a 0.56 s restore before the next command ends. The kernel reads
on, one sector per restore. The P26 kernel on the SD card has no such writes.

ATM CP/M disks (`atm-turbo/cpm-software`, 113 disks) do not boot: they need the ATM BIOS ROM with CP/M, and the board's
ROM set is BASIC 48 / 128, TR-DOS, Gluk and FATALL.

## Why not the latest NedoOS release

The `osp26sd.$C` builds `6e8274ec` (2026-09-04) and `b476771b` (2026-10-16, the latest) stop after
"loading term.com", and not because of the machine: their system page holds the copy of the user kernel (`wasuserkernel`, copied into every new
task's page 0) at `#3A00`, which is FatFs's fixed path buffer (`src/fatfs4os/ff.c`: `#define pathbuf ((unsigned char
*) 0x3a00)`). Opening `term.com` writes the name over the copy, the new task gets "com..." at `#0005` instead of its
BDOS entry, its calls fall into the `getchar` entry, and it ends in the user kernel's `jp $` at `#0030`. The older builds
`d1d011f8` and `af7e2f07` reach the shell here.

## Tests

- `P2666Software_Test.GlukTrDosBootsTheTimingTestsDisk`: Gluk's "trdos" entry boots `pentagon-tests.trd` from the
  RAM disk (the board's default Beta Disk); the boot program lists the tests.
- `P2666Software_Test.NedoOsBootsFromTheSdCard` (long tier): Gluk's "SD boot" starts NedoOS from `nedoos/sdcard/` as
  the Z-Controller card; the shell prompt `M:/bin>` and the `autoexec.bat` marker appear in RAM.
- `aytest-0.2.trd`, `km-test.trd`, `aydetect.tap`: run by hand through the probe (interactive programs, checked on
  screenshots).
