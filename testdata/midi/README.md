# General MIDI sound banks (test data)

Development and comparison material for the SAM2695 (Dream GM synthesizer chip) emulation library.
The default bank for the product is **GeneralUser GS**. Everything in this folder except this README is
git-ignored (`testdata/midi/*` in `.gitignore`), so the files below live only on the machine that
downloaded them. Re-download from the source links; verify with the SHA-256 column.

Collected 2026-10-03. Sizes are exact byte counts with a rounded human value. Every SF2/SF3 file was checked
for the `RIFF....sfbk` header; archives were unpacked with `unzip`, `7z` or `tar` only.

Terms: **GM** = General MIDI level 1 (128 programs + one drum kit). **GS** = Roland's superset (variation tones
selected by bank number, more drum kits). **XG** = Yamaha's superset. **SF2/SF3** = SoundFont 2 (SF3 = SF2 with
Ogg-compressed samples). **DXB/B16** = Dream soundbank binaries for DreamBlaster X2/X3 and X16 cards.

## Banks

### GeneralUser GS (default bank for the product)

Folder: `generaluser-gs/` | Version: 2.0.3 (2026-02-22), repository commit 684543d

License (as stated by the author): GeneralUser GS License v2.0 (`LICENSE.txt`): use without restriction for private or commercial music; may be used, modified and repackaged in software projects. Samples carry the rights of their (freely available) sources.

Note: GM + Roland GS; 261 presets, 13 drum kits; 30.8 MB file, 30.7 MB RAM. Detailed programming, relies on a standards-compliant SF2 synth. Also contains the author's demo MIDIs (`demo-midis/`).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GeneralUser-GS-main.zip` | 65,362,688 (62.3 MB) | `9a9747b7590c503b083a704e286f84296160ff82e1c30de10784a646002f9232` | [link](https://github.com/mrbumpy409/GeneralUser-GS/archive/refs/heads/main.zip) |
| `GeneralUser-GS.sf2` | 32,319,396 (30.8 MB) | `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe` | [link](https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/main/GeneralUser-GS.sf2) |

### FluidR3_GM / FluidR3_GS

Folder: `fluidr3/` | Version: 3.1 (2008, Debian upstream tarball)

License (as stated by the author): MIT (`COPYING`, Frank Wen); `debian-copyright.txt` has the packaging notes.

Note: FluidR3_GM: full GM set, 141.5 MB, classic open-source reference bank. FluidR3_GS ("Fluid R3 GS+SFX Portion"): 3.1 MB, 33 GS SFX variation presets and an SFX kit, meant to be layered over FluidR3_GM.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `fluid-soundfont_3.1.orig.tar.gz` | 134,835,922 (128.6 MB) | `2621acaa1c78e4abdb24bdd163230cc577e61276936d6aa6e3180582142f0343` | [link](http://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont_3.1.orig.tar.gz) |
| `FluidR3_GM.sf2` | 148,398,306 (141.5 MB) | `74594e8f4250680adf590507a306655a299935343583256f3b722c48a1bc1cb0` | extracted from the archive above |
| `FluidR3_GS.sf2` | 3,201,926 (3.1 MB) | `aadb5597fb95ea5f7d336999e3c68ad32aa6357027aed7d4d9ff0ac75f2988e6` | extracted from the archive above |
| `fluid-soundfont_3.1-6.debian.tar.xz` | 14,528 (14.2 KB) | `db1271f182e56ec2c2c8ef22f950c7aeff75b2b00a999e2043d319f6b7ee57c8` | [link](http://deb.debian.org/debian/pool/main/f/fluid-soundfont/fluid-soundfont_3.1-6.debian.tar.xz) |

### FluidR3Mono_GM (MuseScore 3)

Folder: `fluidr3-mono-musescore/` | Version: as shipped with MuseScore 3.6.2

License (as stated by the author): MIT (`FluidR3Mono_License.md`)

Note: GM; mono-sample, Ogg-compressed SF3 rework of FluidR3 by Michael Cowgill; 22.6 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FluidR3Mono_GM.sf3` | 23,712,790 (22.6 MB) | `2aacd036d7058d40a371846ef2f5dc5f130d648ab3837fe2626591ba49a71254` | [link](https://raw.githubusercontent.com/musescore/MuseScore/v3.6.2/share/sound/FluidR3Mono_GM.sf3) |

### MuseScore_General

Folder: `musescore-general/` | Version: 0.2.0 (`VERSION`)

License (as stated by the author): MIT (`MuseScore_General_License.md`)

Note: GM + GS variations; FluidR3Mono descendant improved by S. Christian Collins; SF2 205.6 MB and Ogg-compressed SF3 38.1 MB of the same bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `MuseScore_General.sf2` | 215,614,036 (205.6 MB) | `ee51d2c4b1525e70f19a45909c4fd7a2e26d91d115fa89dbf5a6bc413d8b9bf3` | [link](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf2) |
| `MuseScore_General.sf3` | 39,900,972 (38.1 MB) | `5b85b6c2c61d10b2b91cddd41efcce7b25cd31c8271d511c73afafbef20b6fa3` | [link](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf3) |

### MuseScore_General_HQ

Folder: `musescore-general-hq/` | Version: 0.2.1 (Debian upstream tarball)

License (as stated by the author): MIT (`documentation/MuseScore_General_HQ_License.md`)

Note: GM + GS; lossless high-quality variant of MuseScore_General, 466.8 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `musescore-general-soundfont_0.2.1.orig.tar.xz` | 274,424,736 (261.7 MB) | `0e481d40ac7114d6aef5bdcf7d0246829b8f8873de02049896cd120d8490bcae` | [link](http://deb.debian.org/debian/pool/main/m/musescore-general-soundfont/musescore-general-soundfont_0.2.1.orig.tar.xz) |
| `MuseScore_General_HQ.sf2` | 489,517,354 (466.8 MB) | `985680908c60c082919abdb27708b36726567ddc200b2bb55a62471c37e4fa07` | extracted from the archive above |

### TimGM6mb

Folder: `timgm6mb/` | Version: 1.3 (Debian upstream tarball)

License (as stated by the author): GPL-2 (`debian-copyright.txt`; Tim Brechbill 2004, David Bolton 2010)

Note: GM; small 5.7 MB bank (MuseScore 1.x default). Closest free SF2 to a ROM-sized GM chip set.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `timgm6mb-soundfont_1.3.orig.tar.gz` | 5,560,953 (5.3 MB) | `af8f3a00e416dfb262bcaa904a1c84df04a51b72bbc1313aed012bc754bdf99b` | [link](http://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/timgm6mb-soundfont_1.3.orig.tar.gz) |
| `TimGM6mb.sf2` | 5,969,788 (5.7 MB) | `c5378b62028c920cb11e4803327983fee2f2cdff5dc89c708e39da417e51c854` | extracted from the archive above |
| `timgm6mb-soundfont_1.3-5.debian.tar.xz` | 12,960 (12.7 KB) | `993893c5b6265a3f7972a38d055d450d08873d097a2a2c41b010bd1858cc068c` | [link](http://deb.debian.org/debian/pool/main/t/timgm6mb-soundfont/timgm6mb-soundfont_1.3-5.debian.tar.xz) |

### OPL-3 FM 128M

Folder: `opl3-fm-gm/` | Version: 1.0 (Debian upstream tarball)

License (as stated by the author): CC BY-SA 4.0 (`README.md`, Zandro Reveille)

Note: GM; samples of a Yamaha YMF262 (Sound Blaster 16) FM rendering of every program, 128.8 MB. FM reference.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `opl3-soundfont_1.0.orig.tar.bz2` | 105,279,758 (100.4 MB) | `9b3c530854b4ad03ce98060caf19bda95b68ef8a834e2943a3c80294d08bacc5` | [link](http://deb.debian.org/debian/pool/main/o/opl3-soundfont/opl3-soundfont_1.0.orig.tar.bz2) |
| `OPL-3_FM_128M.sf2` | 135,020,964 (128.8 MB) | `39bff96eee3dcfbce9665e3968701b894ca2136c7d0bd580281b2bbf59e80392` | extracted from the archive above |
| `opl3-soundfont_1.0-4.debian.tar.xz` | 8,192 (8.0 KB) | `94fa4e42b19275a32e3dd460b82f38f84b5ef91d410ccc2255f479d083c2bbce` | [link](http://deb.debian.org/debian/pool/main/o/opl3-soundfont/opl3-soundfont_1.0-4.debian.tar.xz) |

### GXSCC GM

Folder: `gxscc-gm/` | Version: 0.33 (2006)

License (as stated by the author): CC BY 4.0 (as stated on the author's Musical Artifacts entry, quoted in `dreamblaster-unofficial-hummtaro/gxscc-gm/README.txt`)

Note: GM; 126 KB chiptune (square/pulse, GXSCC style) bank by Zandro Reveille. Tiny-bank extreme.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GXSCC_gm_033.sf2` | 128,788 (125.8 KB) | `daed002f2866eceb3895b3b61b43dc804336e1f0ce3e98d881b2689baf95c242` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/GXSCC_gm_033.sf2) |

### Arachno SoundFont

Folder: `arachno/` | Version: 1.0 (documentation 1.2)

License (as stated by the author): Freeware (`Read Me.txt`): free to use in any project, primarily for private non-commercial use; commercial use needs consent of the credited sample authors.

Note: GM + 9 GM/GS kits; 148.2 MB; built from many synth and sample sources, bright and punchy. Archive also holds MIDI demo files and the HTML documentation.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `arachno-soundfont-10-sf2.zip` | 143,356,181 (136.7 MB) | `5c0c573abe6c5e1a3ccceba5d52a4229db24283620c9a3eb1f403018e2aa22df` | [link](http://maxime.abbey.free.fr/mirror/arachnosoft/files/soundfonts/arachno-soundfont-10-sf2.zip) |
| `Arachno SoundFont - Version 1.0.sf2` | 155,405,818 (148.2 MB) | `9a57fb3b6714e69dda12390e351b087e81fc3b1eca15c6b4bbe172799f4cf3cd` | extracted from the archive above |

### SGM-V2.01 (Shan's GM SoundFont)

Folder: `sgm-v2-01/` | Version: V2.01

License (as stated by the author): No license text; INFO chunk: "David Shan 2002-2007". Distributed free of charge by the author (site now gone); archive.org copy.

Note: GM; 235.9 MB, big realistic set, popular reference for large SF2 banks.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SGM-V2.01.sf2` | 247,406,594 (235.9 MB) | `86f2dc4b6983257ce7f983e62afedd22ea35e6e53e3f391360d9f125ce1ea088` | [link](https://archive.org/download/SGM-V2.01/SGM-V2.01.sf2) |

### Timbres of Heaven (Don Allen)

Folder: `timbres-of-heaven/` | Version: 4.00(G) XGM (2021-11-18) and GM_GS_XG_SFX 3.4 Final (2017)

License (as stated by the author): Freeware from the author's distributor MidKar; 4.00 patch list states "Copyright = GNU-GPL 2.0", 3.4 states "Copyright = 2016 Don Allen".

Note: GM + GS + XG (+ SFX); 419 MB (4.00) / 377 MB (3.4); large, rich, game-music oriented.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Timbres of Heaven (XGM) 4.00(G).7z` | 299,193,823 (285.3 MB) | `e5cdfbfc85def9ee59dbd72bc17878dbd8a3a0a8159755fd594ad8b6c3ee4a9c` | [link](https://www.midkar.com/SoundFonts/Timbres%20of%20Heaven%20%28XGM%29%204.00%28G%29.7z) |
| `Timbres of Heaven (XGM) 4.00(G).sf2` | 439,620,490 (419.3 MB) | `be5c79b9dacd823abdf87b0333cce94e6e01876cac5663c8e0d029eac74cee65` | extracted from the archive above |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.4 Final.7z` | 264,239,592 (252.0 MB) | `ee74c940935753fa5cff472607c729d96a4f9f20b85b36cf4e50b965d8309cf9` | [link](https://www.midkar.com/SoundFonts/Timbres%20Of%20Heaven%20GM_GS_XG_SFX%20V%203.4%20Final.7z) |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.4 Final.sf2` | 395,249,188 (376.9 MB) | `a94524fc660ce203dd9216dae008d75c1375c15701eaaa67b95f3aad1dea9384` | extracted from the archive above |

### Musyng Kite

Folder: `musyng-kite/` | Version: as archived on archive.org (final release)

License (as stated by the author): Freeware, no license text (author's KVR thread); archive.org copy.

Note: GM + GS; ~1 GB, very large realistic bank (the largest file here, under the 1.5 GB limit). The `.sfpack` variant on archive.org was skipped (same content, proprietary compression).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Musyng Kite.sf2` | 1,070,141,704 (1020.6 MB) | `ed8c5a34c28b0fbbc805d6bbae74a169582c1881b57219972052e822c7be35f0` | [link](https://archive.org/download/musyng-kite/Musyng%20Kite.sf2) |

### Chorium Revision A

Folder: `chorium/` | Version: Rev. A (2003)

License (as stated by the author): No license text; INFO chunk: "all rights reserved to the author" (openwrld). Freely distributed on the web for two decades; archive.org copy.

Note: GM + GS; 27.6 MB; Creative/E-mu-style tone, a favorite for DOS game music.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `ChoriumRevA.SF2` | 28,926,744 (27.6 MB) | `993a1683a67f30c56c248290e1eb6c13b779331856e9a767ace0f5063f41f897` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/ChoriumRevA.SF2) |

### Florestan Basic GM GS

Folder: `florestan-basic-gm-gs/` | Version: unversioned

License (as stated by the author): INFO chunk: "Public Domain" (Nando Florestan).

Note: GM + GS; 3.1 MB small bank, same size class as Scc1t2. (archive.org item `scc1t2` is actually this file, byte-identical.)

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Florestan_Basic_GM_GS.sf2` | 3,272,882 (3.1 MB) | `6cc153f925bc3c0136d431802bd0039587dd3b3ef4a05b2e9d316c48f41a0363` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Florestan_Basic_GM_GS.sf2) |

### Creative / E-mu GM banks (1, 2, 4, 8 MB)

Folder: `creative-gm/` | Version: 1MB GM (1993), 2GMGSMT Rev N++ (1998), 4MB GMGSMT (1996), 8MBGSFX Rev B

License (as stated by the author): Copyright E-mu Systems (INFO chunks); shipped free with Sound Blaster AWE/Live drivers, no open license.

Note: GM (+ GS / MT-32 variations); 1.0 to 7.2 MB ROM-class banks, the classic Sound Blaster AWE/Live sound. Good size-class comparison for a 4 Mbit chip set.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Creative_1mgm.sf2` | 1,090,280 (1.0 MB) | `de63e1a457fb64d11fbbfc9164a9be474c5625deb6841f7c8effecaa293bbe84` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Creative_1mgm.sf2) |
| `CT2MGM.SF2` | 2,167,684 (2.1 MB) | `cfa4b5899f118208b2a98b5cc0dd35a81d94b5ac4b44e4652308c2d5aa0d646f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/CT2MGM.SF2) |
| `Creative Labs 4M GM_4gmgsmt.sf2` | 4,174,814 (4.0 MB) | `6c429ea4769c8dec705e6f766f6194fc14e3d8462368296b01a0768d669eb1c5` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Creative%20Labs%204M%20GM_4gmgsmt.sf2) |
| `CT8MGM.SF2` | 7,572,224 (7.2 MB) | `538f60cd87e22320b15535fb785da29b58532245697ba0fb23655bd399935d00` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/CT8MGM.SF2) |

### Sonivox EAS wavetable (Android MIDI synth)

Folder: `sonivox-eas-wt-200k/` | Version: sonivox v4.0.2 (bank revision 960, 2009)

License (as stated by the author): Apache 2.0 (`LICENSE`, `NOTICE`)

Note: GM; ~200 KB sample ROM converted from `wt_200k_G.dls` to C arrays (`wt_200k_G.c` + `wt_200k_samples.c`); not an SF2/DLS file. Same design class as a mask-ROM GM chip like the SAM2695.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `wt_200k_G.c` | 4,463 (4.4 KB) | `1a8f98dd7bdc7bfa989f37c5de79a0249c24427d5b05e1af70d3408e97174d4d` | [link](https://raw.githubusercontent.com/pedrolcl/sonivox/v4.0.2/arm-wt-22k/lib_src/wt_200k_G.c) |
| `wt_200k_samples.c` | 2,788,730 (2.7 MB) | `709f64f6e8bf758a216586b4d4e762517de61e85ba7da3896643089305ca9dd7` | [link](https://raw.githubusercontent.com/pedrolcl/sonivox/v4.0.2/arm-wt-22k/lib_src/wt_200k_samples.c) |
| `wt_22khz.c` | 56,123 (54.8 KB) | `3f1c1d72bb1119d9ac4825cd3b3d4ec303f5513079a6d0000018855ed873a6a9` | [link](https://raw.githubusercontent.com/pedrolcl/sonivox/v4.0.2/arm-wt-22k/lib_src/wt_22khz.c) |
| `eas_wt_IPC_frame.h` | 3,185 (3.1 KB) | `3e8ce91d55af97be6bcc9f5184d469fc190dcf1096e3d1891095815040da2253` | [link](https://raw.githubusercontent.com/pedrolcl/sonivox/v4.0.2/arm-wt-22k/lib_src/eas_wt_IPC_frame.h) |

### Dream GM banks GMBK5X128 / GMBK5X64 (official, Dream-native)

Folder: `dream-gmbk5x/` | Version: GMBK5X128 2.03 (2019) and original (2015), GMBK5X64 (2016), X16 build GMBK5X128-V2.03.B16 (2023)

License (as stated by the author): Copyright Dream S.A.S. France; free download from Serdaco, permitted only for use on DreamBlaster cards (see the bank PDFs).

Note: GM (+ MT-32 map on variation 127); Dream's own 16 Mbyte / 8 Mbyte GM set for the SAM5000 series, the nearest official relative of the SAM2695 CleanWave set. `.DXB` = X2/X3 format, `.B16` = X16 format.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GMBK5X128_203.zip` | 12,974,289 (12.4 MB) | `c74d2a5392accca82203211f0aa78b222862b15c75212c3f417a439d8c0addc6` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/Dream/GMBK5X128_203.zip) |
| `GMBK5X128_203.DXB` | 13,650,966 (13.0 MB) | `95a3a211ff8b27684f948a359dfb61f5b88d89e9a22e1f1cd85f1c92dfdaba4b` | extracted from the archive above |
| `GMBK5X128.zip` | 12,463,234 (11.9 MB) | `a75265ce518d11fd327f2287e579856935a8de76eadc98975550fb44d3b3541b` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/Dream/GMBK5X128.zip) |
| `GMBK5X128.DXB` | 13,313,430 (12.7 MB) | `e98257c8b4018b9060fd540ba539c5c5066fe931f1fee21bfe68342ee629f9f8` | extracted from the archive above |
| `GMBK5X64.zip` | 7,515,854 (7.2 MB) | `1bf3b460d789a9c46bcb7e34cf58de4f6c695ec923b72cdb7d339a85d6b68d5e` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/Dream/GMBK5X64.zip) |
| `GMBK5X64.DXB` | 7,870,542 (7.5 MB) | `13879b77572e73414e9b00dbccb425ed0762b13c3e4675fab47ebacda1917390` | extracted from the archive above |
| `GMBK5X128-V2.03.B16.zip` | 12,978,326 (12.4 MB) | `955e148e4628086c14190a1cbb85e2dc803127468636fb80e87b1f460fefe1d2` | [link](https://serdaco.com/downloads/X16/X16_Soundbanks/GMBK5X128-V2.03.B16.zip) |
| `GMBK5X128-V2.03.B16` | 15,626,240 (14.9 MB) | `adf97f6d33e376d0295d3f0471092871daf22c8d5834c809ccabb0b4b454e9ae` | extracted from the archive above |
| `DREAM_EFFECTS_DEFAULT.DXP` | 116 (116 B) | `27e532e433103f7ef17c4cebba90f49839e5aa57bde7c80c80fbfcf3af95645f` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/Dream/DREAM_EFFECTS_DEFAULT.DXP) |

### GUD GM bank for DreamBlaster (Serdaco)

Folder: `dreamblaster-gud/` | Version: 1.04 (2018)

License (as stated by the author): Soundbank binary (c) 2017 Serdaco BVBA, use only on Serdaco cards; GeneralUser samples used with permission (`GUD_10_README.txt`).

Note: GM; 34.2 MB Dream-native `.DXB`, GeneralUser GS samples rebuilt for the Dream engine.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GUD_104.zip` | 32,304,835 (30.8 MB) | `e75cfbb5f39e54f9a90dfe94bfee42a3fd09020c79c07fcc491a7e97afaa4e56` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/GUD/GUD_104.zip) |
| `GUD_104.DXB` | 35,874,118 (34.2 MB) | `880020e3999030889b1b7a5a1f7ccd8f8851f7658926411f6fee54d3205f8ea2` | extracted from the archive above |

### BURAN GM bank for DreamBlaster (Serdaco)

Folder: `dreamblaster-buran/` | Version: 1.1 (2021, X2/X3 `.DXB`) and 1.00 (2023, X16 `.B16`)

License (as stated by the author): Soundbank binary (c) 2018 Serdaco BVBA, use only on Serdaco cards; partially based on GeneralUser samples with permission (`BURANV11_README.TXT`).

Note: GM + MT-32 map (variation 127), 10 drum sets; 44 MB Dream-native bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `BURAN11.zip` | 42,178,952 (40.2 MB) | `d1ebebab6f0f1c5acb18860462d179df26f35f02336357bd1ce37131c69c955d` | [link](https://serdaco.com/downloads/X2/X2_SoundbankPack/GM/BURAN/BURAN11.zip) |
| `BURAN11.DXB` | 46,163,534 (44.0 MB) | `16c46421f0ba8c89058636de9baec83df460e6ec80a0dfdb175ff350ffe546bd` | extracted from the archive above |
| `BURAN-v1.00.B16.zip` | 48,253,822 (46.0 MB) | `444d18f06b6d15e4375f7854e3d645d578776581ee69a4c16e427080d211c5bb` | [link](https://serdaco.com/downloads/X16/X16_Soundbanks/BURAN-v1.00.B16.zip) |
| `BURAN-v1.00.B16` | 54,333,440 (51.8 MB) | `2720122d66241d1b97d82c4385ecbb5985312bd02cca0cd646daa752ae74fdca` | extracted from the archive above |

### Unofficial DreamBlaster banks by Hummtaro (Dream-native)

Folder: `dreamblaster-unofficial-hummtaro/` | Index: [VOGONS: Unofficial Soundbanks for Dreamblaster X2 / X3M / X16](https://www.vogons.org/viewtopic.php?t=65873)

License (as stated by the author): no license text; free downloads posted by the author on VOGONS. Each bank's
`README.txt` names the source bank it was converted from (GXSCC = CC BY 4.0, OPL-3 = CC BY-SA 4.0, ...).

Note: community banks in Dream format (`.dxb` for X2/X3 = SAM5504, `.b16` for X16). The DreamBlaster S2 (SAM2695)
cannot load banks (its CleanWave set is in mask ROM), so these are the closest community material to the Dream
sound engine; `YamahaGM` and `WT_22KHZ` are small ROM-class sets. The thread's `Roland GM` banks were removed:
their readme says they are converted from Windows `gm.dls` (not redistributable).

| Archive | Size | SHA-256 | Extracted bank | Bank SHA-256 | Source | Content |
|---------|------|---------|----------------|--------------|--------|---------|
| `YamahaGM Unfinished.zip` | 1,863,831 (1.8 MB) | `8284ddb9fa8ec24c3f6feae9474c4664d2f91b728e1893dbb2ed7fd2e6d8507d` | `yamahagm-unfinished/YamahaGM.dxb` (3.0 MB) | `ed14cfb21e354a333bd0020652257707b896808702b27ba5ae752fdf4a8acc68` | [link](https://www.mediafire.com/file/qft3srn13hjlw0h/YamahaGM_Unfinished.zip/file) | GM, 226 variations, 10 kits (3.0 MB); source bank not named |
| `YamahaGM_X16.zip` | 1,926,185 (1.8 MB) | `9c63ee529fee91d51eed5f2a2196acd39a853c2f220f732ad8ff4eeaf41d8b22` | `yamahagm-x16/Yamaha GM.b16` (4.0 MB) | `76dee2a0cd88c51ce1924cad4beca41c8d54828e79041992fc9d38e8224f4753` | [link](https://www.mediafire.com/file/zchb8bdk90o1gyl/YamahaGM_X16.zip/file) | same, X16 build |
| `SonicImp  Unfinished.zip` | 7,940,163 (7.6 MB) | `bc845184b8d1f32791bdf434d5c81539df22ba2323169ce12e857f697a33c300` | `sonicimp-unfinished/SonicImp.dxb` (8.6 MB) | `af80fd785567c0712b1462c8312222f671f8b0a8e878335a21691b0e334c711b` | [link](https://www.mediafire.com/file/4buy9uzr8pjd6dj/SonicImp__Unfinished.zip/file) | GM, 1 kit (8.6 MB); source bank not named |
| `WT_22KHZ Unfinished.zip` | 452,900 (442.3 KB) | `5d3dff42ba4bdf1d27cca76fd05f7390098846b70510baa4ac8d0d795633d67f` | `wt-22khz-unfinished/WT_22KHZ.dxb` (601.1 KB) | `b5deb5a243afd5ffeff06d8d63c3525d1403bd1fc42f07d6075de5b589387958` | [link](https://www.mediafire.com/file/e2zp92q4v27qwgk/WT_22KHZ_Unfinished.zip/file) | tiny GM, 2 kits (601 KB); source bank not named |
| `UltraSound Unfinished.zip` | 9,448,962 (9.0 MB) | `b08890b78085559a2d631efc0b2a312205f3107fc0956345ff0a25e0f8b30913` | `ultrasound-unfinished/UltraSound.dxb` (9.5 MB) | `fce12cd0a83b4e0175c673a5770ec9f0cb7966f8e179b35c8abcf4a8505386f8` | [link](https://www.mediafire.com/file/67ieertx959q24b/UltraSound_Unfinished.zip/file) | GM, 2 kits (9.5 MB), presumably Gravis UltraSound patches |
| `UltraSound_X16.zip` | 9,457,941 (9.0 MB) | `453fef7c2d2460c8980ba59e393bfea5a87ddc4a52fb5453e92c3a8f1c7811ab` | `ultrasound-x16/UltraSound.b16` (10.2 MB) | `aeec5856fcbf0443465a4389fa73d28dad0fe5c02e8de4a81ceb805501c16ba3` | [link](https://www.mediafire.com/file/81uxfb4awq066yr/UltraSound_X16.zip/file) | same, X16 build |
| `GXSCC GM.zip` | 68,992 (67.4 KB) | `62c2180727c45c0d22a9f70c457c6f386c8dbd6a588ed72a858291b33d7ee91e` | `gxscc-gm/GXSCC GM.dxb` (157.5 KB) | `b63909446497e512cbfdc5745f88bba3f2672374e357ff74a7c5bbbd8fcd16b7` | [link](https://www.mediafire.com/file/qcz7xr410javjbv/GXSCC_GM.zip/file) | chiptune GM from GXSCC_gm_033 (CC BY 4.0) |
| `GXSCC_X16.zip` | 73,199 (71.5 KB) | `da34de84ebe89559d8ed4dc84a960799a0e8949a3b6fa080768539afaad677ed` | `gxscc-x16/GXSCC GM.b16` (588.0 KB) | `ed9345dcaad6674fda2bd5a87de6ae238de3e46b2ab26c8f9e9ca6b621169384` | [link](https://www.mediafire.com/file/t74de2y3u0jzxiz/GXSCC_X16.zip/file) | same, X16 build |
| `OPL-3 FM 64M.zip` | 58,304,806 (55.6 MB) | `45d4969e178ec30cb60f5cc982f7ddf5fb03db83f86d146af836db8922bfb465` | `opl-3-fm-64m/OPL-3 FM 64M.dxb` (62.0 MB) | `ed274aa6c496430258cd345d618f453c4ec4170cceafbfa71e3d403ca1929424` | [link](https://www.mediafire.com/file/8xfoplam6q7eavb/OPL-3_FM_64M.zip/file) | OPL3 FM GM (62 MB) |
| `OPL-3 FM 48M r2.zip` | 43,886,191 (41.9 MB) | `01daa18332e58fc82c17fa3e2de6c683269742347bee6a0f5c04e21097fb033c` | `opl-3-fm-48m-r2/OPL-3 FM 48M.dxb` (46.6 MB) | `aef18aa8f8fc19bb1f721e5d2881e4fad418dc867b48944376064db8358b29b1` | [link](https://www.mediafire.com/file/kbr0kgvvute3pec/OPL-3_FM_48M_r2.zip/file) | OPL3 FM GM (47 MB) |
| `OPL-3 FM_128M_X16.zip` | 117,002,078 (111.6 MB) | `fcfbb6d5a51dbb9d10b215979950f7119a6b44e535291b93a09247293ba807b6` | `opl-3-fm-128m-x16/OPL-3 FM 128M.b16` (126.8 MB) | `8e12a19ef46eb972c3916d96534ad0fd3ce21d122619df12bbabe2628663a0a0` | [link](https://www.mediafire.com/file/7zvt99o1vxrm700/OPL-3_FM_128M_X16.zip/file) | OPL3 FM GM, X16 build |
| `ESFM_X16.zip` | 207,019,204 (197.4 MB) | `ddd1708ac7ac74651dc97f01e6d7e68a49f97159272415b6ece51797cb1fd51f` | `esfm-x16/ESFM.b16` (247.3 MB) | `8c7e2b2ee94cba8a11a866ab3b6248e1104431b0cf9c9b6348d4ff9d68eaf0e9` | [link](https://www.mediafire.com/file/qpl364itn9uj2zn/ESFM_X16.zip/file) | ESS ESFM FM GM (247 MB), from ESFM.sfpack, X16 only |
| `MT-32_X16.zip` | 161,825,720 (154.3 MB) | `aaba499a1c993b337dee08c42e0064125fa5b5531d4dbd9d4d2ccbfa584130c9` | `mt-32-x16/MT-32.b16` (216.7 MB) | `ae464fc799c2d7920f1cc0fd25a4cfbaecf0c876813a936963df20b99a72a24b` | [link](https://www.mediafire.com/file/i7xtbjaxolgpdps/MT-32_X16.zip/file) | MT-32 + GM map, from Hedsound MT-32 SF2, X16 only |
| `MT-32_Stereo_X16.zip` | 359,208,170 (342.6 MB) | `1c370d9826aa2d0d7d1e606d5cb69b0de8e17a5234664a290bdc3f113e40340b` | `mt-32-stereo-x16/MT-32 Stereo.b16` (481.8 MB) | `3440ec8a638a7a2ad4871b35620b0f16a81618ada1651e461131d9a5ed94cb31` | [link](https://www.mediafire.com/file/5kzd1c65flclbj7/MT-32_Stereo_X16.zip/file) | same, stereo (482 MB), X16 only |
| `Old Upright Piano.7z` | 24,868,060 (23.7 MB) | `4300c48df243c36d3fb1c553c35fc593c6fc39fca0074a4f291d8972f0301e7d` | `old-upright-piano/Old Upright Piano.DXB` (54.4 MB) | `3c6fb6667192d36420fe681efe009130f2b1f21742cfccf347714390b3d3dc40` | [link](http://www.mediafire.com/file/9e5fq6a8manpk24/Old%2BUpright%2BPiano.7z) | solo piano (not GM), X2 |

## Docs

Folder: `docs/`. Dream SAM2000/SAM5000 datasheets, application notes and DreamBlaster MIDI implementation charts.
Copyright of each document stays with its publisher (Dream S.A.S. or Serdaco).

| File | Size | SHA-256 | Content | Source |
|------|------|---------|---------|--------|
| `SAM2695.pdf` | 303,160 (296.1 KB) | `d74390c67767383f27b54134bb968304aecbca35679680573264b875d162b80e` | SAM2695 datasheet (low-power single-chip GM synth with effects; MIDI implementation, CleanWave GM/MT-32 maps) | [link](https://docs.dream.fr/pdf/Serie2000/SAM_Datasheets/SAM2695.pdf) |
| `SAM2195.pdf` | 379,902 (371.0 KB) | `fe5d16759527794dcff1bae7ada2bccfcd1f8776180475042f4421c7e8d7f8da` | SAM2195 datasheet (predecessor of the SAM2695) | [link](http://serdaco.com/files/SAM2195.pdf) |
| `AN_2695_MigrateFromSAM2195toSAM2695.pdf` | 31,376 (30.6 KB) | `2528740f40dfcc73ef24a9caa405453b6acd732a82914c8905a690b92839974a` | Application note: migrating from SAM2195 to SAM2695 (differences list) | [link](https://docs.dream.fr/pdf/Serie2000/Application_Notes/AN_2695_MigrateFromSAM2195toSAM2695.pdf) |
| `SAM2634.pdf` | 186,569 (182.2 KB) | `a84870343dbd882c97f7d4c3306b58bc9342b79a34fb46fc6b348d98fbbee4b4` | SAM2634 datasheet (related 2000-series chip) | [link](https://docs.dream.fr/pdf/Serie2000/SAM_Datasheets/SAM2634.pdf) |
| `SAM2635.pdf` | 249,724 (243.9 KB) | `6429b7b67e67bdd2e97e285629a9ce420756eca753c52d41d209f70bd4ba6154` | SAM2635 datasheet (related 2000-series chip) | [link](https://docs.dream.fr/pdf/Serie2000/SAM_Datasheets/SAM2635.pdf) |
| `Discontinuation_SAM26xx.pdf` | 215,938 (210.9 KB) | `7e6e84bb39675dfd705d99fb0bc9470d7c3a8f94e7160f6cef2086a602515940` | Dream notice: discontinuation of the SAM26xx series (2025) | [link](https://dream.fr/wp-content/uploads/2025/01/Discontinuation_SAM26xx.pdf) |
| `Dream-CleanWave_Sound_Packages.pdf` | 166,352 (162.5 KB) | `972e46e92bcfa4443e2eebc06b07935851d1cf46509d2235a90f1e2b20ec57fd` | Dream CleanWave sound packages (the GM sound set family the SAM2695 ROM belongs to) | [link](https://docs.dream.fr/pdf/SndDev/Sound_Packages/Dream-CleanWave_Sound_Packages.pdf) |
| `Dream-CleanDrum16_Sound_Package.pdf` | 176,563 (172.4 KB) | `c5f911ae314962e82fd048dfb55e077fac18244455b04ddd8de90e7109d90605` | Dream CleanDrum16 sound package | [link](https://docs.dream.fr/pdf/SndDev/Sound_Packages/Dream-CleanDrum16_Sound_Package.pdf) |
| `GMBK5X128.pdf` | 170,949 (166.9 KB) | `125beb50af70f6bdf63a8c4b715da67e909be0fd1e6ca07ee78440b5db5b8e5d` | Dream GMBK5X128 GM bank (SAM5000 series) instrument and drum map | [link](https://docs.dream.fr/pdf/Serie5000/Soundbanks/GMBK5X128.pdf) |
| `SAM5504B.pdf` | 1,433,442 (1.4 MB) | `51d4eea31443c94238afb57f54f47287c77aa3a28bd4f7ab99919e29df2724bd` | SAM5504B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5504B.pdf) |
| `SAM5704B.pdf` | 1,434,632 (1.4 MB) | `3da678bd8f35be8b25b996167a052b7c8cd7226800559b015e0ab45aeca0bd7e` | SAM5704B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5704B.pdf) |
| `SAM5708B.pdf` | 1,431,065 (1.4 MB) | `9ed5ec9882a6f0010d66f576069cb40e3c16f8d7d06f4fcd405a479082cc94f2` | SAM5708B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5708B.pdf) |
| `SAM5716B.pdf` | 1,431,426 (1.4 MB) | `04a6b61ad75d7b7b5aa03606533a058f0fd36aa5da3b6e614edd2476e1d9d79d` | SAM5716B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5716B.pdf) |
| `SAM5808B.pdf` | 1,424,608 (1.4 MB) | `cfc49d5d7f63fec66ea7032a8d96fa6d2e30784a9b3485173661186b23f9ffd2` | SAM5808B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5808B.pdf) |
| `SAM5916B.pdf` | 1,402,670 (1.3 MB) | `12734c592d6c1fd164d1b398a7dd1f36e5ab0e4f4c8221d0ca40d9f7cda4ce43` | SAM5916B datasheet (SAM5000 series) | [link](https://docs.dream.fr/pdf/Serie5000/SAM_Datasheets/SAM5916B.pdf) |
| `5000-SDK_short.pdf` | 508,473 (496.6 KB) | `c04e2f17b55cd4b1d5004c60414b5c2799bbd0f7f24f07b978dbcd475a5cb002` | SAM5000 SDK short form | [link](https://docs.dream.fr/pdf/Serie5000/Short_Forms/5000-SDK_short.pdf) |
| `MakeRom User Guide.pdf` | 378,275 (369.4 KB) | `7dda4151895dd14ce2712562e7a4c2e042ee4a3cfbb5fe2c60e023fb19f7ec74` | Dream MakeRom user guide (building sound ROM images) | [link](https://docs.dream.fr/pdf/General_Application_Notes/MakeRom%20User%20Guide.pdf) |
| `DreamBlaster X2 User Manual.pdf` | 856,631 (836.6 KB) | `9cf42650805e992a08c075197392f32613f6f823a26a694f5cf1a4d1d5acabd1` | DreamBlaster X2 user manual (SAM5504-based card) | [link](https://serdaco.com/downloads/X2/X2_Documentation/DreamBlaster%20X2%20User%20Manual.pdf) |
| `X2_5504_SPECIAL_MIDISPECS.PDF` | 655,297 (639.9 KB) | `2b1f8d51c4e9c4eba7b93a14f620712369c649dfd30c77d353d80973a17e5dce` | DreamBlaster X2 / SAM5504 MIDI implementation (NRPN, SysEx) | [link](https://serdaco.com/downloads/X2/X2_Documentation/X2_5504_SPECIAL_MIDISPECS.PDF) |
| `X2_DefaultSoundbank_GMBK5X128.pdf` | 147,224 (143.8 KB) | `3242bb7a22bac84590e4fd8517561419c9a6167bf6001bfeaa240e419aa54744` | DreamBlaster X2 default soundbank map (GMBK5X128) | [link](https://serdaco.com/downloads/X2/X2_Documentation/X2_DefaultSoundbank_GMBK5X128.pdf) |
| `X2GSBANKSPECSV1.pdf` | 585,763 (572.0 KB) | `da8fc8761542a1cd1798d5dbc2a07c3bca653bfae8c836c8f44b12376c365e9a` | DreamBlaster X2GS GS bank specification | [link](https://serdaco.com/downloads/X2/X2GS_Documentation/X2GSBANKSPECSV1.pdf) |
| `Converting SF2 soundfonts to X2 DreamBlaster soundbanks.pdf` | 1,329,737 (1.3 MB) | `f1f7e1dbe3379b37d39dfe5a54af46d58ba568cd052e3bd543a44291c8ebf2ee` | Serdaco guide: SF2 to Dream DXB conversion | [link](https://serdaco.com/downloads/X2/X2_Documentation/Converting%20SF2%20soundfonts%20to%20X2%20DreamBlaster%20soundbanks.pdf) |
| `DreamBlaster X16 MIDI Specs.pdf` | 820,465 (801.2 KB) | `13a09959b010d158166f6a1d56e6ceaf9afe0ab43ae0d81b76bb2f917ff9880d` | DreamBlaster X16 MIDI implementation | [link](https://serdaco.com/downloads/X16/X16_Documentation/DreamBlaster%20X16%20MIDI%20Specs.pdf) |
| `DreamBlaster X16 User Manual.pdf` | 1,145,785 (1.1 MB) | `3a59093cf625685c7aeb27f795642575f5c867f2c01f5d3266f99a21fa573ad7` | DreamBlaster X16 user manual | [link](https://serdaco.com/downloads/X16/X16_Documentation/DreamBlaster%20X16%20User%20Manual.pdf) |
| `DreamBlaster X16GS GS bank MIDI specs.pdf` | 618,181 (603.7 KB) | `000cf348224a048cc9d76500b629f36bae6304dd58855b4ff1752e179dfbb438` | DreamBlaster X16GS GS bank MIDI implementation | [link](https://serdaco.com/downloads/X16/X16_Documentation/DreamBlaster%20X16GS%20GS%20bank%20MIDI%20specs.pdf) |

## Project pages

- GeneralUser GS home page: [schristiancollins.com/generaluser.php](https://www.schristiancollins.com/generaluser.php)
  (a JavaScript app behind a Cloudflare check, no scriptable download link; the GitHub repository it points to
  carries the same 2.0.3 release and is the source used above).
- Arachno SoundFont: [arachnosoft.com](https://www.arachnosoft.com/main/soundfont.php)
- Timbres of Heaven: [MidKar SoundFonts](https://www.midkar.com/SoundFonts/)
- Dream device documentation: [docs.dream.fr](https://docs.dream.fr/devices.html)
- Serdaco (DreamBlaster) downloads: [serdaco.com/downloads](https://serdaco.com/downloads/)
- Sonivox EAS: [github.com/pedrolcl/sonivox](https://github.com/pedrolcl/sonivox)
- archive.org mirrors: [free-soundfonts-sf2-2019-04](https://archive.org/details/free-soundfonts-sf2-2019-04),
  [musyng-kite](https://archive.org/details/musyng-kite)

## Not obtained

- **Dream CleanWave GM set of the SAM2695 itself**: it lives in the chip's mask ROM and Dream does not distribute
  it. The DreamBlaster S2 cannot load banks, so no S2 bank exists; no community SF2 that recreates the
  CleanWave/S2 sound was found. Closest material here: the Dream GMBK5X banks and the DreamBlaster community banks.
- **Windows `gm.dls` (Microsoft GS Wavetable Synth, Roland 1996 sound set)**: not redistributable. Skipped
  deliberately, together with its conversions: `Microsoft_gm.sf2` on archive.org and Hummtaro's `Roland GM`
  DreamBlaster banks (their readme names `gm.dls` as the source; they were downloaded, then deleted). No legally
  redistributable gm.dls-equivalent DLS bank was found; the nearest permissively licensed ROM-class GM bank is
  the Sonivox EAS wavetable (Apache 2.0) above. `Scc1t2` (the same Roland 1996 set, converted to SF2) was
  downloaded, then deleted for the same reason.
- **Roland SC-55 / SC-88 and Yamaha XG SF2 rips** (archive.org): sampled from commercial ROMs without
  permission; skipped.
- **Musical Artifacts** (`musical-artifacts.com`; Chorium Rev B, Chorium "Albatwo" mod, other mirrors):
  the site answers scripted requests with a Cloudflare challenge (HTTP 403). Chorium Rev A came from archive.org.
- **SAM2195 datasheet on docs.dream.fr**: removed there (HTTP 404); the copy here comes from Serdaco.
- **A separate SAM2695 MIDI implementation chart or other SAM2695 application notes**: Dream publishes none
  besides the datasheet (which contains the MIDI implementation) and the SAM2195 migration note.
- Superseded or duplicate variants skipped on purpose: Timbres of Heaven 3.2 and 3.95, Arachno's sfArk edition,
  Musyng Kite `.sfpack`, older GeneralUser GS versions, the X3M copies of the Serdaco banks.
- No file exceeded the 1.5 GB limit (largest: Musyng Kite, 1.0 GB).
