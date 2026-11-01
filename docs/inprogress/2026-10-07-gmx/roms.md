# GMX: firmware images

**Date:** 2026-10-07 - part of [README.md](README.md)

The card's firmware is the **512 KB flash**: the loader, the FPGA scheme files and the ROM planes of every scheme in one
image. The emulator loads it unchanged as one 32-page ROM (the existing `rom.cpp` check) and the plane register picks
a 64 KB window of it ([design.md](design.md) section 2.4).

## 1. What we have

| File | Where | Size | CRC32 | MD5 | What it is |
|:--|:--|--:|:--|:--|:--|
| `data/rom/gmx.rom` | tracked in the repository; also in the Unreal Speccy (zx-evo) `cfg/rom/` folder of the local collection (`tsconf/ft812/materials/emulators/zx-evo-unreal/`, `tsconf/gamedev/unreal-tsfm/`) | 524288 | `00DF8568` | `7911ebbce96ed11e54600508603a32a5` | "GMX Boot Rom 1.2 V. 5.00" in MAME's list (`gmx12500.rom`, SHA1 `dd303d298f96ec4f9fe736eefd3c36fff1dfcdf5`) |

Both local copies are byte-identical to the tracked one.

**Added 2026-10-30:**

| File | Where | Size | CRC32 | MD5 | What it is |
|:--|:--|--:|:--|:--|:--|
| `data/rom/gmx644.rom` | tracked; from the web Unreal Speccy `rom/gmx644.zip` (<https://www.zxby.org/unreal/>, the archive's JSON names it "Scorpion ZS 256 GMX 6.44") | 524288 | `0F84E542` | `3e07fc606947eef4348c79d2bde4a974` | TMgmx Loader V2.00 ("(c) 1997,1998 by Andrew MOA (c) 2024 LW/PLM, Compiled 17 Mar 2024") + "Shadow service monitor v6.44su, #25AB, 21.08.2025" with the FAT32 driver ("ZS-ScorpionFAT32") and the Z-Controller SD path. This is the firmware OSZ requires ("fresh firmware by LW/PLM, about version 40 and up"); it differs from `gmx.rom` in 30 of 32 pages |

The new image is the default for the model (section 5): the test software in `testdata/machines/gmx/` needs it, and
`gmx.rom` stays as the older V1.20 / 5.00 baseline.

### 1.1 Page scan of `gmx644.rom`

From strings and the `LD BC,nnnn` port references per 16 KB page; the flash map in the 1998 manual (`references/`,
section "Flash") gives the same plane split: `#00000` loader, `#02000` start-up scheme, `#04000` working scheme,
`#06000` parameters, `#08000-#0FFFF` four more schemes, `#10000-#1FFFF` standard 48 + 128 + TR-DOS 5.03,
`#20000-#2FFFF` GMX test, `#30000` free, `#40000-#7FFFF` Scorpion with the shadow monitor.

| Plane | Content | Ports referenced |
|:--|:--|:--|
| 0 | page 0: the loader (`DI; JP #00B5`, `OUT (#7EFD),#84`, reads the magic byte through `#78FD`, depacks the main loader to `#5C01`); pages 1-3: FPGA scheme data named "DK_test", "Pentagon", "PentaGON", "Komposit", "Work Sch", and a second copy of "Loader V2.00.334" | `#7EFD`, `#78FD` |
| 1 | Pentagon scheme ROMs: page 0 = Pentagon 128 editor (CRC `124AD9E0`), page 1 = 48 BASIC; pages 2-3 duplicates | `#7FFD` |
| 2 | the GMX test program with its own 8x8 font | all GMX ports |
| 3 | 128 / 48 pages and a page with `/RRL/BOOT.$C`, `/RRL/FASTBOOT.INI`, `/RRL/SHELL.RRL` | |
| 4 | the Scorpion scheme: 128 editor, 48 BASIC, service, TR-DOS; the 128 menu runs here with `#7EFD = #C0` (turbo + plane 4) | `#7EFD`, `#78FD`, `#DFFD` |
| 5 | Shadow service monitor v6.44su and the FAT32 driver | `#78FD`, `#7AFD`, `#DFFD` |
| 6 | utilities ("Copy disk to image", "For run, you need GMX with firmware", NEMO / SMUC strings) | |
| 7 | a second Scorpion plane: pages 0 and 2 = plane 4, pages 1 and 3 differ | |

## 2. Page scan of `gmx.rom` (ours)

Computed with a script over the tracked file: 16 KB pages, CRC32 per page, matched against the other tracked ROMs.

| Plane | Pages 0 / 1 / 2 / 3 | Content |
|:--|:--|:--|
| 0 | loader / test / "PentaGON" scheme data / "Work Sch" scheme data | page 0 starts with the text `TMgmx(r) Loader V1.20 Copyright(c) 1997,1998 by Andrew MOA`. This plane runs at power-on. Pages 1-3 are not ROM code in the Spectrum sense (FPGA scheme files) |
| 1 | `124AD9E0` / `40EF6484` / `BB9B6A8A` x2 | page 0 = the Pentagon 128 editor (the same CRC as `pentagon128k.rom` page 0 and the Profi 128 page), page 1 = 48 BASIC (the same as `48for128.rom`), pages 2-3 = a duplicated service/TR-DOS page |
| 2, 3 | `2B1C36BF`, `619D06C0` x2, `909713A2` | plane 3 is a copy of plane 2 |
| 4 | `F23001FC` / `64D46229` / `5092CCB6` / `1E9B59AA` | the Scorpion plane: page 1 (48 BASIC) and page 3 (TR-DOS) are the same pages as in `scorp_prof401.rom`; page 0 is a 128 editor, page 2 a service monitor |
| 5 | `B1AE8922` / `56BDF612` / `EAE2E4D2` / `67E47FE1` | strings "AutoConfig Ok", "1993-1998 MOA Shadow Service Monitor", "Insert disk, press Y key": the MOA shadow monitor and tests |
| 6 | `8BF9159D` / `107F528F` / `C28B1630` / `78DA834C` | monitor utilities ("Run_prog", "Catalog") |
| 7 | `7C6B1F4D` / `056B29DF` / `AE5088FE` / `5CA2D78E` | data |

The loader enters plane 4 for the Scorpion scheme (2): `OUT (#7EFD),#40`, `OUT (#00),#02`, `JP #0000` (Q1). The page-role
naming in `ROM::GetROMPageRole` is per plane: planes 1, 4 and 7 as editor, BASIC 48, service, TR-DOS (plane 1 is the
Pentagon scheme's), planes 0, 2, 3, 5, 6 as `Specific`.

## 3. Other images known to exist (not in the local collection)

MAME lists these for `scorpiongmx` (all 512 KB, each with CRC32 and SHA1 in
[the driver](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp)): GMX Boot Rom 1.2 V. 5.00 / 5.01,
1.3 V. 5.00; ProfROM + GMX V.5.xx.027, .029, .030, .031, .041.8689 (and "3D2F" and "UNI" variants); V.6.xx.041.8689;
V.6.xx.043.9226 in `s`, `se` (VG93 emulation enabled) and `su` (disabled) forms. MAME's default is 6.43su. The
pico-speccy README names "ProfRom_GMX v5.44.9643" with "TMgmx Loader V2.00". We do not have any of them. A search of the
local collection (file names containing `gmx`, `profgmx`) found only `gmx.rom` and the GMXCOM commander software.
Q16 (a newer firmware) is closed by `gmx644.rom` (section 1), which is newer than MAME's default 6.43su.

Note on MAME's loader offset: MAME places the 512 KB at region offset `#10000` and indexes it in 16 KB entries, the same
view as ours (plane = bits 6-4 of `#7EFD`).

## 4. Other software for tests

`software/commanders/GMXCOM.ZIP` ("GMX Commander v1.0", FION 2002) in the local collection (`software/commanders/`), the
`izzx-git/OSZ` multitasking OS (upstream: [OSZ](https://github.com/izzx-git/OSZ), made for the GMX), and the board test
(scheme 6) inside the flash are candidates for the boot and video tests; none has been run.

Since 2026-10-30 `testdata/machines/gmx/` holds two programs written for the real board (see its README and
[research-2026-10-30-software.md](research-2026-10-30-software.md)): Edge Grinder (`edgegrinder/EGGMX.TRD`) and OSZ
(`osz/OS.TRD`, the SD folder `osz/sdcard/OSZ/`, the web emulator's FAT32 image `osz/osz-sd.zip`).

## 5. Config

`data/configs/gmx/unreal.ini`: `HIMEM=GMX`, `RAMSize=2048`, `[ROM] GMX=rom\gmx644.rom` (the key `[ROM] GMX=` is already
read by `config.cpp`), no `RESET=` choice: every reset starts the loader, which enters the 128 menu (Q1). The web
Unreal Speccy's GMX model uses `HIMEM=PROFSCORP, RAMSize=2048, CMOS=DALLAS, SMUC=1, RESET=MENU, GMX=1` and the image as
`[ROM] PROFROM=` ([research-2026-10-30-web-unreal.md](research-2026-10-30-web-unreal.md)).
