# Wild Sound (Robus) - software, music, docs found online and locally

Date: 2026-10-09. Read-only research (gh CLI, curl, WebFetch). Nothing was executed.

Abbreviations: WS = Wild Sound; LOCAL = `~/Downloads/zx-spectrum`; WSREPO = LOCAL `sprinter/peripherals/wild-sound/Wild-Sound`
(a clone of https://github.com/RobusAsmCoder/Wild-Sound, single commit `edd3b11`, 2021-07-14, no LICENSE file).

## 0. Bottom line

- **The only known Sprinter (DSS) program for the ISA Wild Sound is `prosiak.exe`.** It is on the MAME-pack system disk
  as `DEMOS/WILDSND/prosiak.exe` (150 003 bytes, sha1 `250332da6967a5389671af058bc06737b5618154`). The CHD has already
  been converted (LOCAL `sprinter/hdd/sp_hdd_sys.img`), and the file is extracted at
  LOCAL `sprinter/peripherals/developer-interest/disk-notes/prosiak.exe`. `WILDSND/` holds no other file. The EXE is packed
  (header `EXE\0`, mostly compressed). Readable strings: `Wild Soun`, ` ISA #`, `Not F...`, `XM Init`, `TRK Data`,
  `HDR`, `PLAY`, `Enable `, `Prosiak`. It is a self-contained XM demo: it uploads the PROSIAK.XM song to the card and plays it.
  No source code for it was found anywhere, including the GitHub repo, GitLab sprinter-computer and the forums.
- The 2020 ZX Spectrum (TR-DOS) equivalents are complete and their sources are in WSREPO: **MasonXM** (a PC converter
  from XM to the card's format), a TR-DOS boot/loader, and **4 XM music disks** (+2 more that only exist in the repo).
  They target an AYX-32 board flashed with WS firmware, through the AY ports #FFFD/#BFFD.
- No XM/MOD/S3M player for the card exists apart from the per-song "linked" disks/EXE. There is no generic
  "Wild Sound player", no "WS player", and no flasher for the Sprinter. The flasher is the TR-DOS ay-config (`ayx32_fw_*.trd`),
  or the TS-Conf AYX32_Tool.
- Not on zxart, demozoo or pouet (searched "wild sound", "wildsound", "ayx", "masonxm": no productions). The public
  zx_sprinter Telegram preview cannot be searched. The zx-pk.ru thread asking for protocol docs (see §3) has no answer.

## 1. Programs that use the card

| Item | Platform | Where | Size | Usefulness for testing |
|:--|:--|:--|:--|:--|
| `prosiak.exe` (WS-ISA XM demo, Robus) | Sprinter DSS | MAME pack `IMG/sp_hdd_sys.chd` -> `DEMOS/WILDSND/prosiak.exe`; local copy above | 150 003 | **Primary target.** Exercises the ISA interface, the upload of instruments and patterns, XM init and play. Without docs it has to be reverse-engineered (unpack it first: the LZSA depacker is in WSREPO `TMT/MasonXM/BOOT/LZSA`, `DOS/DOSLZSA.AZ8`). |
| `prosiak.trd`, `BALLADAL.trd`, `checkno.trd`, `CONSUL.trd` (TRD-XM disks) | ZX + AYX-32 with WS firmware | Google Drive links in [tslabs t=883](https://forum.tslabs.info/viewtopic.php?f=31&t=883): [Ballada](https://drive.google.com/open?id=1N8lLSKk19H14X1wMgdRfey0gGm1HUoeK), [Prosiak](https://drive.google.com/open?id=1txfHAS7evPQVBh3y2n7s6e0aN7lzL-X9), [Checkno](https://drive.google.com/open?id=1SZnIXytwLmavxZrG1AXMp1csWVqhhi67), [Consul](https://drive.google.com/open?id=1KhoSC-prIejzskKUMOJnTPA2z5bRPHDK) | 655 360 each | **Byte-identical** to WSREPO `TMT/MasonXM/BOOT/LINKER/*.trd` (sha1 checked), so they were not kept as separate downloads. Each disk has a `boot` BASIC file, then a loader (`boot` L/I) that streams packed XM data straight from tracks. They are the best ZX-side test: the uploader source is in the repo. |
| `DISCHARZ.trd`, `FASTER-2.trd` | same | only in WSREPO `.../BOOT/LINKER/` | 655 360 each | Two more linked songs that were never posted. |
| MasonXM (`MASONXM.exe`, `MasonXM.pas`, `RobXM.pas`, `PER.pas`) | Win32 (TMT Pascal) | WSREPO `TMT/MasonXM/` + `MasonXM_RELEASE.zip` | exe 34 304 | It converts XM into `*.xmInsPack*` / `*.xmPatPack*` (32 KB chunks) and links them with the Z80 loader. **The pack format and the upload commands are defined here.** It is the main source for the card's XM command protocol. |
| `XMPLOAD.AZ8` / `LOADER.AZ8` / `LOADOS` / `LOAEXE` / `DETECT.AZ8` | Z80 (ASAM syntax) | WSREPO `TMT/MasonXM/BOOT/` | small | The Z80 side: AY register select/write through #FFFD/#BFFD, detection of 1/2 AYs. It is ZX-only (TR-DOS). `LOAEXE` is a TR-DOS "EXE" restarter, not DSS. |
| ay-config firmware flasher disks `ayx32_fw_WS_*.trd`, `ayx32_fw_WILDSOUND.trd`, `ayx32_fw_TSL_2017.trd`, `ayx32_fw_698.trd`, `ayx32_fw_buryak_00{0,1,2}.trd` | ZX TR-DOS | WSREPO `TMT/ay-config/`; the 4 WS builds + TSL_2017 are also attachments on [tslabs t=883](https://forum.tslabs.info/viewtopic.php?f=31&t=883) (`download/file.php?id=2169/2160/2159/2157/2156`) | 655 360 each | They flash the card through its own register protocol (the TSL boot/flash commands). Useful to test the firmware-update command path. The latest is `WS_16593123042021` (2021-04-23, repo only). The tslabs `id=2169` download is identical to the repo copy. |
| AYX32_Tool v1.12 (`.spg`) | TS-Conf | [tslabs t=892](https://forum.tslabs.info/viewtopic.php?f=35&t=892), `download/file.php?id=2196` | 22 528 | A TSL config/flash utility (a port of ay-config). It is for the TSL protocol. The WS firmware claims to keep the flash part compatible. **Downloaded.** |
| Wild Player v0.333 (Budder) | ZX | LOCAL `software/music/multisound/general-sound/` | - | **Not related** (a GS/AY/TS/FM player). It only shares the name. |

Fetching tip: forum.tslabs.info attachments need `curl --http1.1` (HTTP/2 fails with exit 92). Google Drive files download from
`https://drive.usercontent.google.com/download?id=<id>&export=download`. The source archive id `1hk8-NR1f8XvOI1rLdCEMfwSY0Jlti2Mt`
returns HTTP 500, which is fine because the GitHub repo holds the same sources.

## 2. Music for the card

- **XM originals used with the card**: WSREPO `TMT/MasonXM/Music/`: `PROSIAK.XM` (177 815; also `PROSIAK0/1.XM`, an
  autosave copy), `BALLADAL.XM` (55 548), `CHECKNO1.XM` (144 765), `CHECKNO2.XM` (144 490), `CONSUL.XM` (181 771),
  `DISCHARZ.XM` (359 957), `FASTER-2.XM` (132 457), `DARKNESS.XM` (498 936), `Sleep.xm` (243 112), `STINK-B_.XM`,
  `TEXS-3.XM`, `TREXZ.XM`, `EFF00.XM` (126 097, probably an effects test). Next to them are the MasonXM outputs
  (`*.xmInsPack00..NN`, `*.xmPatPack00..NN`). **They are ideal golden data:** the same XM goes through MasonXM, then the
  card, then the expected audio, which can be compared with a reference XM renderer (libxmp/OpenMPT).
- **MASON AY music by Robus** (zxart author id 19741, "Aleksey Fill", Kyiv, 42 tunes): https://zxart.ee/eng/authors/r/robus/.
  The `.ay` files in ZXAYEMUL format, built by the MASON player, include TurboSound ("+ 2xAY") versions. Downloaded
  `Robus_-_Abrove_MASON_4songs_TurboSound.ay` (37 384 bytes; 4 subsongs: Abrove Music, Abrove Music + 2xAY, Abrove Speed,
  Abrove Speed + 2xAY). This tests the WS "MASON AY / TurboSound" path. Other TS tunes on the same page: 3123 Years Of
  Di Halt + 2xAY (DiHalt 2013), Drink For Peace (3BM 2014), Heart Megademo parts, etc. Also downloaded `DiHalt_RealTime_2019.may`
  (19 020 bytes, header `MASON-AY-213`, a raw MASON module from DiHalt Lite 2019): it is the native tracker format, a
  possible input for the card's internal "Mason AY player".
- MASON itself (Robus's tracker, 1997): https://zxart.ee (prod 251050, "System Software/Sound/Digital Music Editors").
  Robus says "press COMPILE in MASON and the module plays on Wild Sound" ([zx-pk t=21431](https://zx-pk.com/forum/viewtopic.php?t=21431)).
- Videos: [AY-AYX32 - Wild Sound II - Digital Player on ZX-Spectrum](https://www.youtube.com/watch?v=owNqmXftsiA) (2020 XM player);
  [2007 XM on a C8051F410](https://youtu.be/_QBdUcxZQy0) (the predecessor). They are a useful audible reference for "what it should sound like".

## 3. Documentation, protocol, forums

| Source | What it has |
|:--|:--|
| [forum.tslabs.info t=883 "AYX-32 Wild Sound II"](https://forum.tslabs.info/viewtopic.php?f=31&t=883) (1 page, 12 posts, from 2020-01-21) | The feature list: hardware TurboSound, software QuadroSound (AYs 3 and 4 emulated), Phase/PWM through MASON, a 111 kHz update rate with no filters, an internal MASON AY player, a MASON digital (XM-like, incomplete) player that runs in parallel with the AY, instrument memory in the order SRAM -> CCMRAM -> FLASH (~128K+64K+1M), a UART loader, SWO, ZXUNO-RS232 support (build 03541128012020). There is **no register map**. The source tree is the GitHub repo. |
| [forum.tslabs.info t=892 "AYX-32 Configuration Utility"](https://forum.tslabs.info/viewtopic.php?f=35&t=892) | AYX32_Tool for TS-Conf. The hardware thread is [t=687](http://forum.tslabs.info/viewtopic.php?f=6&t=687). |
| [zx-pk.com t=21431 "Платы Sprinter 2022d"](https://zx-pk.com/forum/viewtopic.php?t=21431) (local copy LOCAL `sprinter/peripherals/web/zx-pk-com-t21431-*.html`) | The ISA Wild Sound sale (500 rub), Robus's history (Wild Speccy ~2000: WildFDD, WildCOMM, Wild Sound 2003), and the statement "WS-ISA = AYX-32 firmware + lots of memory + the ISA interface; one firmware detects the host and flashes all three devices". AY data goes to the card's ISA addresses instead of the AY ports, and 4 AYs are possible. Modes: AY / Phase / Synthesia / Digital. **No port numbers.** Board photos are at i.ibb.co (listed on that page). Support is through https://t.me/zx_sprinter. |
| [zx-pk.ru t=27413 "AYX-32 - звуковая карта в слоте АУ"](https://zx-pk.ru/threads/27413-ayx-32-zvukovaya-karta-v-slote-au.html) (5 pages) | The original TSL AYX-32 feature list (32-channel wave synthesis, an XM player, 928 KB). On p2 someone asks for "a description of the protocol for samples and XM": **no answer**. On p5 (2022) a user asks how to use Wild Sound, and the reply is that the planned TSL revision never happened. |
| [github.com/tslabs/arm `AYX-32/`](https://github.com/tslabs/arm/tree/master/AYX-32) | The TSL firmware source. `Firm/bus/registers.hpp` = **the AYX-32 register map** (PSG 0x00-0x0F, extended PSG 0x10-0x18 per-channel L/R volume + duty, DAC 0x40-0x46, WS ("wave synth") 0x50-0x55, device control 0xD0-0xD9, ...). `Firm/bus/command.cpp` (commands), `Firm/sound/ws.cpp` (wave-synth channels), `Info/ayx-32.xls` (the register spreadsheet), `Tool/ay-config/src/` (Z80 C flasher). It is the base protocol that the WS firmware claims to stay compatible with. |
| [tslabs/zx-evo `pentevo/unreal/Unreal/sound/ayx32.cpp`](https://github.com/tslabs/zx-evo) (also tslabs/zx-evo-unreal) | The existing AYX-32 emulation in TSL's Unreal Speccy. **It is the closest reference implementation** (for the TSL firmware, not WS). |
| [alexanderk23/ayx](https://github.com/alexanderk23/ayx) | An AYX-32 DIP28 KiCad board (schematic PDF). No firmware. |
| WSREPO `STM_AYX32_TSL/STM_AYX32_TSL_WILDSOUND/SRC/` (`ChipAY_WS_Process.c`, `ChipAY.h`, `process.c`, `ChipAY_BootFlasher.c`, `TestCase.c`) | **The WS firmware source**: the ground truth for the WS command set. `DOC/` holds AY datasheets, XM format docs (`XM.TXT`, `XI.TXT`, FT2 PDFs, `Micromod.pas`), a schematic folder, and BORN #02 (SCL). |

Robus's GitHub repos (`gh api users/RobusAsmCoder/repos`):

| Repo | Description | Sprinter / WS code? |
|:--|:--|:--|
| [Wild-Sound](https://github.com/RobusAsmCoder/Wild-Sound) | "Wild Sound II the emulator of 4xYM2149 chips include TurboSound and Digital Player of MOD & XM"; 1 758 files, pushed 2021-07-14 | WS firmware (Keil, STM32F405), MasonXM, the ZX loaders, TRD disks, XMs. **No Sprinter/ISA/DSS code** (grep for sprinter/estex/dss: nothing). It predates the ISA board. |
| [Wild-FDD](https://github.com/RobusAsmCoder/Wild-FDD) | FD179X emulator (C8051F340 + Quartus) | no |
| [SiLabs-Programmer](https://github.com/RobusAsmCoder/SiLabs-Programmer) | SiLabs C2 programmer on STM32 | no |
| [STM_F4_DISCOVERY_SPI](https://github.com/RobusAsmCoder/STM_F4_DISCOVERY_SPI) | SPI testing on STM32F4 Discovery | no |
| [GamePad-Reader](https://github.com/RobusAsmCoder/GamePad-Reader) | PC gamepad-to-keys tool (Pascal) | no |
| [Abrove](https://github.com/RobusAsmCoder/Abrove) | "ZX-Spectrum Abrove Project", README only | no |

## 4. Sprinter software archives

| Archive | WILDSND / WS content? |
|:--|:--|
| MAME pack `~/Downloads/mame_release_v306_25.05.2025/IMG/sp_hdd_sys.chd` (96 MB, already converted to `sprinter/hdd/sp_hdd_sys.img`; no `chdman` on PATH now) | **Yes: `DEMOS/WILDSND/prosiak.exe` only.** Its listing is in `sprinter/network/local-analysis/sys-img/listing.txt`. |
| MAME pack `sp_hdd_media.chd` | No WS content. It has 3 ProTracker MODs: `MEDIA/1974_1.MOD` (126 900), `MGL.MOD` (195 912), `XTD.MOD` (186 510), used by witchcraft2001/modplay (Covox). They are possible MOD test data. |
| ZXMAK2 `sp_disk1.vhd` (DSS 1.62) | No WILDSND. It has BIN/PT3PLAY.EXE, PROPLAY, WAVPLAY. |
| [gitlab.com/sprinter-computer](https://gitlab.com/sprinter-computer) (hard, apps, dos, bios; all 302 paths listed) | None: no wild/ayx/xm/sound files. |
| [Tolik-Trek/sp_mame](https://github.com/Tolik-Trek/sp_mame), [witchcraft2001/mame_sprinter](https://github.com/witchcraft2001/mame_sprinter) | MAME forks with no WS device. Other Tolik-Trek repos: CDX, disktest, 2D-Studio (none WS). |
| witchcraft2001 repos (DemoN, flexnavigator, gfxview, modplay, ...) | None WS. |
| app.sprinter.ru (local snapshot 2026-10-02), nedopc.org Sprinter threads (local dumps in `sprinter/peripherals/web/`) | No mention of Wild Sound. |

## 5. Free test modules (MOD/S3M/XM) and AY/PT3 tracks

| Set | Licence | Notes |
|:--|:--|:--|
| OpenMPT `test/test.mod` (5 400), `test.s3m` (880), `test.xm` (6 279) - https://github.com/OpenMPT/openmpt/tree/master/test | BSD-3-Clause | Tiny feature-coverage modules. Good loader smoke tests. |
| libxmp `test-dev/data/` - https://github.com/libxmp/libxmp/tree/master/test-dev/data | MIT | 155 small MOD/S3M/XM files (320 B - 2 KB typical): FT2 quirk cases (`ft2_k00_defaults.xm`, `xm_portamento_target.xm`, `pattern_loop_*`, `pattern_jump_*`, `finefx_ft2.xm`, ...), plus `Gaffeltruck.mod` (196 KB). Best for checking effect semantics against WS's "incomplete XM" player (Robus says there is no vibrato and no track panning). |
| OpenMPT wiki test cases - https://wiki.openmpt.org/Development:_Test_Cases/XM (and /MOD, /S3M) | freely downloadable test cases (no formal licence) | They are the source of many libxmp cases. Each module covers one quirk. |
| Robus's XMs in WSREPO (§2) | no licence file (owner: no license questions) | Real-world songs that are known to work on the hardware. |
| LOCAL `zx-next/software/apps/audio/NXModPlayer/mods/` (`dune_ecolove.mod` 153 424, `Jarresque.mod` 98 150, `AWESOME5.MOD` 84 202, `KEF2.MOD` 109 936) | unknown (shipped with NXModPlayer) | MOD set already on disk. |
| AY/PT3: LOCAL has 172 `.pt3` files (e.g. `zx-next/source-code/em00k-pt3driver/tune.pt3`, `zx-next/demos/tbblue-distribution/ts-songs/*.pt3`: TurboSound PT3 songs such as `BrD.pt3`, `summer.pt3`, `bittefallos.pt3`); Robus's MASON `.ay` (downloaded, §2) | scene releases | The TS PT3s exercise the dual-AY path, and the MASON `.ay` exercises the WS-native player. |

## 6. Downloads saved

`~/Downloads/zx-spectrum/sprinter/peripherals/wild-sound/downloads/`:

- `forum.tslabs.info/AYX32_Tool_v1.12/AYX32_Tool_v1.12.spg`: 22 528 bytes, sha256 `23847deb...b977b8`
- `zxart.ee/robus-91986/Robus_-_Abrove_MASON_4songs_TurboSound.ay`: 37 384 bytes, sha256 `96916fe4...6238bb`
- `zxart.ee/robus-308023/DiHalt_RealTime_2019.may`: 19 020 bytes, sha256 `2373eb26...b3b2fe`

These were not saved because they are identical to files already on disk: the 4 Google-Drive TRD-XM disks (= WSREPO `BOOT/LINKER/*.trd`),
the tslabs firmware TRDs (= WSREPO `TMT/ay-config/*.trd`), and `prosiak.exe` (already in `developer-interest/disk-notes/`).
