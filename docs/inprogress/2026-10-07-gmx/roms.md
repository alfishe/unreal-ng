# GMX: firmware images

**Date:** 2026-10-07 - part of [README.md](README.md)

The card's firmware is the **512 KB flash**: the loader, the FPGA scheme files and the ROM planes of every scheme in one
image. The emulator loads it unchanged as one 32-page ROM (the existing `rom.cpp` check) and the plane register picks
a 64 KB window of it ([design.md](design.md) section 2.4).

## 1. What we have

| File | Where | Size | CRC32 | MD5 | What it is |
|:--|:--|--:|:--|:--|:--|
| `data/rom/gmx.rom` | tracked in the repository; also in the Unreal Speccy (zx-evo) `cfg/rom/` folder of the local collection (`tsconf/ft812/materials/emulators/zx-evo-unreal/`, `tsconf/gamedev/unreal-tsfm/`) | 524288 | `00DF8568` | `7911ebbce96ed11e54600508603a32a5` | "GMX Boot Rom 1.2 V. 5.00" in MAME's list (`gmx12500.rom`, SHA1 `dd303d298f96ec4f9fe736eefd3c36fff1dfcdf5`) |

Both local copies are byte-identical to the tracked one. Nothing is copied into the repository by this folder.

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

Which plane the loader enters for the Scorpion scheme (2) is a runtime fact (the loader writes `#7EFD`). The page-role
naming in `ROM::GetROMPageRole` is therefore per plane, with plane 0 and 5-7 as `Specific` and planes 1-4 as editor,
BASIC 48, service, TR-DOS (to be confirmed by a boot trace in phase 1; Q1).

## 3. Other images known to exist (not in the local collection)

MAME lists these for `scorpiongmx` (all 512 KB, each with CRC32 and SHA1 in
[the driver](https://github.com/mamedev/mame/blob/master/src/mame/sinclair/scorpion.cpp)): GMX Boot Rom 1.2 V. 5.00 / 5.01,
1.3 V. 5.00; ProfROM + GMX V.5.xx.027, .029, .030, .031, .041.8689 (and "3D2F" and "UNI" variants); V.6.xx.041.8689;
V.6.xx.043.9226 in `s`, `se` (VG93 emulation enabled) and `su` (disabled) forms. MAME's default is 6.43su. The
pico-speccy README names "ProfRom_GMX v5.44.9643" with "TMgmx Loader V2.00". We do not have any of them. A search of the
local collection (file names containing `gmx`, `profgmx`) found only `gmx.rom` and the GMXCOM commander software.
Obtaining a newer firmware is Q16; the tracked V1.2 image is sufficient for phase 1.

Note on MAME's loader offset: MAME places the 512 KB at region offset `#10000` and indexes it in 16 KB entries, the same
view as ours (plane = bits 6-4 of `#7EFD`).

## 4. Other software for tests

`software/commanders/GMXCOM.ZIP` ("GMX Commander v1.0", FION 2002) in the local collection (`software/commanders/`), the
`izzx-git/OSZ` multitasking OS (upstream: [OSZ](https://github.com/izzx-git/OSZ), made for the GMX), and the board test
(scheme 6) inside the flash are candidates for the boot and video tests; none has been run. Disk images are not copied
into the repository; tests that need them use `testdata/` paths and skip when the file is absent, as the Profi tests do.

## 5. Config

`data/configs/gmx/unreal.ini`: `HIMEM=GMX`, `RAMSize=2048`, `[ROM] GMX=rom\gmx.rom` (the key `[ROM] GMX=` is already read
by `config.cpp`), `RESET=` the loader default (the first run decides whether `SYS` or `128` is right; Q1).
