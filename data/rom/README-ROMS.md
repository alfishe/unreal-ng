# ROM images

The files in this directory (and `data/testrom/`) are firmware images of ZX Spectrum machines and clones.
They are **not** part of the unreal-ng source code and are **not** licensed under the GPL. They are included so
that the emulator works out of the box, on the following basis:

* **Sinclair / Amstrad ROMs** (`48.rom`, `128.rom`, `128_low.rom`, `plus2.rom`, `plus2a.rom`, `plus3.rom`,
  `plus341.rom`, `1982.rom`, `sos.rom`, `48for128.rom`, `service.rom`, `if1.rom`, and the 48K ROM font bitmap in
  `core/src/debugger/analyzers/rom-print/zxspectrumfont.h`): Amstrad plc, the copyright holder, has given
  permission (Cliff Lawson, Amstrad, comp.sys.sinclair, 1999) for the Spectrum ROMs to be distributed with emulators
  free of charge, provided the copyright is acknowledged and no fee is charged for the ROMs themselves.
  Amstrad copyright is hereby acknowledged.
* **TR-DOS** (`trdos.rom`, `trdos503.rom`, `trdos504t.rom`, `trd504tm.rom`, `dos.rom`, `dos6_10e.rom`, `128tr!.rom`):
  Technology Research Ltd (no longer trading). Distributed with emulators by long-standing custom; no formal permission exists.
* **Pentagon, Scorpion, KAY, ATM, Profi, ZX Evolution / TS-Conf, General Sound and other clone firmware**
  (`pentagon*.rom`, `glukpen*.rom`, `scorp*.rom`, `kay1024*.rom`, `atm*.rom`, `glukatm.rom`, `profi.rom`, `profi/*.rom`,
  `zxevo.rom`, `zxevo-fe.rom`, `ts-bios*.rom`, `gs*.rom`, `bootGS.rom`, `lsy256.rom`, `qu7v42.rom`, `qc_3_05.rom`, `madrom.rom`,
  `zxi1.rom`, `2006.rom`, `xbios135.rom`, `sgen.rom`, `gd.rom`, `gmx.rom`, `1993.rom`, `ZXM-Phoenix_bios.bin`,
  `tk90.rom`, `tk95.rom`): property of the respective clone manufacturers and authors, distributed freely in the
  ZX Spectrum community. ZX Evolution firmware sources are published by TSLabs.
  `zxevo-fe.rom` is the official NedoPC BaseConf image `rom/zxevo_fe.rom` of the pentevo repository
  (md5 `6e2900206aea5505cddc89963914a300`: EVO Reset Service 0.60.05 FE, NEO-DOS in page 29, the image
  the released `base_trdemu` FPGA expects); `zxevo.rom` is an older custom image (TR-DOS 5.04T in pages
  0-3, earlier ERS) kept for the legacy BaseConf FPGA and TS-Conf.
* **NeoGS flash image** (`neogs/full_ngs.rom`, NeoGS flash v1.11: loader, main ROM and FPGA configuration): built
  from the NedoPC `ngs` sources (http://nedopc.com/gs/ngs_eng.php), which carry no licence file; treated as MIT
  like the rest of the NedoPC NeoGS material. The parts it is packed from are in `tools/neogs/parts/`, and
  `tools/neogs/pack_flash.py` checks the image against them.
* **General Sound ROM 1.05b** (`gs105b.rom`): the firmware of the ZX-MultiSound card's General Sound
  (`GSProfile::MultiSound`, `core/src/emulator/sound/chips/gs/gsprofile.h`), "Version 1.05b" (1.04 Beta with
  bug fixes by psb and Evgeny Muchkin, 2007, 2015; sources: [psbhlw/gs-firmware](https://github.com/psbhlw/gs-firmware)).
  Copied unchanged from `rom/gs105b.32K.rom` of the card repository
  [UzixLS/zx-multisound](https://github.com/UzixLS/zx-multisound/tree/85656da/rom) (commit `85656da`, MIT license),
  32 768 bytes, SHA-256 `9948ec9617365fb1913b0c56e00d5f6b6a36084875cca16debae270a4c6e6adf`. The repository's
  `gs105b.64K.rom` (for a 27C512, SHA-256 `d03249af73741b775bb3719eaaf8a97f58a0863939e990240139b122fae0c517`) is
  this image twice and is not included. General Sound firmware by Stinger (1997), distributed freely like the
  other `gs*.rom` images above.
* **Peters Plus Sprinter Sp2000 BIOS** (`sprinter/sp2k-3.04.rom`): BIOS 3.04 build 253 of 17.06.2003, the last
  Peters Plus build, 262 144 bytes, CRC32 `1729cb5c` (the value MAME's `sprinter` ROM set uses), SHA-256
  `e166d1557f699cedb65481e784e7f0f17c7b0cdbb6851bf2ed0c4dbf5448de95`. Copied unchanged from
  `fw/bios/sp2k-3.04.253.bin` of the Sprinter board repository (https://zxgit.org/Sprinter/2000). Peters Plus Ltd
  firmware, distributed freely in the Sprinter community (the BIOS 2.17 sources were released in 2009 and are
  believed public domain). ROM pages: 0 = disk drivers and the packed SETUP, 8 = the BIOS proper, `#C`-`#F` = the
  PLD loader and bitstream, the rest empty; annotated listings in `docs/disasm/rom/sprinter/`. A known variant
  (ZXMAK2 `SP_304.BIN`, CRC32 `a3970620`) differs in 5 bytes: the page 0 checksum and the page 8 board-id byte.
  MAME's community BIOS 3.05 / 3.06 images (CRC32 `fe1c2685` / `187f4382`) are not included: no copy is public
  (Tolik-Trek `Sprinter-BIOS` publishes sources only, no releases); take them from the MAME `sprinter` ROM set.
* **Community Sprinter BIOS builds** (`sprinter/sp2k-3.06-hf2.rom`, `sprinter/sp2k-3.07-beta1.rom`): built here from
  the sources of Anatoly Belyansky (Tolik-Trek), https://zxgit.org/Tolik-Trek/Sprinter-BIOS (no license file;
  treated as MIT per the project rule for unlicensed sources), with `tools/machines/sprinter/bios-build/make-bios.py` and sjasmplus
  1.21.1. 262 144 bytes each, the 1K30 board image. Pages: 0 ROM + SETUP, 1 logo, 2-4 the ZX ROMs (128, 48,
  TR-DOS), 5-7 and 9-11 the recovery ROM disk (DSS), 8 the BIOS, `#C`-`#F` loader + bitstream.
  * `sp2k-3.06-hf2.rom`: "Firmware v3.06 Hotfix 2", Release 19.01.2026, branch `master` commit `c14a8c5`, build
    date fixed to 2026-01-19; bitstream "Core 1K30 v3.05" taken from `Build/Bin/LOADER_K30.BIN` of commit
    `4c5d44a` (the tree itself has none). CRC32 `9aa7bb29`, SHA-256
    `fc910ba4c32f42a8b130b804434f3b449f2f01ed710510a9e8340d8d3ae9a90a`. The shipped default of
    `configs/sprinter/unreal.ini` (`[ROM] SPRINTER=`) since 2026-10-03.
  * `sp2k-3.07-beta1.rom`: "Firmware v3.07 BETA 1", branch `beta` commit `f546c4e` (2026-09-24), build date
    2026-09-24 12:00:00, its own bitstream. CRC32 `a06a1a02`, SHA-256
    `3745a845e3729189fc5a9590a6c3d5e0dff5ee02da98d120b4a5bb3905505c22`. It is the public head of the
    upstream `beta` branch as of 2026-10-03. It was the shipped default from 2026-10-02 to 2026-10-03.

  No author's build of these versions is public to compare against; how they were made, the boot results and
  how to follow new builds: `docs/inprogress/2026-09-28-sprinter/bios-versions.md`.
* **ZX Profi factory firmware** (`profi/*.rom`): the stock 64K images of the two board families, each four 16K
  pages in the order SYS (BIOS), TR-DOS, 128 BASIC, 48 BASIC. Downloaded unchanged from
  [speccy4ever](https://speccy4ever.speccy.org/_PR.htm), which names each file by its CRC32; every CRC32 below
  matches its name there. `profi.rom` one level up is not a factory image (BIOS 2.0 with TR-DOS 6.08 and the
  STS 3.2 monitor, speccy4ever `PB20POS-A932676F`) and stays the default.

  | File | Board | Contents | CRC32 | MD5 | Also shipped by |
  |:--|:--|:--|:--|:--|:--|
  | `kramis-v02.rom` | v3 | JV "KRAMIS" BIOS V0.2 (10.1990), TR-DOS 5.03 | `77327F52` | `720f23bec22581f37fb55bd7a928fde2` | xpeccy-plus `profi-kramis02.rom` |
  | `kramis-v03.rom` | v3 | "Computer Profi" BIOS V0.3, TR-DOS 5.04T | `D7023609` | `dba4ef54a1588daa68290950be2ebf52` | ZXMAK2 `PROFI_v03.ROM` |
  | `bios10-930505.rom` | v5 | Micco Software ROM Bios 1.0 of 05.05.93, TR-DOS 5.04T | `FA9F090A` | `56ae7befeff52eba82dadef305adc912` | - |
  | `bios10.rom` | v5 | ROM Bios 1.0 of 21.09.93, TR-DOS 5.04T | `E95F7AA0` | `1dfad1fb6303d0db01fd87c1cff05039` | ZXMAK2 `profi_v10.rom` |
  | `bios10-kondor504.rom` | v5 | as `bios10.rom`, read off a Kondor 5.04 board; the 48K page's NMI test at `#006D` is `JR Z` (`#28`), not `JR NZ` | `10DA289A` | `f4ab0dd91cd7d207879767d4fe5bf30e` | xpeccy-plus `profi-bios10.rom` |
  | `bios20.rom` | v5 | ROM Bios 2.0 of 17.04.94, TR-DOS 5.04T | `36F5F7BD` | `02877e403f22d10d12ef0296ccb96f60` | xpeccy-plus `profi-bios20.rom`, ZXMAK2 `PROF-M.ROM` |
  | `bios20-font.rom` | v5 | as `bios20.rom` with one glyph of the 48K font changed (`#3D99`-`#3D9A`) | `DA81DED7` | `2f7549cd9fff863f68867f7d944de2a1` | - |
  | `bios-plus-041h1.rom` | v5 + V0.03 decoder (`PROFI-PLUS`) | not factory: ROM-BIOS PLUS 0.41h1 by Vadim (C) 1998-2025, "with patched RTC", for PQ-DOS; needs the extended ports in the SYS ROM (`[PROFI] ExtPorts=sys`). From [Karabas-Pro](https://github.com/andykarpov/karabas-pro) `firmware/src/fpga/profi/rom/bios_pqdos.rom` (commit `0c1bd2f`, 2026-02-15) | `594E10FA` | `1246daf2605704131b5abb239c6115bb` | pico-spec `src/roms/profi/` |

* **ZX Profi PROFI-XT keyboard controller** (`profixt/profi-xt-v1.27.rom`): firmware "JV KRAMIS (C) 28.10.1992
  vers 1.27" of the 8035 on the PROFI-XT board, **reconstructed**: the only known dump (speccy4ever
  `PROFI_XT-9A8E2686.ROM`) never enables interrupts and so receives no key; 5 bytes at `02Eh`-`032h` are replaced.
  The patch, why, and how it was checked: [`profixt/README.md`](profixt/README.md). CRC32 `59C7A98C`.
* **Open firmware**: `gdos-pd.rom` (public domain), `opense.rom` (OpenSE BASIC, GPL),
  `data/testrom/zx-diagnostics.rom` (Brendan Alford, GPL-3.0).
* **YRW801 wave data ROM** (`opl4/yrw801-m-yamaha-1993.rom`, renamed from the archive's
  `YRW801-M - Yamaha - 1993.rom` per the no-spaces kebab-case naming rule): mask ROM of the Yamaha YRW801
  wavetable option for the YMF278B (OPL4) used by ZXM-MoonSound, **Copyright (c) 1993 Yamaha
  Corporation**. The 2 MiB dump is the single known image, distributed freely in the ZX
  Spectrum / MSX communities for use with MoonSound hardware and emulators (e.g. by Mick
  Laboratory, micklab.ru — the card's author, whose `yrw801m_1993.rar` unpacks to the original
  spaced file name and is byte-identical — verified 2026-09-14 — and the same image also ships
  inside the author's `moonservice_v03.rar` as `moonsnd.rom`); as with the clone firmware above, no formal
  permission from the rights holder exists. Authenticity markers: ASCII `CopyrightYAMAHA` at
  offset 0x1200 and version `01.00` at 0x1FFFFE (the same checks the Linux kernel OPL4 driver
  applies); MD5 `42af93619160ef2116416f74a6cb12f2`.
* `data/symbols/*.map` label tables are derived from published disassemblies and are provided for debugging only.

If you are a rights holder and object to a file being distributed here, open an issue or contact the maintainer
and it will be removed.
