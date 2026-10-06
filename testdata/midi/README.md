# General MIDI sound banks (test data)

Development and comparison material for the SAM2695 (Dream GM synthesizer chip) emulation library.
The default bank for the product is **GeneralUser GS**. Everything in this folder except this README is
git-ignored (`testdata/midi/*` in `.gitignore`), so the files below live only on the machine that
downloaded them. Re-download from the source links; verify with the SHA-256 column.

Collected 2026-10-03 (second pass the same day: every reachable GM / GS / XG / MT-32 bank, including
banks without a redistribution license). Sizes are exact byte counts with a rounded human value. Every SF2/SF3/SBK
file was checked for the `RIFF....sfbk` header (the Vorbis-packed Timbres 3.4 file has `RIFF....sfpk`), every DLS
file for `RIFF....DLS `, and every archive for not being an HTML error page. Archives were unpacked with `unzip`,
`7z`, `unar` or `tar` only (nothing downloaded was ever run); an extracted archive lives in a subfolder named after
the archive. `.sfArk` files were unpacked with `sfarkxtc` built from source (see "Tools" at the end); `.sfpack`
files stay packed (no free unpacker exists).

**Local test asset only:** many banks below carry no redistribution license (rips of commercial ROMs and
drivers, community banks without license text). They are kept for comparison and regression tests on this machine,
are never committed, never shipped, never used as a product default, and renders made with them are not
published. Each section states the license as the author or the file states it.

Terms: **GM** = General MIDI level 1 (128 programs + one drum kit). **GS** = Roland's superset (variation tones
selected by bank number, more drum kits). **XG** = Yamaha's superset. **SF2/SF3** = SoundFont 2 (SF3 = SF2 with
Ogg-compressed samples). **DXB/B16** = Dream soundbank binaries for DreamBlaster X2/X3 and X16 cards.

## Banks

### GeneralUser GS (default bank for the product)

Folder: `generaluser-gs/` | Version: 2.0.3 (2026-02-22), repository commit 684543d

The product copy (the same file, byte for byte) lives in `data/midi/generaluser-gs.sf2` with its license and README.

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

Folder: `musescore-general/` | Version: 0.2.0 (`VERSION`); also v0.1.3 (2018 SF2, INFO "v0.1.1") from archive.org

License (as stated by the author): MIT (`MuseScore_General_License.md`)

Note: GM + GS variations; FluidR3Mono descendant improved by S. Christian Collins; SF2 205.6 MB and Ogg-compressed SF3 38.1 MB of the same bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `MuseScore_General.sf2` | 215,614,036 (205.6 MB) | `ee51d2c4b1525e70f19a45909c4fd7a2e26d91d115fa89dbf5a6bc413d8b9bf3` | [link](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf2) |
| `MuseScore_General.sf3` | 39,900,972 (38.1 MB) | `5b85b6c2c61d10b2b91cddd41efcce7b25cd31c8271d511c73afafbef20b6fa3` | [link](https://ftp.osuosl.org/pub/musescore/soundfont/MuseScore_General/MuseScore_General.sf3) |
| `MuseScore_General(v0.1.3).sf2` | 218,388,782 (208.3 MB) | `8520f85bd115d51be327736584fd2b0ccced1ec786636fc2139efea2d714a5b4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/MuseScore_General%28v0.1.3%29.sf2) |

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

Note: GM + 9 GM/GS kits; 148.2 MB; built from many synth and sample sources, bright and punchy. Archive also holds MIDI demo files and the HTML documentation. The sfArk edition (`arachno-soundfont-10.zip`, the author's mirror; it also bundles the sfArk tools, which were not run) unpacks with sfarkxtc to the same bytes as the SF2 (SHA-256 `9a57fb3b...`); a second `.sfArk` copy came from the archive.org `GMSoundfonts` collection.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `arachno-soundfont-10-sf2.zip` | 143,356,181 (136.7 MB) | `5c0c573abe6c5e1a3ccceba5d52a4229db24283620c9a3eb1f403018e2aa22df` | [link](http://maxime.abbey.free.fr/mirror/arachnosoft/files/soundfonts/arachno-soundfont-10-sf2.zip) |
| `Arachno SoundFont - Version 1.0.sf2` | 155,405,818 (148.2 MB) | `9a57fb3b6714e69dda12390e351b087e81fc3b1eca15c6b4bbe172799f4cf3cd` | extracted from the archive above |
| `Arachno SoundFont - Version 1.0.sfArk` | 73,979,612 (70.6 MB) | `dc038589ebbc7c2286e1d837edbd140263b3a3f6e88073d1b648d4528ca26c9f` | [link](https://archive.org/download/GMSoundfonts/Arachno%20SoundFont%20-%20Version%201.0.sfArk) |
| `arachno-soundfont-10.zip` | 82,571,454 (78.7 MB) | `5f27fba3eb3dfa2106bbfa3460d236c6f5452fdef362fc84e9c4eafc409b395a` | [link](http://maxime.abbey.free.fr/mirror/arachnosoft/files/soundfonts/arachno-soundfont-10.zip) |
| `arachno-soundfont-10/` (directory, 435 files) | 92,141,802 (87.9 MB) | - | extracted from `arachno-soundfont-10.zip` |
| `arachno-soundfont-10/Arachno SoundFont - Version 1.0.sfArk` | 73,981,184 (70.6 MB) | `aae2df46d759a732ccaabb088963af3a7d0846dcf5663a47c4a442f09eb7fcc5` | extracted from `arachno-soundfont-10.zip` |
| `arachno-soundfont-10/sfArk/sfArkXT (Mac OS X).zip` | 119,583 (116.8 KB) | `1882b633af9cc0a6bceab622803a01a6cf1e657d2a61f286a61778a66d66da69` | extracted from `arachno-soundfont-10.zip` |
| `arachno-soundfont-10/sfArk/sfArkXTc.tar (Linux).gz` | 34,127 (33.3 KB) | `717816e9dcfcb560e1cb561ed64b22b4be65f09675f516ffd552c1dcccdedc32` | extracted from `arachno-soundfont-10.zip` |

### SGM-V2.01 (Shan's GM SoundFont)

Folder: `sgm-v2-01/` | Version: V2.01

License (as stated by the author): No license text; INFO chunk: "David Shan 2002-2007". Distributed free of charge by the author (site now gone); archive.org copy.

Note: GM; 235.9 MB, big realistic set, popular reference for large SF2 banks.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SGM-V2.01.sf2` | 247,406,594 (235.9 MB) | `86f2dc4b6983257ce7f983e62afedd22ea35e6e53e3f391360d9f125ce1ea088` | [link](https://archive.org/download/SGM-V2.01/SGM-V2.01.sf2) |

### Timbres of Heaven (Don Allen)

Folder: `timbres-of-heaven/` | Version: 4.00(G) XGM (2021-11-18), 3.95 XGM, GM_GS_XG_SFX 3.4 Final (2017, plus a Vorbis-packed `sfpk` copy) and 3.2 Final

License (as stated by the author): Freeware from the author's distributor MidKar; 4.00 patch list states "Copyright = GNU-GPL 2.0", 3.4 states "Copyright = 2016 Don Allen".

Note: GM + GS + XG (+ SFX); 419 MB (4.00) / 377 MB (3.4); large, rich, game-music oriented.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Timbres of Heaven (XGM) 4.00(G).7z` | 299,193,823 (285.3 MB) | `e5cdfbfc85def9ee59dbd72bc17878dbd8a3a0a8159755fd594ad8b6c3ee4a9c` | [link](https://www.midkar.com/SoundFonts/Timbres%20of%20Heaven%20%28XGM%29%204.00%28G%29.7z) |
| `Timbres of Heaven (XGM) 4.00(G).sf2` | 439,620,490 (419.3 MB) | `be5c79b9dacd823abdf87b0333cce94e6e01876cac5663c8e0d029eac74cee65` | extracted from the archive above |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.4 Final.7z` | 264,239,592 (252.0 MB) | `ee74c940935753fa5cff472607c729d96a4f9f20b85b36cf4e50b965d8309cf9` | [link](https://www.midkar.com/SoundFonts/Timbres%20Of%20Heaven%20GM_GS_XG_SFX%20V%203.4%20Final.7z) |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.4 Final.sf2` | 395,249,188 (376.9 MB) | `a94524fc660ce203dd9216dae008d75c1375c15701eaaa67b95f3aad1dea9384` | extracted from the archive above |
| `Timbres Of Heaven (XGM) 3.95.7z` | 297,260,343 (283.5 MB) | `42b6fc4825be9620fd409f38b505b9a2a4172799bc73baf0b647c59d3df38faa` | [link](https://midkar.com/SoundFonts/Timbres%20Of%20Heaven%20(XGM)%203.95.7z) |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final.7z` | 248,563,447 (237.0 MB) | `d2b5bdde0458c5428b3ff67b4ea442852e8537efc88ae2b159e4afe9825b2e8a` | [link](https://midkar.com/SoundFonts/Timbres%20Of%20Heaven%20GM_GS_XG_SFX%20V%203.2%20Final.7z) |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.4 Final_Vorbis.sf2` | 14,910,888 (14.2 MB) | `755e8bc9645ff20ecbe83efc0a99e6bd96189acb4ae87c16e556cbffbb883f74` | [link](https://archive.org/download/toh-gmgsxg/Timbres%20Of%20Heaven%20GM_GS_XG_SFX%20V%203.4%20Final_Vorbis.sf2) |
| `Timbres Of Heaven (XGM) 3.95/` (directory, 4 files) | 436,739,324 (416.5 MB) | - | extracted from `Timbres Of Heaven (XGM) 3.95.7z` |
| `Timbres Of Heaven (XGM) 3.95/Timbres Of Heaven (XGM) 3.95.sf2` | 436,708,556 (416.5 MB) | `133f53afa2faa7e819ef8cffa45638df6ce6b5c556176402f717fa4c2cec5e44` | extracted from `Timbres Of Heaven (XGM) 3.95.7z` |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final/` (directory, 3 files) | 371,399,620 (354.2 MB) | - | extracted from `Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final.7z` |
| `Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final/Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final.sf2` | 371,375,172 (354.2 MB) | `a7216942f181f4c8081352829c210472a898c048bde94805b3d0990c1c04f470` | extracted from `Timbres Of Heaven GM_GS_XG_SFX V 3.2 Final.7z` |

### Musyng Kite

Folder: `musyng-kite/` | Version: as archived on archive.org (final release)

License (as stated by the author): Freeware, no license text (author's KVR thread); archive.org copy.

Note: GM + GS; ~1 GB, very large realistic bank. Also kept: the `.sfpack` variant from the same archive.org item (proprietary compression, not unpacked) and `Musyng_Kite.sfArk` from the `GMSoundfonts` collection (it unpacks with sfarkxtc to exactly the same bytes as `Musyng Kite.sf2`; the unpacked copy was removed). Its predecessor "Musyng" is in `musyng/`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Musyng Kite.sf2` | 1,070,141,704 (1020.6 MB) | `ed8c5a34c28b0fbbc805d6bbae74a169582c1881b57219972052e822c7be35f0` | [link](https://archive.org/download/musyng-kite/Musyng%20Kite.sf2) |
| `Musyng Kite.sfpack` | 354,155,569 (337.7 MB) | `92cbbeaa66154ed1d5bac9499c246272f3d532158be28f10cb1afb0f960b107b` | [link](https://archive.org/download/musyng-kite/Musyng%20Kite.sfpack) |
| `Musyng_Kite.sfArk` | 348,595,576 (332.4 MB) | `edb5e9deb467c84f36fe3f54696d115359bd8888ddef515ff37d2ffcabc42989` | [link](https://archive.org/download/GMSoundfonts/Musyng_Kite.sfArk) |

### Chorium Revision A

Folder: `chorium/` | Version: Rev. A (2003)

License (as stated by the author): No license text; INFO chunk: "all rights reserved to the author" (openwrld). Freely distributed on the web for two decades; archive.org copy.

Note: GM + GS; 27.6 MB; Creative/E-mu-style tone, a favorite for DOS game music.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `ChoriumRevA.SF2` | 28,926,744 (27.6 MB) | `993a1683a67f30c56c248290e1eb6c13b779331856e9a767ace0f5063f41f897` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/ChoriumRevA.SF2) |

### Scc1t2

Folder: `scc1t2/` | Version: 1.00.16 (INFO: "960920 ver. 1.00.16", "GS sound set (16 bit)", converted for the E-mu 10K1 with Awave 4.8)

License (as stated by the author): no license; INFO chunk: "Copyright 1996 Roland Corporation U.S.".

Note: GM + GS; 3.1 MB; the Roland Sound Canvas 1996 sound set (the one Windows' Microsoft GS Wavetable Synth,
`gm.dls`, also uses), converted to SF2. Small ROM-class reference for the SAM2695 work (a sound set of the same size
class as the chip's 4 Mbit CleanWave). **Local development asset only:** Roland's copyright, no redistribution
license; never committed, never shipped, never used as a default; comparison renders made with it are not published.
The original `gm.dls` and its other SF2 conversions are in `microsoft-gm-dls/`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Scc1t2.sf2` | 3,281,786 (3.1 MB) | `865f15ed7097d68539c4556ceea629658148e5d8fcc9d15a4eb13144653e9cb8` | [link](https://stash.reaper.fm/23360/Scc1t2.sf2) |

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

Note: GM (+ GS / MT-32 variations); 1.0 to 7.2 MB ROM-class banks, the classic Sound Blaster AWE/Live sound. Good size-class comparison for a 4 Mbit chip set. The rest of the Creative / E-mu family (28 MB, 3.5 MB, 4MBGM, 4MBGMSFX, 8MBGM, APS, AWE32 ROM, AWE64 CD banks) is in `creative-sound-blaster-archive/` and `creative-emu-extra/`.

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
sound engine; `YamahaGM` and `WT_22KHZ` are small ROM-class sets. The `Roland GM` banks (recovered 2026-10-03) are,
per their readme, converted from Windows `gm.dls` (Roland 1996 sound set, licensed for Windows only): **local test
asset only**, like `microsoft-gm-dls/`.

| Archive | Size | SHA-256 | Extracted bank | Bank SHA-256 | Source | Content |
|---------|------|---------|----------------|--------------|--------|---------|
| `Roland GM.zip` | 3,120,771 (3.0 MB) | `3d6e56ae2c15ef77ebf6b8111fd962d8595578179bf48d7d3737e15eafe9d1e4` | `roland-gm/Roland GM.dxb` (3.1 MB) | `a2f97527595243a74c8387605e2f5feb984769ecd9542a681242dfb3e6628885` | [link](https://www.mediafire.com/file/lxm0r3vjva4f8w2/Roland_GM.zip/file) | GM + GS, 226 variations, 10 kits, MT-32 map on variation 127 (3.1 MB); from Windows `gm.dls`; also `Roland GM Preset.DXP` |
| `RolandGM_X16.zip` | 3,131,611 (3.0 MB) | `b113faade08fc4458c446fe21af855f76377cad9b7c1d964f150e61088260a14` | `rolandgm-x16/Roland GM.b16` (4.2 MB) | `b5d022c509f108e02a42f5d70ef324cbb489051a01565e0e5c59712897d729ee` | [link](https://www.mediafire.com/file/qc4l5qed617rwrd/RolandGM_X16.zip/file) | same, X16 build (2024); `Roland GM Readme.txt`, `Roland GM Preset.DXP` |
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

### Microsoft GS Wavetable Synth (`gm.dls`) and its SF2 conversions

Folder: `microsoft-gm-dls/` | Version: gm.dls 3,440,660 bytes (Windows 2000 to 11), sound set 960920 ver. 1.00.16

License (as stated by the author): `gmreadme.txt`: Roland GS Sound Set (P) 1996 Roland Corporation U.S., licensed under Microsoft's EULA for use with Microsoft operating systems only. **Local test asset only** (no redistribution license).

Note: GM + GS; the Roland 1996 Sound Canvas sound set that every Windows PC plays MIDI with (226 melodic + 9 drum instruments). `gm.dls` is the DLS level 1 original (RIFF `DLS `); the SF2 files are third-party conversions of it: `GS sound set (16 bit).sf2` (SpessaSynth, 2024), `Microsoft_gm.sf2` and `Roland GS Wavetable Synth.sf2` (byte-identical, `dls_cnv` v0.27y), `GM1_Roland.sf2` ("Sound Canvas Pure", Polyphone). Same set as `scc1t2/`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GM1_Roland.sf2` | 3,298,386 (3.1 MB) | `f40a5f4e941072339007631a6da3c249595391caf39a4042a328b46410c449c0` | [link](https://archive.org/download/gm-1-roland/GM1_Roland.sf2) |
| `GS sound set (16 bit).sf2` | 3,234,092 (3.1 MB) | `56966650cde4736311f7c6cb0e44dd85bc62b4e8432b8fe2178b24234b64421a` | [link](https://archive.org/download/gm_20240929/GS%20sound%20set%20%2816%20bit%29.sf2) |
| `Microsoft_gm.sf2` | 3,208,108 (3.1 MB) | `c6d6d1d91995d49187510de5de536054b410456a05065672d331a9afab26c274` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Microsoft_gm.sf2) |
| `Roland GS Wavetable Synth.sf2` | 3,208,108 (3.1 MB) | `c6d6d1d91995d49187510de5de536054b410456a05065672d331a9afab26c274` | [link](https://archive.org/download/roland-gs-wavetable-synth/Roland%20GS%20Wavetable%20Synth.sf2) |
| `gm.dls` | 3,440,660 (3.3 MB) | `3229b09b9d7d9f3f4793b0d9b34fe6abc75cfa4a2503c0c90f43ff651ba7f2c0` | [link](https://archive.org/download/gm_20240929/gm.dls) |
| `gmreadme.txt` | 646 (646 B) | `10bf6e15c08c8c7f5d96b658bab78aed86d1de20f08e7871eefba0939af11a02` | [link](https://archive.org/download/gm_20240929/gmreadme.txt) |

### Roland SC-55 SoundFont by EmperorGrieferus

Folder: `roland-sc55-emperorgrieferus/` | Version: v3.7 (2017) and the 79 MB edition (2022)

License (as stated by the author): No license text; INFO chunk of the 79 MB edition: "Roland Corporation, 1991" (samples from the SC-55). **Local test asset only** (no redistribution license).

Note: GM + GS; large SC-55 recreation, started as a fork of Patch93's bank. `Changelog.txt` from the archive.org item.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Changelog.txt` | 5,314 (5.2 KB) | `04874170548e12fffcd1c895afd2e6aeced550da03cd55b4f3316c7c965c67ea` | [link](https://archive.org/download/SC55EmperorGrieferus/Changelog.txt) |
| `Roland SC-55 v3.7.sf2` | 108,424,522 (103.4 MB) | `ac294a0d2b4b645b1d18f3858f3e3435c7bed674d3a70bf3da21d833ac184187` | [link](https://archive.org/download/SC55EmperorGrieferus/Roland%20SC-55%20v3.7.sf2) |
| `SC-55 EmperorGrieferus 79mb.sf2` | 83,771,692 (79.9 MB) | `3914c5d905f3aca2968d4c238aec66545c9cabb54467b80c20252580f8a5e947` | [link](https://archive.org/download/sc-55-emperor-grieferus-79mb/SC-55%20EmperorGrieferus%2079mb.sf2) |

### Roland SC-55 SoundFont by Patch93

Folder: `roland-sc55-patch93/` | Version: v2.2 (with xan1242), 2016; plus a TiMidity++ compatibility fix (2024)

License (as stated by the author): No license text (sampled from Roland SC-55 hardware). **Local test asset only** (no redistribution license).

Note: GM + GS; the classic SC-55 SF2 recreation. `Patch93_mod.sf2` is a third-party variant with TiMidity++ fixes. (`Roland_SC-55_v2.2_by_Patch93_and_xan1242.sf2` in the 2019 archive.org collection has the same size as `SC-55.sf2`.)

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Patch93_mod.sf2` | 47,357,568 (45.2 MB) | `14b217ed45150d598317c76283445e577c8d3a8e38e32548043ca10ca646534f` | [link](https://archive.org/download/patch-93-timidity-fix/Patch93_mod.sf2) |
| `SC-55.sf2` | 47,324,042 (45.1 MB) | `ae2e56711dc40835d3f97d975d80eadb2abdc76dc9da8b055f7a3c18dab20974` | [link](https://archive.org/download/sc-55/SC-55.sf2) |

### Roland SC-55 SoundFont by Trevor0402

Folder: `roland-sc55-trevor0402/` | Version: as archived 2024 (INFO: "SC-55 SoundFont v1.12b", fixed by Triaxis)

License (as stated by the author): No license text; INFO chunk: "Roland Corporation (C) 1991". **Local test asset only** (no redistribution license).

Note: GM + GS; small (~10 MB) SC-55 bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Trevor0402_SC-55.zip` | 9,259,189 (8.8 MB) | `3567ca4c2d5234a2835711e893bd1c04674b9c0b5394c74afe772606e9abb5f6` | [link](https://archive.org/download/trevor-0402-sc-55-sf2/Trevor0402_SC-55.zip) |
| `Trevor0402_SC-55/` (directory, 1 files) | 10,403,854 (9.9 MB) | - | extracted from `Trevor0402_SC-55.zip` |
| `Trevor0402_SC-55/SC-55.SF2` | 10,403,854 (9.9 MB) | `36ef83bf87a1e3ce550a7e2b406c52bdb1e7584451f6c560672c1f2ec05683b1` | extracted from `Trevor0402_SC-55.zip` |

### SC-55 SoundFont v1.2b (Triaxis)

Folder: `roland-sc55-v1-2b/` | Version: v1.2b (INFO says v1.12b); two archive.org uploads of different size, plus the sfArk edition

License (as stated by the author): No license text; INFO chunk: "Roland Corporation (C) 1991", "Fixed by Triaxis". **Local test asset only** (no redistribution license).

Note: GM + GS; ~10 MB SC-55 sample set. The `.sfArk` was unpacked with sfarkxtc (see the note at the end). `SC-55.SoundFont.v1.2b.sfArk` unpacks to exactly the same bytes as `SC-55.SoundFont.v1.2b.sf2` (unpacked copy removed).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SC-55 SoundFont v1.2b.sf2` | 10,386,170 (9.9 MB) | `e6c6f2d82ebd0c0fb4c2f4851ad34ca6a2a5988ad737c4922470c59822dbf520` | [link](https://archive.org/download/sc-55-sound-font-v-1.2b/SC-55%20SoundFont%20v1.2b.sf2) |
| `SC-55.SoundFont.v1.2b.sf2` | 10,403,854 (9.9 MB) | `36ef83bf87a1e3ce550a7e2b406c52bdb1e7584451f6c560672c1f2ec05683b1` | [link](https://archive.org/download/sc-55.-sound-font.v-1.2b/SC-55.SoundFont.v1.2b.sf2) |
| `SC-55.SoundFont.v1.2b.sfArk` | 6,944,768 (6.6 MB) | `6cb6e1ff480a6b79253bd5321aeb5e944ae551d1bd4dca2b57446c448ec70bfe` | [link](https://archive.org/download/GMSoundfonts/SC-55.SoundFont.v1.2b.sfArk) |

### Other Roland SC-55 SoundFonts (archive.org uploads)

Folder: `roland-sc55-misc/` | Version: various; one subfolder per archive.org item

License (as stated by the author): No license texts; several carry "Copyright 1996 Roland Corporation U.S." (i.e. they are `gm.dls` / SCC-1 conversions despite the SC-55 name). **Local test asset only** (no redistribution license).

Note: GM + GS; unattributed SC-55 banks: `SC-55 Roland SOUNDCanvas Up.sf2` / `SC-55 Soundfont.sf2` (185 MB), `Roland_SC-55_by_StrikingUAC.sf2`, `Roland_SC-55_v1.1 full pack.sf2` (Dj Tony 2007), `Roland_SC-55.sf2`, and three `SC-55.sf2` uploads of different size.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `free-soundfonts-2019/Roland_SC-55.sf2` | 25,744,528 (24.6 MB) | `a6ce98b03dd7899a7555d184f18bcd8299646f804bb5b509b8152b94f5c49037` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Roland_SC-55.sf2) |
| `free-soundfonts-2019/Roland_SC-55_by_StrikingUAC.sf2` | 56,194,824 (53.6 MB) | `a368a897ed9cd98fe75aa173b0ed5b0df74c1f3e2afa7aba74746ac6867b2ca5` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Roland_SC-55_by_StrikingUAC.sf2) |
| `free-soundfonts-2019/Roland_SC-55_v1.1 full pack.sf2` | 9,862,608 (9.4 MB) | `e3420bfd064f3f096e83522f743de88830e1487b0564c35d219cb138142361ce` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Roland_SC-55_v1.1%20full%20pack.sf2) |
| `free-soundfonts-2019/SC-55.sf2` | 137,664,302 (131.3 MB) | `4443b9543798ac6b43b7defb78937e1895053c68437b32010c11d66bf34bd09c` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/SC-55.sf2) |
| `sc-55-2023/SC-55.sf2` | 118,503,680 (113.0 MB) | `e88863eb4de408ea006f69620f330ee24c2ae8b2338b971cda331253a1da8373` | [link](https://archive.org/download/sc-55_202308/SC-55.sf2) |
| `sc-55-2024/SC-55.sf2` | 55,366,360 (52.8 MB) | `036b45cefff6aea7bd22c26b53a4dca47ee39490ad19ec68a11b621056f3c1ec` | [link](https://archive.org/download/sc-55_20250708/SC-55.sf2) |
| `sc-55-roland-soundcanvas-up/SC-55 Roland SOUNDCanvas Up.sf2` | 185,718,730 (177.1 MB) | `ce26d477924b95da58b1b00bfd11c6f8580bf7ecf0b2c64db10dbfe4a9927714` | [link](https://archive.org/download/sc-55-roland-soundcanvas-up/SC-55%20Roland%20SOUNDCanvas%20Up.sf2) |
| `sc-55-roland-soundcanvas-up/SC-55 Soundfont.sf2` | 185,357,224 (176.8 MB) | `69e238413b64a88f727c3116766b4deeddeef6194b559c286de24245f2878ed3` | [link](https://archive.org/download/sc-55-roland-soundcanvas-up/SC-55%20Soundfont.sf2) |

### SC-55 SoundFont by zzdenis

Folder: `roland-sc55-zzdenis/` | Version: v0.6 (2024)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 303 MB SC-55 recreation (the same archive.org item also holds KORG i3 and Akai piano banks, not GM, not downloaded). The download link answered HTTP 500 at the final link check (2026-10-04, archive.org storage node fault); the local file is complete (size equals the item metadata, RIFF length matches).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SC55_zzdenis_v0.6.sf2` | 303,119,076 (289.1 MB) | `f188c58243393d725b16e9d762ef924cb88ec2ca7fdc8577a3ae41316ac949aa` | [link](https://archive.org/download/SF_zzdenis/SC55_zzdenis_v0.6.sf2) |

### Roland SC-88 samples and SF2

Folder: `roland-sc88/` | Version: sample pack (1998 date on the item) and an SF2 build

License (as stated by the author): No license text (sampled from a Roland SC-88). **Local test asset only** (no redistribution license).

Note: GS; `Roland SC-88.zip` holds single WAV samples (one per tone), `Roland_SC-88.sf2` (22.8 MB) is a playable bank, `Roland_SC-88.sfArk` comes from a different collection (see its unpacked SHA-256 in the table). The sfArk unpacks to exactly the same bytes as `Roland_SC-88.sf2` (the unpacked copy was removed).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Roland SC-88.zip` | 44,538,571 (42.5 MB) | `679c0695789f282bfc25e450bc102ad5494403b93e52abc8bb9f8756baff85ca` | [link](https://archive.org/download/roland-sc-88/Roland%20SC-88.zip) |
| `Roland_SC-88.sf2` | 22,827,354 (21.8 MB) | `a40d9291194df240b68bb367f97aaac70fa2ac16b526ad713ab9b6c0ca5236a4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Roland_SC-88.sf2) |
| `Roland_SC-88.sfArk` | 10,370,675 (9.9 MB) | `a07f630da00b9f3896135a30952edb3bf3e853040f14293cb5bb7a043ecf81b0` | [link](https://archive.org/download/GMSoundfonts/Roland_SC-88.sfArk) |
| `Roland SC-88/` (directory, 135 files) | 54,484,882 (52.0 MB) | - | extracted from `Roland SC-88.zip` |

### Complete Sound Canvas SC-88 Pro SoundFont Collection

Folder: `roland-sc88pro-complete/` | Version: v1 (archive.org, 2026-07)

License (as stated by the author): No license text (multisampled from a Roland SC-88 Pro). **Local test asset only** (no redistribution license).

Note: GS; the SC-88 Pro sound set split into one SF2 per instrument family (piano, bass, ..., drum kits), 8.5 GB in total; `SC88Pro (Normalized & Stereo)/BASS.sf2` is an alternative normalized/stereo build of the bass family.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SC88Pro/BASS.sf2` | 631,450,452 (602.2 MB) | `e6023eb55d2c4c61c844d92d0db1cb4e8ddce835bf994f5ba9de67802a896042` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/BASS.sf2) |
| `SC88Pro/BRASS.sf2` | 539,141,160 (514.2 MB) | `d912a188f1713c40c72d4a69fba99f1aa0ec512fc13ce44426aeac87718d84aa` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/BRASS.sf2) |
| `SC88Pro/CHROMATIC.sf2` | 134,851,048 (128.6 MB) | `a81d203018ccb4dee36a2eeaa7fb5122552834696079e08f042e36c1a193e1dd` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/CHROMATIC.sf2) |
| `SC88Pro/DRUM KIT.sf2` | 136,073,972 (129.8 MB) | `bcd94ca31717117879dee84ac7057b403293d3891cfd409e54b2633f7a19cca0` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/DRUM%20KIT.sf2) |
| `SC88Pro/ENSEMBLE.sf2` | 614,815,648 (586.3 MB) | `9ccfd79a2785e4754e8c5ace5c47f1c22fcd5369a185b3dc3314856f931d7514` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/ENSEMBLE.sf2) |
| `SC88Pro/ETHNIC.sf2` | 289,709,300 (276.3 MB) | `fbf4bb91600d2a14101385974ec861f15fcb764cf8972662d108e2f662a4ff5e` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/ETHNIC.sf2) |
| `SC88Pro/GUITAR.sf2` | 447,884,364 (427.1 MB) | `8a35cb7aa37895064cc4b2398c1b563bc1f77cb0926f662319d869780935503a` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/GUITAR.sf2) |
| `SC88Pro/ORCHESTRA.sf2` | 157,804,350 (150.5 MB) | `cc726cbe7118f5f5f6d2fc4d6be2683d7dc7734be5c8ee639dd388eacaedef92` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/ORCHESTRA.sf2) |
| `SC88Pro/ORGAN.sf2` | 550,256,930 (524.8 MB) | `a6840c212bf9c3b6784a8e45e394e61ea43ac8f9f3aa197177f65396062c63bc` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/ORGAN.sf2) |
| `SC88Pro/PERC_.sf2` | 123,634,478 (117.9 MB) | `f7532f87864aef672af8ea3f234f0937dc74a0d3b2ba9312e75bf3764c9f7eaa` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/PERC_.sf2) |
| `SC88Pro/PIANO.sf2` | 319,750,748 (304.9 MB) | `0afab1384dce317e9e81c334d3560407b961b4fc8f666a739ceb48cac4e0a9eb` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/PIANO.sf2) |
| `SC88Pro/PIPE.sf2` | 238,686,954 (227.6 MB) | `f37aff4208768a8d05616285fb89f16a3691aa26a34142720a24259710c3527c` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/PIPE.sf2) |
| `SC88Pro/REED.sf2` | 223,683,380 (213.3 MB) | `1e27b04d6ccfb3e72f6b4a5cc8c38bd808ac198c016ad340fea93ddef72b9c4f` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/REED.sf2) |
| `SC88Pro/SFX.sf2` | 475,905,624 (453.9 MB) | `e5eca0e144adac5420dd43cf90ef2556d39ceea9255c358ff38aa020e5aa989b` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/SFX.sf2) |
| `SC88Pro/SYN_LEAD.sf2` | 862,767,044 (822.8 MB) | `5a0da5906ba9126fb016cb6205b32ffd424cc00f6456488cd41e36beddc8bf68` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/SYN_LEAD.sf2) |
| `SC88Pro/SYN_PAD.sf2` | 553,761,220 (528.1 MB) | `0437c6cd15f0ad9fe4a1877b28affca8c458258f771354ec33c2448b0941fdaa` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/SYN_PAD.sf2) |
| `SC88Pro/SYN_SFX.sf2` | 839,143,068 (800.3 MB) | `fdc30c7e194fee7983b38156f5e7666ab5b1c6469434feceffc5547951ca0bf2` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro/SYN_SFX.sf2) |
| `SC88Pro (Normalized & Stereo)/BASS.sf2` | 1,340,556,200 (1.2 GB) | `34f68c6d09014b39dc863b4be28e845259449b1afa323d0d73ae4511e671275b` | [link](https://archive.org/download/sc-88-pro-complete-sf2-v1/SC88Pro%20%28Normalized%20%26%20Stereo%29/BASS.sf2) |

### 1 GB 88 Pro

Folder: `roland-sc88pro-1gb/` | Version: as archived 2022

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GS; 1.1 GB single file named `.sfz` (SC-88 Pro based); kept as downloaded.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `1GB88Pro.sfz` | 1,140,868,812 (1.1 GB) | `8e7551bbb8a7e0f43378302fdca48e1d0e27b61e38454331fe07a0c711e68638` | [link](https://archive.org/download/1-gb-88-pro/1GB88Pro.sfz) |

### Roland Sound Canvas SoundFont (24-bit XGD Edition, "GMGSx")

Folder: `roland-sound-canvas-24bit-xgd/` | Version: 2020-12-17

License (as stated by the author): INFO chunk: "Public Domain" (W. D. Tharinda Perera); samples are Roland Sound Canvas material. **Local test asset only** (no redistribution license).

Note: GM + GS + XG drum kits; 24-bit SF2 (ifil 2.4) built around the SC-55 sound.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GMGSx 24-bit+XGD.sf2` | 61,219,732 (58.4 MB) | `a13cb79b4226f0eeb12f97c238713ecec89052d7963883f22ae424d3ee2951f0` | [link](https://archive.org/download/Roland-SoundCanvas-24-bit-XGDedit/GMGSx%2024-bit%2BXGD.sf2) |

### Roland MT-32 SF2 and MT-32 patch banks

Folder: `roland-mt32/` | Version: MT32.sf2 "very early release" (2006); SysEx patch collection (1997-1998)

License (as stated by the author): MT32.sf2 INFO chunk: "Zandro"; SysEx files: no license. **Local test asset only** (no redistribution license).

Note: MT-32 (pre-GM Roland LA synth); `MT32.sf2` is Zandro Reveille's sampled MT-32 bank. `roland_mt-32-patches.zip` holds per-game MT-32 timbre banks as `.SYX` (Sierra/LucasArts titles), useful to drive an MT-32 map.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `MT32.sf2` | 7,554,486 (7.2 MB) | `94b3cee6cff74f83970f73733a2295d20aa0ec230bc6c2c06f17cdeb0bc4f84c` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/MT32.sf2) |
| `roland_mt-32-patches.zip` | 532,786 (520.3 KB) | `2ecf245dff20085fdb55531fb23dcf2508eff8f60affd6b477489622d042be69` | [link](https://archive.org/download/roland_mt-32-patches/roland_mt-32-patches.zip) |
| `roland_mt-32-patches/` (directory, 47 files) | 1,429,150 (1.4 MB) | - | extracted from `roland_mt-32-patches.zip` |

### Collection of Yamaha XG SoundFonts / DLS

Folder: `yamaha-xg-collection/` | Version: zip of 2024-11

License (as stated by the author): INFO chunks: "Copyright 1998 Yamaha Corp." (K.C. Miyaoku SWP conversions), "Yamaha Corporation, Simone Piervergili". **Local test asset only** (no redistribution license).

Note: XG / GM; `mu2000.dls` (MU2000 set as DLS, 49 MB), `DLSbyXG.dls` (2.6 MB, Yamaha SoundMAX/DLSbyXG ROM-class set), `XG_Sound_Set_from_SoundMAX_DLSbyXG.sf2`, `Yamaha DB50XG Presets.sf2` (3.9 MB), `Yamaha XG Sound Set Ver.2.0.sf2`, `Yamaha S-YXG50_0.2.1.2.sf2`, `Yamaha_S-YXG50_GM_Soundfont_WIP.sf2`, `Yamaha FF8 GM.sf2`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Collection of Yamaha XG sound fonts .zip` | 217,118,644 (207.1 MB) | `6a1f994d68361b4b320ddf76e6ce7dcea02ecd6a0b5766688fc7b4d001966402` | [link](https://archive.org/download/collection-of-yamaha-xg-sound-fonts/Collection%20of%20Yamaha%20XG%20sound%20fonts%20.zip) |
| `Collection of Yamaha XG sound fonts /` (directory, 8 files) | 319,585,788 (304.8 MB) | - | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /DLSbyXG.dls` | 2,624,670 (2.5 MB) | `abf91987c9afab429e5b1ec04ba31b2ba8448521133b967a1385f624655ea184` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /XG_Sound_Set_from_SoundMAX_DLSbyXG.sf2` | 3,900,960 (3.7 MB) | `4bbeea1b02553ea42cc044bd1b1435e89d3d5d265b7d0789e2a3fc7014b0bfb8` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /Yamaha DB50XG Presets.sf2` | 3,863,418 (3.7 MB) | `d96255da8eb6cdbc80b09712b9bf2d7ca39c309a9a3dc565639d5abe72db83e5` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /Yamaha FF8 GM.sf2` | 9,444,256 (9.0 MB) | `b7de1e4dd918391045d2a5535e1df9ac9bb47d6b54fc680d2ce5b1057a21780c` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /Yamaha S-YXG50_0.2.1.2.sf2` | 52,830,264 (50.4 MB) | `e9e6d22f4dca83ca5602d878b83b855503978dbd2b625bf44e75e40799737c81` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /Yamaha XG Sound Set Ver.2.0.sf2` | 56,456,276 (53.8 MB) | `01ae3f8826b73b7acc757f8f90f0ee99e2770b01c4ba70be029361d1a6311ce0` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /Yamaha_S-YXG50_GM_Soundfont_WIP.sf2` | 141,235,998 (134.7 MB) | `6fadd62f105f0851c490cfa63acc70205dac5b46de92a2fc55c20a3af0c75e56` | extracted from `Collection of Yamaha XG sound fonts .zip` |
| `Collection of Yamaha XG sound fonts /mu2000.dls` | 49,229,946 (46.9 MB) | `38c953a1138caec5d28d2fae7e0fa1b45bcffeaa4efaf3f46d8f19efe8cf3437` | extracted from `Collection of Yamaha XG sound fonts .zip` |

### Yamaha XG Sound Set (Re-Map)

Folder: `yamaha-xg-sound-set/` | Version: remap by Zandro Reveille of K.C. Miyaoku's conversion

License (as stated by the author): INFO chunk: "Copyright 1998 Yamaha Corp.". **Local test asset only** (no redistribution license).

Note: XG / GS; 3.9 MB ROM-class XG set (the DB50XG / SW60XG-era wave set), remapped to the XG and GS standards. Same size class as `Yamaha DB50XG Presets.sf2`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Yamaha_XG_Sound_Set.sf2` | 3,853,314 (3.7 MB) | `5501c62c1cbcd805b8669a53808b499b5229aae205fb4e365b7231deeecf41e4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Yamaha_XG_Sound_Set.sf2) |

### Yamaha S-YXG50 SF3 and "Yamaland" GM/GS/XG banks

Folder: `yamaha-syxg50-yamaland/` | Version: 2026-07 uploads

License (as stated by the author): archive.org item marked CC0 by the uploader; INFO chunks: "Yamaha Corporation, louiejames". The samples are Yamaha (S-YXG50) and Roland (SC-8850) material. **Local test asset only** (no redistribution license).

Note: XG; `SYXG50 .sf3` is a 6.8 MB Ogg-compressed rendering of the S-YXG50 softsynth set; the three Yamaland banks mix S-YXG50 and SC-8850 sounds (GM/GS/XG).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SYXG50 .sf3` | 6,817,412 (6.5 MB) | `db03e05613e6bcf6627cf4b3776c8b445f9497edd25ae1185ce39db4262be5e0` | [link](https://archive.org/download/yamaha-syxg-50-roland-sc-8850/SYXG50%20.sf3) |
| `Yamaland GMGSXG.sf2` | 400,414,282 (381.9 MB) | `0f36dff70a79cfa5a7a1eed25bd8ad9d2fb4aca4a743a68b08efa85714aae491` | [link](https://archive.org/download/yamaha-syxg-50-roland-sc-8850/Yamaland%20GMGSXG.sf2) |
| `Yamaland final GMGSXG.sf2` | 282,239,482 (269.2 MB) | `dde60585c007cfc5c93d58e7bcd74d7021b9ef2a691b3382e0f78125ab027d5f` | [link](https://archive.org/download/yamaha-syxg-50-roland-sc-8850/Yamaland%20final%20GMGSXG.sf2) |
| `Yamaland new and improved gmgs soundfont.sf2` | 222,126,200 (211.8 MB) | `5ca93f17352595c2c00a4debcbb9a3a379774b93f1d82d66665cbd7f3080951d` | [link](https://archive.org/download/yamaha-syxg-50-roland-sc-8850/Yamaland%20new%20and%20improved%20gmgs%20soundfont.sf2) |

### Yamaha XG driver CD (S-YXG50, DS-XG)

Folder: `yamaha-s-yxg50-cd/` | Version: CD image as archived 2025

License (as stated by the author): Yamaha software, licensed with `License.txt` on the CD for the bundled hardware. **Local test asset only** (no redistribution license).

Note: XG; ISO image kept and unpacked with `7z` (nothing was run). It carries the S-YXG50 softsynth installer (`S-YXG50/`) and the DS-XG (YMF7x4) driver wave tables `DSXGWAVE.TBL` / `YDSXG.DAT` (the 2/4 MB XG ROM-class wave set of the Yamaha PCI cards).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `README.TXT` | 8,764 (8.6 KB) | `65ce1e68079206cdccc038df6b39da60ab08be67c957a6917141ea5376c3455d` | [link](https://archive.org/download/yamaha-xg/README.TXT) |
| `Yamaha XG.iso` | 384,925,696 (367.1 MB) | `12c4e8f539d45a427b991786362e1a19c4224d978a1a1f5c696c127db57bc687` | [link](https://archive.org/download/yamaha-xg/Yamaha%20XG.iso) |
| `Yamaha XG/` (directory, 1061 files) | 382,335,244 (364.6 MB) | - | extracted from `Yamaha XG.iso` |
| `Yamaha XG/DS1/LOGO'D DRIVER/95V1024/YDSXG.DAT` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/DS1/LOGO'D DRIVER/NTV6017/DRIVER/SXGWAVE2.TBL` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/DS1/WDM-DRV/DSXGWAVE.TBL` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/DS1/WIN95/YDSXG.DAT` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/DS1/WINNT/YDSXG.DAT` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/YMF744/NT40/YDSXG.DAT` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |
| `Yamaha XG/YMF744/Win95&98/ydsxg.dat` | 2,417,445 (2.3 MB) | `79dbc69c66019dbd03cab83f1dada93bfdab3af504bfce28c57684a2d1ec91df` | extracted from `Yamaha XG.iso` |

### Yamaha TG300 SoundFont

Folder: `yamaha-tg300/` | Version: 2023-10-04 (INFO date)

License (as stated by the author): INFO chunk: "SynthFont Viena" (sampled from a Yamaha TG300, a GS-compatible module). **Local test asset only** (no redistribution license).

Note: GM + GS (Yamaha's 1993 GS module); 357 MB 7z, 560 MB SF2.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Yamaha TG300.7z` | 357,004,695 (340.5 MB) | `0f5d651ab7034be8d88cf9ce5ef0462baf33adfd117f65e814226899efc5f72f` | [link](https://archive.org/download/yamaha-tg-300-sf2/Yamaha%20TG300.7z) |
| `Yamaha TG300/` (directory, 1 files) | 593,812,268 (566.3 MB) | - | extracted from `Yamaha TG300.7z` |
| `Yamaha TG300/Yamaha TG300.sf2` | 593,812,268 (566.3 MB) | `45a8acc1d31e1d4497a7b9e166d632b2545914a82e504fd95c1272ff059d9fff` | extracted from `Yamaha TG300.7z` |

### Yamaha Tyros 4 GM SoundFont ("Just T4")

Folder: `yamaha-tyros4/` | Version: 1.0 (2018)

License (as stated by the author): INFO chunk: "c 2018 mpj factory studios, quito, ecuador" (Milton Paredes); samples from a Yamaha Tyros 4. **Local test asset only** (no redistribution license).

Note: GM; 526 MB bank from the Tyros 4 arranger keyboard.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `yamaha tyros 4_just_t4_fixed.sf2` | 526,011,878 (501.6 MB) | `de5b1404630840a2a897e77c6083e2231c5d1523e6571a8afd972a4f684d36ce` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/yamaha%20tyros%204_just_t4_fixed.sf2) |

### Creative / E-mu banks beyond the 1/2/4/8 MB set

Folder: `creative-emu-extra/` | Version: various (1993 to 2006)

License (as stated by the author): INFO chunks: Copyright E-mu Systems, Inc. / Creative Technology; shipped free with Sound Blaster drivers, no open license. Community edits as stated. **Local test asset only** (no redistribution license).

Note: GM (+ GS / MT-32); `CREATIVE_28MBGM.sf2` (Creative 28 MB GM, Audigy/X-Fi era, 2006), `CREATIVE_8MBGM.SF2` (8MBGM Rev B), `Creative (emu10k1)8MBGMSFX.SF2` / `8MBGMSFX.SF2` (8MBGSFX Rev B, SB Live!), `Creative Labs 2M GM_2gmgsmt.sf2` (2GMGS Rev N), `ct2mgm.sf2` / `CT4MGM.SF2` (SB Live! 2 and 4 MB), `awe32.sf2` (AWE32 1 MB GM, 1993), `AweROMGM.sf2` (AWE32 ROM samples ripped to SF2), `8mbgm_enhanced18.sf2` (8MBGM improved by holbred), `AWE64.zip` (AWE64 CD sample MIDIs and the `SFBANK` banks).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `8MBGMSFX.SF2` | 7,557,598 (7.2 MB) | `6c2ff6e9219989e0a2d39e633cbdc7d8f8a575903985160495aeab5d01cc48e6` | [link](https://archive.org/download/soundfont-collection_202601/8MBGMSFX.SF2) |
| `8mbgm_enhanced-eng.txt` | 4,773 (4.7 KB) | `5191881c1bd2c78eec20b63b5d061123c3d433a61c669f2cbc8a48348a8760be` | [link](https://archive.org/download/soundfonts_201910/8mbgm_enhanced-eng.txt) |
| `8mbgm_enhanced-ger.txt` | 5,020 (4.9 KB) | `30791c168ac370609bd8175e5a1a414ecfae64a99c1046d24f3a7c3f5ef31e7f` | [link](https://archive.org/download/soundfonts_201910/8mbgm_enhanced-ger.txt) |
| `8mbgm_enhanced18.sf2` | 27,707,022 (26.4 MB) | `2ef5bd3eb377f8985eab77ddc76fcccae43c8b0a16c9d77338c9e0e8a2caa648` | [link](https://archive.org/download/soundfonts_201910/8mbgm_enhanced18.sf2) |
| `AWE ROM Readme.txt` | 4,410 (4.3 KB) | `2cfe575334b1017fb68ee8c86d8c96cafb571eb32f113f2b5d95053477fa8f4e` | [link](https://archive.org/download/soundfonts_201910/AWE%20ROM%20Readme.txt) |
| `AWE64.zip` | 9,368,942 (8.9 MB) | `b4da2ac3519acfe64a54fef911ef75baa5943aa561c9c53ab6eef34e658a023c` | [link](https://archive.org/download/awe-64_202606/AWE64.zip) |
| `AweROMGM.sf2` | 1,093,878 (1.0 MB) | `2b6fe34548d5f56e4f4f4497b1635a61936c7a15fddef72d4f31d9a859ae6474` | [link](https://archive.org/download/soundfonts_201910/AweROMGM.sf2) |
| `CREATIVE_28MBGM.sf2` | 29,705,938 (28.3 MB) | `3e5b1d704f9d252a85200cd3878b4c9ea46d6fcb5bfd54eb5edec1a70b01c6ca` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/CREATIVE_28MBGM.sf2) |
| `CREATIVE_8MBGM.SF2` | 7,582,896 (7.2 MB) | `c8debc3907334852df9c72c350310366b2eab48431042fc51ec877b307f268f4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/CREATIVE_8MBGM.SF2) |
| `CT4MGM.SF2` | 4,174,814 (4.0 MB) | `6c429ea4769c8dec705e6f766f6194fc14e3d8462368296b01a0768d669eb1c5` | [link](https://archive.org/download/SB-live-soundfonts/CT4MGM.SF2) |
| `Creative (emu10k1)8MBGMSFX.SF2` | 7,557,598 (7.2 MB) | `6c2ff6e9219989e0a2d39e633cbdc7d8f8a575903985160495aeab5d01cc48e6` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Creative%20%28emu10k1%298MBGMSFX.SF2) |
| `Creative Labs 2M GM_2gmgsmt.sf2` | 2,090,170 (2.0 MB) | `38807d06e9c92a9bbaeff60e9fa3e9f3300de6dcd04a9311c15266c3ae028d84` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Creative%20Labs%202M%20GM_2gmgsmt.sf2) |
| `awe32.sf2` | 1,090,280 (1.0 MB) | `de63e1a457fb64d11fbbfc9164a9be474c5625deb6841f7c8effecaa293bbe84` | [link](https://archive.org/download/soundfont-collection_202601/awe32.sf2) |
| `ct2mgm.sf2` | 2,167,684 (2.1 MB) | `cfa4b5899f118208b2a98b5cc0dd35a81d94b5ac4b44e4652308c2d5aa0d646f` | [link](https://archive.org/download/SB-live-soundfonts/ct2mgm.sf2) |
| `AWE64/` (directory, 31 files) | 10,497,555 (10.0 MB) | - | extracted from `AWE64.zip` |
| `AWE64/AWE64/SFBANK/2GMGSMT.SF2` | 2,090,170 (2.0 MB) | `38807d06e9c92a9bbaeff60e9fa3e9f3300de6dcd04a9311c15266c3ae028d84` | extracted from `AWE64.zip` |
| `AWE64/AWE64/SFBANK/4GMGSMT.SF2` | 4,174,814 (4.0 MB) | `6c429ea4769c8dec705e6f766f6194fc14e3d8462368296b01a0768d669eb1c5` | extracted from `AWE64.zip` |
| `AWE64/AWE64/SFBANK/GM35REVC.SF2` | 3,608,924 (3.4 MB) | `e71ade72b30bea21beb649fbd29cc72539079474e0068648a8cc2b81b67401f8` | extracted from `AWE64.zip` |

### Official Sound Blaster SoundFont archive

Folder: `creative-sound-blaster-archive/` | Version: as archived 2022-09 (16 banks)

License (as stated by the author): INFO chunks: E-mu Systems / Creative Technology. Shipped free with Sound Blaster drivers. **Local test asset only** (no redistribution license).

Note: GM (+ GS / MT-32); the complete official set in one place: `1MGM` (AWE32), `2GMGSMT` / `2MBGMGS` (2 MB Rev N), `4GMGSMT`, `4MBGM`, `4MBGMSFX`, `8MBGM`, `8MBGMSFX`, `CT2MGM`, `CT4MGM`, `CT8MGM` (SB Live! / Audigy), `CT28MBGM` and `CT28MBGM_fixed` (X-Fi), `EMUAPS_2meg_version` / `EmuAPS_8MB` (E-mu APS card), `GM35REVC` (Creative 3.5 MB GM). Some files duplicate `creative-gm/` and `creative-emu-extra/` byte for byte (compare the SHA-256).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `1MGM.SF2` | 1,090,280 (1.0 MB) | `de63e1a457fb64d11fbbfc9164a9be474c5625deb6841f7c8effecaa293bbe84` | [link](https://archive.org/download/sound_blaster_archive/1MGM.SF2) |
| `2GMGSMT.SF2` | 2,090,170 (2.0 MB) | `38807d06e9c92a9bbaeff60e9fa3e9f3300de6dcd04a9311c15266c3ae028d84` | [link](https://archive.org/download/sound_blaster_archive/2GMGSMT.SF2) |
| `2MBGMGS.SF2` | 2,090,170 (2.0 MB) | `4c7576aeaec8397a6e9a0f57186ba1f7dae5e78b41d2440dc22b3530f9f4cf82` | [link](https://archive.org/download/sound_blaster_archive/2MBGMGS.SF2) |
| `4GMGSMT.SF2` | 4,174,814 (4.0 MB) | `6c429ea4769c8dec705e6f766f6194fc14e3d8462368296b01a0768d669eb1c5` | [link](https://archive.org/download/sound_blaster_archive/4GMGSMT.SF2) |
| `4MBGM.SF2` | 4,181,354 (4.0 MB) | `4d14d0284d047b0d486c5f4430e58a7bc03199af91dcf548a96d7a8b9cbb1b25` | [link](https://archive.org/download/sound_blaster_archive/4MBGM.SF2) |
| `4MBGMSFX.SF2` | 4,189,008 (4.0 MB) | `5ecbcefc9d7ce90085969d26ba5a27b957ec90cf6563db961cad728e5c3b433d` | [link](https://archive.org/download/sound_blaster_archive/4MBGMSFX.SF2) |
| `8MBGM.SF2` | 7,582,896 (7.2 MB) | `c8debc3907334852df9c72c350310366b2eab48431042fc51ec877b307f268f4` | [link](https://archive.org/download/sound_blaster_archive/8MBGM.SF2) |
| `8MBGMSFX.SF2` | 7,572,224 (7.2 MB) | `538f60cd87e22320b15535fb785da29b58532245697ba0fb23655bd399935d00` | [link](https://archive.org/download/sound_blaster_archive/8MBGMSFX.SF2) |
| `CT28MBGM.SF2` | 29,705,938 (28.3 MB) | `3e5b1d704f9d252a85200cd3878b4c9ea46d6fcb5bfd54eb5edec1a70b01c6ca` | [link](https://archive.org/download/sound_blaster_archive/CT28MBGM.SF2) |
| `CT28MBGM_fixed.SF2` | 29,721,254 (28.3 MB) | `9f0f8f652d0a4a32b2546f19c3714e0823d9950a45f180d69f72bd19dfee6d2b` | [link](https://archive.org/download/sound_blaster_archive/CT28MBGM_fixed.SF2) |
| `CT2MGM.SF2` | 2,167,684 (2.1 MB) | `cfa4b5899f118208b2a98b5cc0dd35a81d94b5ac4b44e4652308c2d5aa0d646f` | [link](https://archive.org/download/sound_blaster_archive/CT2MGM.SF2) |
| `CT4MGM.SF2` | 4,174,814 (4.0 MB) | `6c429ea4769c8dec705e6f766f6194fc14e3d8462368296b01a0768d669eb1c5` | [link](https://archive.org/download/sound_blaster_archive/CT4MGM.SF2) |
| `CT8MGM.SF2` | 7,572,224 (7.2 MB) | `538f60cd87e22320b15535fb785da29b58532245697ba0fb23655bd399935d00` | [link](https://archive.org/download/sound_blaster_archive/CT8MGM.SF2) |
| `EMUAPS_2meg_version.SF2` | 2,160,454 (2.1 MB) | `68b74f640a050d0cdbe1716977fa904da1a45c20525ef141d03b6145c59a07fd` | [link](https://archive.org/download/sound_blaster_archive/EMUAPS_2meg_version.SF2) |
| `EmuAPS_8MB.sf2` | 7,604,214 (7.3 MB) | `80dece1bf413b9976ec2a1dc7844a90ebef241cefd6a8527832cc2d13f4cfed8` | [link](https://archive.org/download/sound_blaster_archive/EmuAPS_8MB.sf2) |
| `GM35REVC.SF2` | 3,608,924 (3.4 MB) | `e71ade72b30bea21beb649fbd29cc72539079474e0068648a8cc2b81b67401f8` | [link](https://archive.org/download/sound_blaster_archive/GM35REVC.SF2) |

### E-mu Classic Series Vol. 14: ESI-32 General MIDI Collection

Folder: `emu-esi32-gm/` | Version: E-mu sample CD content (WAV + SoundFont), uploaded 2026

License (as stated by the author): Copyright E-mu Systems (commercial sample library). **Local test asset only** (no redistribution license).

Note: GM; the E-mu ESI-32 sampler GM collection as WAV samples plus SoundFont files (3,280 files).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Vol. 14 – ESI-32 General Midi Collection.zip` | 57,818,929 (55.1 MB) | `fb61c5855d5eb3fd36ae4b364b2d0a222ede52a0c1dbcc3650726e4f2bdb1e70` | [link](https://archive.org/download/vol.-14-esi-32-general-midi-collection/Vol.%2014%20%E2%80%93%20ESI-32%20General%20Midi%20Collection.zip) |
| `Vol. 14 – ESI-32 General Midi Collection/` (directory, 3280 files) | 65,438,649 (62.4 MB) | - | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/1.3M Drums+SFX X/1.3M Drums+SFX X.sf2` | 1,349,710 (1.3 MB) | `14914529ad9c75c2ca05916c06bac40e592e077245b6d0e8edd7cb3545ce8aa2` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/2.5M Drums+SFX X/2.5M Drums+SFX X.sf2` | 2,760,086 (2.6 MB) | `995c1d5be164909b06bf4ee9e5e63b316bde39ced860412ea2577768fd8bdce6` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/2M GM + SFX    X/2M GM + SFX    X.sf2` | 2,065,374 (2.0 MB) | `2ff9210845329532695c32e1d5902088fdb5f5dd70fbb5cf2d9a9df0a9b6d190` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/4M GM + SFX    X/4M GM + SFX    X.sf2` | 4,340,676 (4.1 MB) | `8e1673795c12fb502ca3582745501811d7ea75d77bfecfa1b53e2dc30c4aa2c6` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/4M GeneralMidi X/4M GeneralMidi X.sf2` | 4,208,030 (4.0 MB) | `097350493336878b2b8e6d66797221e54cb4fb08bc0bfdb9fe9cf4854347a364` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/8M GM + SFX    X/8M GM + SFX    X.sf2` | 7,917,806 (7.6 MB) | `63219d5d739117cfacd155820441948a9fb1a2f49feab943c5ef00eb8f743a7d` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/8M GeneralMidi X/8M GeneralMidi X.sf2` | 8,149,768 (7.8 MB) | `7c6a037d55c46addce17107dda845afb475ec91fc34403cf80998f2af5b85e61` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |
| `Vol. 14 – ESI-32 General Midi Collection/Vol. 14 – ESI-32 General Midi Collection/Designed by S&M/General Midi   X/General Midi   X.sf2` | 376 (376 B) | `ace4fe65c648f6209f9e7c1589ff74640964672a830d7c9f5e4c87005b49d4d0` | extracted from `Vol. 14 – ESI-32 General Midi Collection.zip` |

### Ensoniq AudioPCI 8 MB GM set (EAPCI8M) converted to SF2

Folder: `ensoniq-eapci8m/` | Version: two conversions by Hedsound (2019)

License (as stated by the author): No license text (the Ensoniq/Creative AudioPCI wavetable). **Local test asset only** (no redistribution license).

Note: GM; the Ensoniq AudioPCI / SB PCI64/128 software-wavetable set, ~6.8 MB, a ROM-class set like the SAM2695's.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `eapci8m-sf2-Hedsound-v2.sf2` | 6,824,786 (6.5 MB) | `6041c65fef85726e3302e126286b9d04221f12a02f78ef196ec92184a4e3b091` | [link](https://archive.org/download/EAPCI8M-SF2/eapci8m-sf2-Hedsound-v2.sf2) |
| `eapci8m-sf2-Hedsound.sf2` | 6,830,864 (6.5 MB) | `b56f5b8e91809c727cc49ceb33c6ff225e95823f785aafa4e9756257acccd20e` | [link](https://archive.org/download/EAPCI8M-SF2/eapci8m-sf2-Hedsound.sf2) |

### "Reyna SE / 2695 SF2" (fan-made SAM2695-style bank)

Folder: `dream-sam2695-sf2/` | Version: as archived (INFO date "Friday 6 September")

License (as stated by the author): archive.org item marked CC BY-NC-ND 4.0; INFO chunk: "pk-tj10zhd" (Justin/TJ10z). **Local test asset only** (no redistribution license).

Note: GM; 20 MB SF2 uploaded as "Dream France sound module soundfonts" with the file name `sam2695.sf2`. The INFO chunk shows a community build (Polyphone), not a dump of the Dream CleanWave ROM; treat it as an approximation of the DreamBlaster S2 sound, not a reference.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `sam2695.sf2` | 20,368,642 (19.4 MB) | `af3371d87660d45f0f1b6497188a07127143e68db3b51ff8ff37634238ccebc9` | [link](https://archive.org/download/dreamfrancesoundmodulesf2/sam2695.sf2) |

### GeneralUser GS, older versions

Folder: `generaluser-gs-older/` | Version: 1.35, 1.4, 1.44 (SoftSynth / FluidSynth / Live-Audigy), 1.442 (MuseScore), 1.471, 2.0.2 (doc r4), 2.0.3 beta

License (as stated by the author): 1.35: own license (no redistribution for profit); 1.4 and later: GeneralUser GS License v2.0 (free use, may be repackaged).

Note: Historical versions of the default bank for A/B tests against 2.0.3. The `GeneralUser-GS.zip` upload (archive.org, 2026) holds 2.0.3 BETA and a third-party variant with the GMGSx snare. Older author zips came from web.archive.org snapshots of schristiancollins.com. `GeneralUser GS v1.471.sfArk` unpacks to exactly the same bytes as `GeneralUser GS v1.471.sf2` (unpacked copy removed).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GeneralUser GS 1.35.license.txt` | 1,385 (1.4 KB) | `74f19f14673365fbc0f8d6f5c5080395ea44c68e5de87f72e8a83a544abb8e14` | [link](https://archive.org/download/soundfont-collection_202601/creative/sf2/GeneralUser%20GS%201.35.license.txt) |
| `GeneralUser GS 1.35.sf2` | 26,605,360 (25.4 MB) | `cb08df013f4c0896b553446c4f0f9925400322a3fddb7f7d2486b1e3b94a76f5` | [link](https://archive.org/download/soundfont-collection_202601/creative/sf2/GeneralUser%20GS%201.35.sf2) |
| `GeneralUser GS 1.35.txt` | 2,787 (2.7 KB) | `2f4f05c5fc36e34f1aa1233d551254589d91e289621f23f5f1357781c96749ce` | [link](https://archive.org/download/soundfont-collection_202601/creative/sf2/GeneralUser%20GS%201.35.txt) |
| `GeneralUser GS 1.4.sf2` | 31,414,772 (30.0 MB) | `2e605989621eaf59137dbfa872d86e500ca75865d97c79046f532af24cd371ca` | [link](https://archive.org/download/soundfonts_201910/GeneralUser%20GS%201.4.sf2) |
| `GeneralUser GS v1.471.sf2` | 31,281,186 (29.8 MB) | `f45b6b4a68b6bf3d792fcbb6d7de24dc701a0f89c5900a21ef3aaece993b839a` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/GeneralUser%20GS%20v1.471.sf2) |
| `GeneralUser GS v1.471.sfArk` | 17,485,084 (16.7 MB) | `558e426b7440e94df87bac0a1bfe735c4cf08b1f3d0f1db2d24bef804b25f2bb` | [link](https://archive.org/download/GMSoundfonts/GeneralUser%20GS%20v1.471.sfArk) |
| `GeneralUser-GS.zip` | 58,354,269 (55.7 MB) | `3c9615910d3d3949a2e8a8f7b6a42fc706c6f2bcb6bcb66e97dfc4b4064bfbc9` | [link](https://archive.org/download/general-user-gs/GeneralUser-GS.zip) |
| `GeneralUser_GS_1.35.zip` | 23,864,599 (22.8 MB) | `193281ec1bae9be038d66409743d8aaddb435c569a7449c8a8b067faf6a0570d` | [link](https://web.archive.org/web/20191231090726id_/http://schristiancollins.com/soundfonts/GeneralUser_GS_1.35.zip) |
| `GeneralUser_GS_1.44-FluidSynth.zip` | 28,596,599 (27.3 MB) | `8a9ad3582c8a9de8a493f0de95d2207ff4857351fb0f98e529797f4e2598358d` | [link](https://web.archive.org/web/20140904185819id_/http://www.schristiancollins.com/soundfonts/GeneralUser_GS_1.44-FluidSynth.zip) |
| `GeneralUser_GS_1.44-Live-Audigy.zip` | 28,604,075 (27.3 MB) | `7acfc3a13b24e77d9942042463c243aebe4a99f5d7078f3da1650fab3ff7fa5b` | [link](https://web.archive.org/web/20160818131921id_/http://www.schristiancollins.com/soundfonts/GeneralUser_GS_1.44-Live-Audigy.zip) |
| `GeneralUser_GS_1.44-SoftSynth.zip` | 28,593,190 (27.3 MB) | `68cb601ffbbe7fb67d2454135bd62b194ac43b932246fe1e408cda2c5525710d` | [link](https://web.archive.org/web/20130712141025id_/http://www.schristiancollins.com/soundfonts/GeneralUser_GS_1.44-SoftSynth.zip) |
| `GeneralUser_GS_1.442-MuseScore.zip` | 28,397,777 (27.1 MB) | `2b47a269ce6f64c6774ce95587a39e882e42181617ffb8f53f065d2fb2c88216` | [link](https://web.archive.org/web/20140828155733id_/http://schristiancollins.com/soundfonts/GeneralUser_GS_1.442-MuseScore.zip) |
| `GeneralUser_GS_SoftSynth_v1.44.sf2` | 31,279,314 (29.8 MB) | `00725296486f652ba42bdbf2098254762ca80239353d04b55a27454fc8edb8a8` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/GeneralUser_GS_SoftSynth_v1.44.sf2) |
| `GeneralUser_GS_v2.0.2--doc_r4.zip` | 62,620,243 (59.7 MB) | `a1b17a4362558e91777bea878a1945419ae680b9e59a5ac0d1fd300bce4bdc2a` | [link](https://archive.org/download/general-user-gs-v-2.0.2-doc-r-4/GeneralUser_GS_v2.0.2--doc_r4.zip) |
| `GeneralUser-GS/` (directory, 2 files) | 64,651,154 (61.7 MB) | - | extracted from `GeneralUser-GS.zip` |
| `GeneralUser-GS/GeneralUser-GS/GeneralUser GS but the snare is the GMGSx snare.sf2` | 32,331,758 (30.8 MB) | `bd67abe1b86448899c29f2a4e05b631f13b7a84c8e604deb5265a383dc80f890` | extracted from `GeneralUser-GS.zip` |
| `GeneralUser-GS/GeneralUser-GS/GeneralUser-GS.sf2` | 32,319,396 (30.8 MB) | `9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe` | extracted from `GeneralUser-GS.zip` |
| `GeneralUser_GS_1.35/` (directory, 6 files) | 26,624,143 (25.4 MB) | - | extracted from `GeneralUser_GS_1.35.zip` |
| `GeneralUser_GS_1.35/GeneralUser GS 1.35/GeneralUser GS 1.35.sf2` | 26,605,360 (25.4 MB) | `06e41ed756687741f2c4d5832103a804b9356a506265911320a15ed831a3b132` | extracted from `GeneralUser_GS_1.35.zip` |
| `GeneralUser_GS_1.44-FluidSynth/` (directory, 15 files) | 31,710,337 (30.2 MB) | - | extracted from `GeneralUser_GS_1.44-FluidSynth.zip` |
| `GeneralUser_GS_1.44-FluidSynth/GeneralUser GS 1.44 FluidSynth/GeneralUser GS FluidSynth v1.44.sf2` | 31,276,230 (29.8 MB) | `a5562699004e660facc8c0ed9f08ea0331413ea3f92e1e37b14fbd75ee84756e` | extracted from `GeneralUser_GS_1.44-FluidSynth.zip` |
| `GeneralUser_GS_1.44-Live-Audigy/` (directory, 19 files) | 31,742,399 (30.3 MB) | - | extracted from `GeneralUser_GS_1.44-Live-Audigy.zip` |
| `GeneralUser_GS_1.44-Live-Audigy/GeneralUser GS 1.44 Live-Audigy/GeneralUser GS Live-Audigy v1.44.sf2` | 31,288,762 (29.8 MB) | `e4b56107e431bfa56f05cf04d0227a4d0c66ccb9ee35ff5e6fd87241ad4e8dc0` | extracted from `GeneralUser_GS_1.44-Live-Audigy.zip` |
| `GeneralUser_GS_1.44-SoftSynth/` (directory, 15 files) | 31,713,421 (30.2 MB) | - | extracted from `GeneralUser_GS_1.44-SoftSynth.zip` |
| `GeneralUser_GS_1.44-SoftSynth/GeneralUser GS 1.44 SoftSynth/GeneralUser GS SoftSynth v1.44.sf2` | 31,279,314 (29.8 MB) | `00725296486f652ba42bdbf2098254762ca80239353d04b55a27454fc8edb8a8` | extracted from `GeneralUser_GS_1.44-SoftSynth.zip` |
| `GeneralUser_GS_1.442-MuseScore/` (directory, 6 files) | 31,308,562 (29.9 MB) | - | extracted from `GeneralUser_GS_1.442-MuseScore.zip` |
| `GeneralUser_GS_1.442-MuseScore/GeneralUser GS 1.442 MuseScore/GeneralUser GS MuseScore v1.442.sf2` | 31,277,462 (29.8 MB) | `d910e139f619048b331d72d2e0867f1729f9725e2a97a6be32b09b8b5b5c4b12` | extracted from `GeneralUser_GS_1.442-MuseScore.zip` |
| `GeneralUser_GS_v2.0.2--doc_r4/` (directory, 36 files) | 66,498,298 (63.4 MB) | - | extracted from `GeneralUser_GS_v2.0.2--doc_r4.zip` |
| `GeneralUser_GS_v2.0.2--doc_r4/GeneralUser-GS/GeneralUser-GS.sf2` | 32,322,864 (30.8 MB) | `c278464b823daf9c52106c0957f752817da0e52964817ff682fe3a8d2f8446ce` | extracted from `GeneralUser_GS_v2.0.2--doc_r4.zip` |

### Virtue (S. Christian Collins)

Folder: `virtue/` | Version: 1.05 (AWE/Live!) and 1.06 (Audigy/APS), 2001

License (as stated by the author): Own license in the INFO chunk (free use for music creation, same author as GeneralUser GS).

Note: GM; tiny (~0.8 MB) ROM-class GM bank designed for Sound Blaster AWE/Live!/Audigy, size class of a mask-ROM GM chip.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Virtue_1.05-AWE-Live.zip` | 828,852 (809.4 KB) | `51eb5869de1eaad06dce4b0e389cf4642bcef1e6e53a1e4aae5125eeac9bacd4` | [link](https://web.archive.org/web/20221222032512id_/https://www.schristiancollins.com/soundfonts/Virtue_1.05-AWE-Live.zip) |
| `Virtue_1.06-Audigy-APS.zip` | 829,829 (810.4 KB) | `3b99fa6d79ce930d77f8f3be410ab1c672a474ca9499432adce205ffedaa9407` | [link](https://web.archive.org/web/20221222032512id_/https://www.schristiancollins.com/soundfonts/Virtue_1.06-Audigy-APS.zip) |
| `Virtue_1.05-AWE-Live/` (directory, 1 files) | 945,660 (923.5 KB) | - | extracted from `Virtue_1.05-AWE-Live.zip` |
| `Virtue_1.05-AWE-Live/Virtue 1.05 for AWE & Live!.sf2` | 945,660 (923.5 KB) | `1ee1904cdcd56bc347a500abaeee69db43a7e7f1e659789b906c5af66074549c` | extracted from `Virtue_1.05-AWE-Live.zip` |
| `Virtue_1.06-Audigy-APS/` (directory, 1 files) | 961,098 (938.6 KB) | - | extracted from `Virtue_1.06-Audigy-APS.zip` |
| `Virtue_1.06-Audigy-APS/Virtue 1.06 for Audigy & APS.sf2` | 961,098 (938.6 KB) | `d1a809f08e5e67ba02ad15d246a28ee9b70aaa4adc91f23d43552c68d6d70d6a` | extracted from `Virtue_1.06-Audigy-APS.zip` |

### SGM (Shan's GM) variants

Folder: `sgm-variants/` | Version: SGM-128 1.17, SGM-150 1.3, SGM-180 1.5, SGM-V2 beta (sfpack, 2007); SGM Pro 17, X64 v17, X28 v17, ES8C (2026 zips); SGM-v2.01 mods

License (as stated by the author): archive.org items marked CC BY-ND 4.0 (SGM Pro/X64/X28/ES8) or no license; original SGM: "David Shan 2002-2007". **Local test asset only** (no redistribution license).

Note: GM; the SGM family besides `sgm-v2-01/`: older `.sfpack` releases (kept packed: no free sfpack unpacker), new compact builds (zips unpacked), and community mods of V2.01 (`NicePianosGuitarsBass`, `CompactGrand-Guit-Bass`, `HQ v3.0`, `Steinway-Guit-Bass`, `GuitsPlusBass`, `XG 1.08`; sfArk files unpacked with sfarkxtc).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SGM-128 1.17.sfpack` | 61,361,480 (58.5 MB) | `73709a0e15099b2306eec2104444631ed18e30951b4537cdbb81ee4fe100fd19` | [link](https://archive.org/download/SGM_older/SGM-128%201.17.sfpack) |
| `SGM-150 1.3.sfpack` | 69,955,002 (66.7 MB) | `5f71c2337e7f4d7e1643dbc228787d7dd0fce515c0aa0f8d783060d2af135207` | [link](https://archive.org/download/SGM_older/SGM-150%201.3.sfpack) |
| `SGM-180 1.5.sfpack` | 80,507,682 (76.8 MB) | `979ea699bf8fdce58eaf915edb37ad23fbb37d0276878e0036e732b69bbc76ad` | [link](https://archive.org/download/SGM_older/SGM-180%201.5.sfpack) |
| `SGM-V2 Beta (070511).sfpack` | 109,662,512 (104.6 MB) | `d4a12cea9b11364b9713c6e7945573a0fbf3b1a4f05dd0e86cf4cb51e4bafe56` | [link](https://archive.org/download/SGM_older/SGM-V2%20Beta%20%28070511%29.sfpack) |
| `SGM-V2.01-XG-1.08.sf2` | 260,864,942 (248.8 MB) | `22cb3d9a445cf3779c97cec3de09d9d2842b61b5d9ef3b999c9c87219b5abeb4` | unpacked from `SGM-V2.01-XG-1.08.sfArk` with sfarkxtc |
| `SGM-V2.01-XG-1.08.sfArk` | 113,657,682 (108.4 MB) | `a40d1a94e5f433be5f41adb4ada9de79343024213fdcae07b8ad875b4339cd9a` | [link](https://archive.org/download/GMSoundfonts/SGM-V2.01-XG-1.08.sfArk) |
| `SGM-v2.01-CompactGrand-Guit-Bass-v2.7.sf2` | 332,340,658 (316.9 MB) | `85a36e94bd5614c87e7b1f95ff781296ae599d42052e49a932aabb6fc314e408` | [link](https://archive.org/download/sgm-v-2.01-compact-grand-guit-bass-v-2.7/SGM-v2.01-CompactGrand-Guit-Bass-v2.7.sf2) |
| `SGM-v2.01-HQ-v3.0.sf2` | 363,012,354 (346.2 MB) | `0ca929ba416bbb18054c88385635f763852177dd6ae37fb506a23b9f93247ae0` | unpacked from `SGM-v2.01-HQ-v3.0.sfArk` with sfarkxtc |
| `SGM-v2.01-HQ-v3.0.sfArk` | 138,260,954 (131.9 MB) | `c6b7a990aa8bea1926105d3ef72683e45cabbc41935815c158e0b3a52a7729f6` | [link](https://archive.org/download/GMSoundfonts/SGM-v2.01-HQ-v3.0.sfArk) |
| `SGM-v2.01-NicePianosGuitarsBass-V1.2.sf2` | 324,800,670 (309.8 MB) | `1b999795b8006c323e490596eb8c41acc0c50598748cb8bee9e71d75b54a6088` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/SGM-v2.01-NicePianosGuitarsBass-V1.2.sf2) |
| `SGM-v2.01-Steinway-Guit-Bass-v2.9.sf2` | 525,236,122 (500.9 MB) | `4690312d96b890abc8cfc0356397db63fd284e40b4420c6b5808f53dfe96704b` | unpacked from `SGM-v2.01-Steinway-Guit-Bass-v2.9.sfArk` with sfarkxtc |
| `SGM-v2.01-Steinway-Guit-Bass-v2.9.sfArk` | 187,388,830 (178.7 MB) | `db2933f997378a0c484a98a2debeb0b957b41d83669c2a1ee56f3e628f42014c` | [link](https://archive.org/download/GMSoundfonts/SGM-v2.01-Steinway-Guit-Bass-v2.9.sfArk) |
| `SGMv2.01-GuitsPlusBass-V1.4.sf2` | 315,374,424 (300.8 MB) | `f96d0dd6fb8468d91ca6dd72863f559136204e10b3d8a6533b0c1b545598e421` | unpacked from `SGMv2.01-GuitsPlusBass-V1.4.sfArk` with sfarkxtc |
| `SGMv2.01-GuitsPlusBass-V1.4.sfArk` | 121,491,122 (115.9 MB) | `25c5ed234e513db613c5aba2c9b8490710964f01e5a79d09ad7841c72757811d` | [link](https://archive.org/download/GMSoundfonts/SGMv2.01-GuitsPlusBass-V1.4.sfArk) |
| `Shan SGM ES8C Soundfont.zip` | 6,978,001 (6.7 MB) | `2c48dfbf990bfc06b78c7ef604c1d6744c35e2c8c5c3a47c9c72844861a94e8c` | [link](https://archive.org/download/SGM-X48/Shan%20SGM%20ES8C%20Soundfont.zip) |
| `Shan SGM Pro 17 Soundfont.zip` | 106,520,754 (101.6 MB) | `da34d0dd7342809285eef689f42fa3f02da3c20447e4ea28798374dc4fe1d58e` | [link](https://archive.org/download/SGM-X48/Shan%20SGM%20Pro%2017%20Soundfont.zip) |
| `Shan SGM X28 v17 Soundfont.zip` | 25,149,429 (24.0 MB) | `2b6e5082bec51a41ab8ccf663ece5dc01639117f2c3c568cd2e1ac4bcbf68969` | [link](https://archive.org/download/SGM-X48/Shan%20SGM%20X28%20v17%20Soundfont.zip) |
| `Shan SGM X64 v17 Soundfont.zip` | 56,528,548 (53.9 MB) | `6a14c9fc83f80c66bc952a89a60dbafa5486ff7944ca3d53fe779c4436302b66` | [link](https://archive.org/download/SGM-X48/Shan%20SGM%20X64%20v17%20Soundfont.zip) |
| `Shan SGM ES8C Soundfont/` (directory, 2 files) | 7,466,000 (7.1 MB) | - | extracted from `Shan SGM ES8C Soundfont.zip` |
| `Shan SGM ES8C Soundfont/Shan SGM ES8C.SF2` | 7,465,066 (7.1 MB) | `3d43509cda990de8e6db851b2ba8220940800d46d2f09a0e9b1ee8baa678eaf4` | extracted from `Shan SGM ES8C Soundfont.zip` |
| `Shan SGM Pro 17 Soundfont/` (directory, 3 files) | 125,100,195 (119.3 MB) | - | extracted from `Shan SGM Pro 17 Soundfont.zip` |
| `Shan SGM Pro 17 Soundfont/Shan SGM Pro 17.SF2` | 125,095,324 (119.3 MB) | `b9d12a4e4df88ff23bc8a761a2893f57977a51f405b7e8f55ef3589587900e8b` | extracted from `Shan SGM Pro 17 Soundfont.zip` |
| `Shan SGM X28 v17 Soundfont/` (directory, 2 files) | 27,998,465 (26.7 MB) | - | extracted from `Shan SGM X28 v17 Soundfont.zip` |
| `Shan SGM X28 v17 Soundfont/Shan SGM X28 v17.SF2` | 27,995,440 (26.7 MB) | `eaef89cf30c92e91102777789b29127b2a6ab293daa61aa2b8b030b4ddc1c5e1` | extracted from `Shan SGM X28 v17 Soundfont.zip` |
| `Shan SGM X64 v17 Soundfont/` (directory, 2 files) | 66,734,565 (63.6 MB) | - | extracted from `Shan SGM X64 v17 Soundfont.zip` |
| `Shan SGM X64 v17 Soundfont/Shan SGM X64 v17.SF2` | 66,732,308 (63.6 MB) | `703d610026f26701ea49368dd2b6f89537cd55ebcfaba285fd1cf03fbf67d816` | extracted from `Shan SGM X64 v17 Soundfont.zip` |

### ColomboGMGS2 (W. D. Tharinda Perera)

Folder: `colombo-gmgs2/` | Version: v15.0 (`v15/`) and the upload titled V16.7 (`v16-7/`, its INFO says V17.0)

License (as stated by the author): CC BY-SA 4.0 (INFO chunk and archive.org items).

Note: GM + GS (+ GM2/XG maps); ~270 MB bank assembled from many free banks; demo MIDIs and SONAR instrument definitions included.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `v15/ColomboGMGS2.sf2` | 270,158,766 (257.6 MB) | `ea8c1e28b1c0e959344c385505d6ca414d5c453024a8c7b86e1016921fd06ab2` | [link](https://archive.org/download/ColomboGMGS2/ColomboGMGS2.sf2) |
| `v15/ColomboGMGS2_SONAR.zip` | 527,787 (515.4 KB) | `d3a5a41a5abd1a3598b84588db39b04cccf36844bf7a89b8930f25d019b6ec86` | [link](https://archive.org/download/ColomboGMGS2/ColomboGMGS2_SONAR.zip) |
| `v15/ColomboGMGSLyt.rar` | 484,000 (472.7 KB) | `a8b5f03b89437b6b258bae984f58242f349f6b68b069b780faa93f609b5c7376` | [link](https://archive.org/download/ColomboGMGS2/ColomboGMGSLyt.rar) |
| `v15/Demo MIDIs.zip` | 1,725,106 (1.6 MB) | `2636794fb673a761310f8b2a384a9347554a3a69167e6caced90c6c59115d7b8` | [link](https://archive.org/download/ColomboGMGS2/Demo%20MIDIs.zip) |
| `v16-7/ColomboGMGS2.sf2` | 273,910,116 (261.2 MB) | `bb33fd4158e725f1ca2fde9f27df992b2b5abd3a43bcf590017e71d5d4ce9262` | [link](https://archive.org/download/dawnsgmgs2/ColomboGMGS2.sf2) |
| `v16-7/ColomboGMGS2_SONAR.rar` | 519,880 (507.7 KB) | `754e02ad14df80676682954d6e49ee8d8fdd131684f43cdb3f45722cd1a2c956` | [link](https://archive.org/download/dawnsgmgs2/ColomboGMGS2_SONAR.rar) |
| `v16-7/ColomboGMGSLyt.rar` | 366,786 (358.2 KB) | `6f836986137df1b0bc5b696db12da7b961602b3e749da8e5a97451da55c0e98f` | [link](https://archive.org/download/dawnsgmgs2/ColomboGMGSLyt.rar) |
| `v16-7/Demo MIDIs.zip` | 1,982,041 (1.9 MB) | `8eb072fa27eb55fae3857771750add4429ba99a21ee70e0c2104d773a3273488` | [link](https://archive.org/download/dawnsgmgs2/Demo%20MIDIs.zip) |

### UHD3 (Unison HD)

Folder: `uhd3/` | Version: Revision 3.31 (2018); `UHD3.7z` from the ScummVM-labelled upload by CWadge

License (as stated by the author): INFO chunk: "Peter Jevnisek and Chris Wadge", "Not for commercial distribution". **Local test asset only** (no redistribution license).

Note: GM + GS (128 GM + 31 GS variations, 10 kits); HD successor of Unison.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `UHD3.7z` | 49,340,828 (47.1 MB) | `c6b9d8eb9efe0da50babf52accf600d315b43fc945a13376bb35640ca9b0f749` | [link](https://archive.org/download/scummvm_202510/UHD3.7z) |
| `UHD3.sf2` | 67,413,184 (64.3 MB) | `15ac29ae03193f9660f7b4a283528292893921de33465d764d84e8acda5b81f2` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/UHD3.sf2) |
| `UHD3/` (directory, 1 files) | 67,413,184 (64.3 MB) | - | extracted from `UHD3.7z` |
| `UHD3/UHD3.sf2` | 67,413,184 (64.3 MB) | `15ac29ae03193f9660f7b4a283528292893921de33465d764d84e8acda5b81f2` | extracted from `UHD3.7z` |

### Crisis General MIDI

Folder: `crisis-general-midi/` | Version: 3.01 (2006-02-02)

License (as stated by the author): INFO chunk: "Free, not for commercial use!" (Chris Maricourt). **Local test asset only** (no redistribution license).

Note: GM; 1.69 GB realistic bank; kept as SF2, as the original 7z and as `.sfpack`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `CrisisGeneralMidi3.01.7z` | 1,006,555,773 (959.9 MB) | `0d37573d381140d95d4ed12ff4dfe447c0e94a5ac2db501505fded1b85db24d6` | [link](https://archive.org/download/crisis-general-midi-3.01/CrisisGeneralMidi3.01.7z) |
| `CrisisGeneralMidi3.01.sf2` | 1,689,192,084 (1.6 GB) | `a382036696e065fafe37797b804ca8e2b56155682de4f54b084d6636807a82f5` | [link](https://archive.org/download/crisis-general-midi-3.01/CrisisGeneralMidi3.01.sf2) |
| `CrisisGeneralMidi3.01.sfpack` | 640,783,613 (611.1 MB) | `8cde7b87652d55da002c1c8798cb4b27f28b93121a2ccc3e03673231444183a9` | [link](https://archive.org/download/crisis-general-midi-3.01/CrisisGeneralMidi3.01.sfpack) |
| `CrisisGeneralMidi3.01/` (directory, 1 files) | 1,689,192,084 (1.6 GB) | - | extracted from `CrisisGeneralMidi3.01.7z` |
| `CrisisGeneralMidi3.01/CrisisGeneralMidi3.01.sf2` | 1,689,192,084 (1.6 GB) | `a382036696e065fafe37797b804ca8e2b56155682de4f54b084d6636807a82f5` | extracted from `CrisisGeneralMidi3.01.7z` |

### Airfont (Milton Paredes)

Folder: `airfont/` | Version: 320 neo (2018), 340 (2004, SF2 and DLS), 380 final (2007)

License (as stated by the author): airfont-340 item marked CC0, DLS item marked CC BY-NC-ND 4.0; INFO chunks: MPJ Factory Studios. **Local test asset only** (no redistribution license).

Note: GM; three generations of the bank, plus `Airfont_340.dls` (DLS build, for DLS loader tests) and the `.afpk` preset packs.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Airfont_340.dls` | 81,362,584 (77.6 MB) | `beb3e39e3c9fc51ef4dff36fdd8db0361471a91d244c3ee78af90f6d3c783b04` | [link](https://archive.org/download/airfont-340_dls_version/Airfont_340.dls) |
| `airfont_320_neo.sf2` | 19,760,870 (18.8 MB) | `7adb61bc8a5e736e16dafee0edc37957682b0088fcffe65729ce15a3769a9dd5` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/airfont_320_neo.sf2) |
| `airfont_340.afpk` | 1,384 (1.4 KB) | `cd109c47961bcbee3f2bf3e03aaffb8c8b45e3f5faefb7c466d92cd1d7589b0e` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/airfont_340.afpk) |
| `airfont_340.sf2` | 80,500,780 (76.8 MB) | `26b19d395d2df88af0e8a09f815ae661524e6019d50f862cbd628c11057c9bb1` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/airfont_340.sf2) |
| `airfont_380_final.afpk` | 1,208 (1.2 KB) | `c22f834a3ba0593beb2513161751360a11cc063a7554783d426b80fd51767157` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/airfont_380_final.afpk) |
| `airfont_380_final.sf2` | 275,887,780 (263.1 MB) | `9dd093c0f4829be8715ed407deba1d15158af28901a133349a196dd750d1a6f5` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/airfont_380_final.sf2) |

### Eawpats (Gravis UltraSound / TiMidity patch set)

Folder: `gus-eawpats/` | Version: 12 (Eric A. Welsh, 2005 archive)

License (as stated by the author): Mixed: collected from GUS patch sets of various origin, distributed freely for TiMidity. **Local test asset only** (no redistribution license).

Note: GM + GS; GUS `.pat` patch set with `timidity.cfg`; the de-facto TiMidity standard set.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `eawpats.zip` | 32,032,699 (30.5 MB) | `19087fa4a40e25ec39a09cffcc9f775fc22d88bc971a7a9831e075cdae2ee1e3` | [link](https://archive.org/download/eawpats_gus/eawpats.zip) |
| `eawpats/` (directory, 372 files) | 38,169,205 (36.4 MB) | - | extracted from `eawpats.zip` |

### FreePats (original GUS patch set)

Folder: `gus-freepats-2006/` | Version: 20060219 (Debian upstream tarball)

License (as stated by the author): GPL-2 with exception (Debian `copyright` in the debian tarball).

Note: GM (partial); the original FreePats `.pat` set used by TiMidity on Linux.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `freepats_20060219-4.debian.tar.xz` | 3,588 (3.5 KB) | `6bb5ab41a041fd6145bd4f13365e6282d7f38e520d197abfbe8ce773ca104819` | [link](http://deb.debian.org/debian/pool/main/f/freepats/freepats_20060219-4.debian.tar.xz) |
| `freepats_20060219.orig.tar.gz` | 25,789,552 (24.6 MB) | `70bf8ca084df3903d6c9de43fe20539fc0a553d95cfba4d525da3fe66fda5f10` | [link](http://deb.debian.org/debian/pool/main/f/freepats/freepats_20060219.orig.tar.gz) |
| `freepats_20060219-4.debian/` (directory, 13 files) | 7,866 (7.7 KB) | - | extracted from `freepats_20060219-4.debian.tar.xz` |
| `freepats_20060219.orig/` (directory, 1 files) | 25,791,733 (24.6 MB) | - | extracted from `freepats_20060219.orig.tar.gz` |

### FreePats General MIDI (SF2)

Folder: `freepats/` | Version: 20221026 (GM set) and 20200822 (GM percussion set)

License (as stated by the author): Mixed CC0 / GPL-3 with exception (`cc0.txt`, `gpl.txt`, `readme.txt`).

Note: GM; the modern FreePats project as SF2 (`FreePatsGM-20221026.sf2`, 230+ MB) and its percussion set.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FreePatsGM-Percussion-SF2-20200822.tar.xz` | 15,610,628 (14.9 MB) | `a342ade875b88885d6da7d1b60e79a4385abc9357b7469b205272e734571140e` | [link](https://freepats.zenvoid.org/SoundSets/GM-PercussionSet/FreePatsGM-Percussion-SF2-20200822.tar.xz) |
| `FreePatsGM-SF2-20221026.7z` | 237,775,919 (226.8 MB) | `c2f5c777851e3ad86cba119bf91aa6b915022512e81c37ea010ee6a3f8b5415d` | [link](https://freepats.zenvoid.org/SoundSets/FreePats-GeneralMIDI/FreePatsGM-SF2-20221026.7z) |
| `FreePatsGM-Percussion-SF2-20200822/` (directory, 4 files) | 28,927,540 (27.6 MB) | - | extracted from `FreePatsGM-Percussion-SF2-20200822.tar.xz` |
| `FreePatsGM-Percussion-SF2-20200822/FreePatsGM-Percussion-SF2-20200822/FreePatsGM-Percussion-20200822.sf2` | 28,871,548 (27.5 MB) | `c39363ca6d9c386bbc6b0f5d70b2cbf3cb1a471f6ddbaa9e44574905bd7f4de9` | extracted from `FreePatsGM-Percussion-SF2-20200822.tar.xz` |
| `FreePatsGM-SF2-20221026/` (directory, 4 files) | 322,293,130 (307.4 MB) | - | extracted from `FreePatsGM-SF2-20221026.7z` |
| `FreePatsGM-SF2-20221026/FreePatsGM-SF2-20221026/FreePatsGM-20221026.sf2` | 322,219,516 (307.3 MB) | `f7bf84f92ae8eb5f165097e69aff7e4b9da3906ba0ecde11aa495391052cd5e2` | extracted from `FreePatsGM-SF2-20221026.7z` |

### TiMidity patch collection and Pro Patches Lite

Folder: `gus-timidity-patches/` | Version: as archived 2024 (`timidity_patches.tar.gz`), Pro Patches Lite 1.61 (`PPL161.zip`)

License (as stated by the author): Gravis/Voyetra patches: Advanced Gravis copyright; collection: no license. **Local test asset only** (no redistribution license).

Note: GM; GUS `.pat` sets: the classic TiMidity collection and Gravis "Pro Patches Lite" (the GUS PnP ROM-replacement set).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `PPL161.zip` | 6,903,066 (6.6 MB) | `2f7025752191c365a853c2296b84e233f97409a1ab59ae3a18ee496573b131e8` | [link](https://archive.org/download/timidity_patches.tar/PPL161.zip) |
| `timidity_patches.tar.gz` | 14,905,458 (14.2 MB) | `42643134fcb9e01e87212503efd0e31b965250b33436705bb3d53384c00be62d` | [link](https://archive.org/download/timidity_patches.tar/timidity_patches.tar.gz) |
| `PPL161/` (directory, 277 files) | 6,898,691 (6.6 MB) | - | extracted from `PPL161.zip` |
| `timidity_patches/` (directory, 195 files) | 17,732,668 (16.9 MB) | - | extracted from `timidity_patches.tar.gz` |

### Gravis UltraSound GF1 patch files (batch 1)

Folder: `gus-gf1-patches/` | Version: as archived 2026

License (as stated by the author): Advanced Gravis / Forte Technologies patches (from the GUS driver disks). **Local test asset only** (no redistribution license).

Note: GM; the original GF1 `.pat` instrument set of the Gravis UltraSound (192 files).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Batch1.zip` | 5,170,627 (4.9 MB) | `546e47b289198389375531f8fb3da27dbb1f00e17bb351828e47ad3fc5bb9ba7` | [link](https://archive.org/download/batch-1_202609/Gravis%20UltraSound%20GF1%20Patch%20files%20%5BPAT%5D/Batch1.zip) |
| `Batch1/` (directory, 191 files) | 5,463,637 (5.2 MB) | - | extracted from `Batch1.zip` |

### TiMidity++ patch set for Opentouch

Folder: `gus-timidity-opentouch/` | Version: as archived 2024

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; a TiMidity++ `.pat` set packaged for the Alcatel Opentouch phone platform (373 files).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `snd_timidity.zip` | 31,694,829 (30.2 MB) | `f18a56a04c6d7a1f6f7bd02ae7d72b35d1f3cfa405a4d5c02caca53c1c11f17b` | [link](https://archive.org/download/snd_timidity/snd_timidity.zip) |
| `snd_timidity/` (directory, 372 files) | 38,169,213 (36.4 MB) | - | extracted from `snd_timidity.zip` |

### Gravis UltraSound sets converted to SF2/SBK

Folder: `gus-sf2-conversion/` | Version: GUS.SF2/SBK (1996), Gus1live v1.2 (UltraSound.ini banks), GUIT_GM

License (as stated by the author): INFO chunks: "Copyright 1992,1993 EYE&I Productions and Advanced Gravis". **Local test asset only** (no redistribution license).

Note: GM; the GUS patch set as SoundFont 1 (`.SBK`) and 2 (`.SF2`), plus a guitar-only GM bank in both formats.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GUIT_GM.SBK` | 2,138,387 (2.0 MB) | `2d4d49f7be1a84955fa3aa37097624dead592af75b54b7759211631b8c7ac250` | [link](https://archive.org/download/soundfonts_201910/GUIT_GM.SBK) |
| `GUIT_GM.SF2` | 2,148,371 (2.0 MB) | `911bd62adf19a00ff3eae260c9d54d2383fac39604a06c368050dd31424f7aa5` | [link](https://archive.org/download/soundfonts_201910/GUIT_GM.SF2) |
| `GUS.SBK` | 5,780,220 (5.5 MB) | `42586058d03f1a75b5f4ad26a2dab4b2853d30add7ab62b9e985d87e261883f4` | [link](https://archive.org/download/soundfonts_201910/GUS.SBK) |
| `GUS.SF2` | 5,794,570 (5.5 MB) | `536eb81ad02fbd51071bcf93b6bd6bdfc7d9b452b5d7307ed5966f31cbcb31ee` | [link](https://archive.org/download/soundfonts_201910/GUS.SF2) |
| `Gus1live_v12.sf2` | 16,568,468 (15.8 MB) | `53a81e9301f883b1b7bb822ba5e0f2ab66bc7d3a9f18558a0e7d04970bd8bec1` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Gus1live_v12.sf2) |

### Fluid R3 variants

Folder: `fluidr3-variants/` | Version: FluidR3 GM (1999 SF2 build), FluidR3_GM2-2 (2013, fixed violin), FluidR3 Mobile GM (2008)

License (as stated by the author): Fluid R3: MIT (as stated by the Debian package of the same bank); the 1999 INFO chunk says "DO NOT REDISTRIBUTE ANY OF THESE SAMPLES". **Local test asset only** (no redistribution license).

Note: GM; other builds of the Fluid Release 3 bank next to `fluidr3/` (Debian 3.1).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FluidR3 GM.SF2` | 148,358,590 (141.5 MB) | `a2acd5c90c16262419a151ad1fbfd4c69a09048727afa462a8bac931b91ac3d3` | [link](https://archive.org/download/soundfonts_201910/FluidR3%20GM.SF2) |
| `FluidR3_GM2-2.SF2` | 148,345,256 (141.5 MB) | `2ae766ab5c5deb6f7fffacd6316ec9f3699998cce821df3163e7b10a78a64066` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/FluidR3_GM2-2.SF2) |
| `FluidR3_Mobile_GM.sf2` | 148,358,602 (141.5 MB) | `e76d350f01e678bbf1e7d8d7b18adce8e7eb47def8d83918cc0288350e1ba31f` | [link](https://archive.org/download/fluid-r-3-mobile-gm/FluidR3_Mobile_GM.sf2) |

### Live HQ Natural SoundFont GM

Folder: `live-hq-natural/` | Version: final (2020-2021)

License (as stated by the author): archive.org item marked CC BY-ND 4.0; INFO chunk: "(C) 2020 - 2021 UnderxPipe1985". **Local test asset only** (no redistribution license).

Note: GM; 836 MB realistic bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Live HQ Natural SoundFont GM.sf2` | 836,038,682 (797.3 MB) | `5ed1b6a205686e43ead7386560c6610406b3cf4a0dfda230b89b8403dcf5efb7` | [link](https://archive.org/download/live-hq-natural-sound-font-gm_202607/Live%20HQ%20Natural%20SoundFont%20GM.sf2) |

### SgtPepperArc360 XG

Folder: `sgtpepperarc360-xg/` | Version: 2022 and V2.0 (2024)

License (as stated by the author): archive.org item marked CC BY-ND 4.0; INFO chunk: David "SgtPepperArc360" Egan. **Local test asset only** (no redistribution license).

Note: XG / GM; 290 MB and 318 MB XG-mapped banks.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SgtPepperArc360 XG Soundfont V2.0.sf2` | 318,150,806 (303.4 MB) | `a4f525c5c328fe0f6a7ee8656b5ca76bfa21c309c6f717ede285ae95814513eb` | [link](https://archive.org/download/sgtpepperarc360-xg-soundfont-v2.0/SgtPepperArc360%20XG%20Soundfont%20V2.0.sf2) |
| `SgtPepperArc360.sf2` | 290,749,776 (277.3 MB) | `1ca701f52e56b3a39d6530aa4b82ab7c631fab391ab1655ea754266c32fd0eeb` | [link](https://archive.org/download/SGA360XG/SgtPepperArc360.sf2) |

### Chaos Bank (Seong-jin Yun)

Folder: `chaos-bank/` | Version: 4M v1.5, 8M v1.72 (1997), 12M v1.9 (1999), V20

License (as stated by the author): Freeware (readme texts); V20 INFO chunk: "Public Domain". **Local test asset only** (no redistribution license).

Note: GM; classic Korean AWE-era banks in 4/8/12 MB size classes.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `CHAOS4M.SF2` | 4,428,204 (4.2 MB) | `f6eb52020b01d4911d0f3c3e501ed6bfcb852587baac927e7a2b8cf37b754e6c` | [link](https://archive.org/download/soundfonts_201910/CHAOS4M.SF2) |
| `CHAOS4M_BLUE.MID` | 72,718 (71.0 KB) | `108afd74cc05967999e6b28d909badbd7cc1b782b1901d1a06dc0c5fc7e5e647` | [link](https://archive.org/download/soundfonts_201910/CHAOS4M_BLUE.MID) |
| `CHAOS4M_EN.TXT` | 2,059 (2.0 KB) | `826adb54f6173c1a000d76e7a8c967a09cfc56035ccea19b9f898e28f87e4f1b` | [link](https://archive.org/download/soundfonts_201910/CHAOS4M_EN.TXT) |
| `CHAOS4M_KR.TXT` | 4,065 (4.0 KB) | `c03671545ff9e0064015a36ef4c64ba895383f34db84e6e6185d472249ee7dd7` | [link](https://archive.org/download/soundfonts_201910/CHAOS4M_KR.TXT) |
| `CHAOS8M.SF2` | 8,624,378 (8.2 MB) | `13b828dfe2813042e078cfea969163bc5f2e80db5ae459f3d6a790dce187cabb` | [link](https://archive.org/download/soundfonts_201910/CHAOS8M.SF2) |
| `CHAOS_EN.TXT` | 2,262 (2.2 KB) | `8119ee972c2202afcc738f2c803644958575bf7960fcd630a85128d9495d840a` | [link](https://archive.org/download/soundfonts_201910/CHAOS_EN.TXT) |
| `CHAOS_KR.TXT` | 5,772 (5.6 KB) | `722c6f199ccff5535e68c9c3986b985d92e7e8eb896c1373f2c94cfcc11c722f` | [link](https://archive.org/download/soundfonts_201910/CHAOS_KR.TXT) |
| `Chaos Bank V1.9 (12Mb).sf2` | 12,038,662 (11.5 MB) | `0a107e182fee704ad9b91cbbad2febf97f55cf778ff656ed49415c6a5addd01e` | [link](https://archive.org/download/soundfonts_201910/Chaos%20Bank%20V1.9%20%2812Mb%29.sf2) |
| `Chaos_12M_Readme_English.txt` | 2,017 (2.0 KB) | `e6b0297b4bccd67460eb9f39b437014dee525b01a9a129cd6c35a7b2591e75b8` | [link](https://archive.org/download/soundfonts_201910/Chaos_12M_Readme_English.txt) |
| `Chaos_12M_Readme_Korean.txt` | 10,137 (9.9 KB) | `82b6d1082eb63310c9a0cd571e7982b3d7a658701ab2ed268445b813e9789744` | [link](https://archive.org/download/soundfonts_201910/Chaos_12M_Readme_Korean.txt) |
| `Chaos_V20.sf2` | 12,189,088 (11.6 MB) | `2dc2691394525f9eeb9a138a55bc1ff904bac6620d5775ec3f40a8a83855c364` | [link](https://archive.org/download/soundfonts_201910/Chaos_V20.sf2) |

### FantaGM 32

Folder: `fantagm/` | Version: 1.5 (1997)

License (as stated by the author): INFO chunk: free, no commercial use (Jang SeungGaul). **Local test asset only** (no redistribution license).

Note: GM; 33 MB AWE-era bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FantaGM32v15.sf2` | 32,947,392 (31.4 MB) | `8b6ddbcab9e5a1fd7f5e387f6f7b5423d8cb0a2529e7232eab96412870f951dc` | [link](https://archive.org/download/soundfonts_201910/FantaGM32v15.sf2) |

### UGM

Folder: `ugm/` | Version: v1 beta (2008)

License (as stated by the author): INFO chunk: "Nikolaich (c) 2008". **Local test asset only** (no redistribution license).

Note: GM; 206 MB bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `UGM v_1 beta.sf2` | 205,816,532 (196.3 MB) | `55e88732802b24d19a34119c34744344880a680e06d71c69c58429213fb6d194` | [link](https://archive.org/download/soundfonts_201910/UGM%20v_1%20beta.sf2) |

### Unison (Peter Jevnisek)

Folder: `unison/` | Version: 1.00 (1999)

License (as stated by the author): Freeware (`Unison_README.txt`). **Local test asset only** (no redistribution license).

Note: GM + GS (30 GS variations, 10 kits); 28 MB AWE-era bank; `Unison.zip` holds the original sfArk. The sfArk inside the zip is an sfArk V1 file, which sfarkxtc cannot read; `Unison.SF2` from the same archive.org item is the unpacked bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Unison.SF2` | 29,258,148 (27.9 MB) | `a9af8184b7afd36dc8fde39992ff67542d01eb486295c353fc54d3f3b693d51c` | [link](https://archive.org/download/soundfonts_201910/Unison.SF2) |
| `Unison.zip` | 18,744,517 (17.9 MB) | `ce71bac28d6f73526fd3f417b9e4a8cde026d8be3333892ddb45264ba8dedb4d` | [link](https://archive.org/download/soundfonts_201910/Unison.zip) |
| `Unison_README.txt` | 3,266 (3.2 KB) | `c24d6cec3deab9abf6748843a057fe8ba5c1ec99f1b217d967700f1c0af99f21` | [link](https://archive.org/download/soundfonts_201910/Unison_README.txt) |
| `Unison/` (directory, 30 files) | 19,638,562 (18.7 MB) | - | extracted from `Unison.zip` |
| `Unison/Unison.sfArk` | 17,895,141 (17.1 MB) | `6c520f948564e5816db8f59fa186266eb7442583d2b5f816c51eb9e1fa8be87b` | extracted from `Unison.zip` |

### 8Rock

Folder: `rock-8mb/` | Version: 11e (1997)

License (as stated by the author): INFO chunk: "Distribute freely. Don't disassemble." (George).

Note: GM; 8 MB rock-oriented AWE bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `8Rock11e.sf2` | 8,479,586 (8.1 MB) | `fc238264cdc3b6bf5bdafdce5aff8235dc900c21dc4303cb0643b03656b7c95e` | [link](https://archive.org/download/soundfonts_201910/8Rock11e.sf2) |

### Tyroland (Roland JV-1010 GM)

Folder: `tyroland/` | Version: v1.0 (2020) and the TyrolandGS sfArk

License (as stated by the author): INFO chunk: "(C) 1999 ROLAND CORPORATION", sampled by Thomas K. **Local test asset only** (no redistribution license).

Note: GM + GS; 845 MB bank sampled from a Roland JV-1010 module.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Tyroland.sf2` | 844,811,916 (805.7 MB) | `e64c9579348cb51c284474f02da85dcaa626243f2672f64bb1a3f9ccf6f4f069` | [link](https://archive.org/download/soundfont-collection_202601/Tyroland.sf2) |
| `TyrolandGS.sf2` | 893,219,322 (851.8 MB) | `1e738ffd0888d151761c3372120d03d871608849a567fc2e8dd1fc26285e17c6` | unpacked from `TyrolandGS.sfArk` with sfarkxtc |
| `TyrolandGS.sfArk` | 336,625,459 (321.0 MB) | `49e2a7cc571a3e0ba4b292805692a89ced52b1bc048c1ecb556e348bde7807a1` | [link](https://archive.org/download/GMSoundfonts/TyrolandGS.sfArk) |

### OmegaGMGS2

Folder: `omega-gmgs2/` | Version: 4.1/4.3 (2016)

License (as stated by the author): INFO chunk: "Rick Simon, (c) 2016 all rights reserved". **Local test asset only** (no redistribution license).

Note: GM + GM2 + GS + XG; 278 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `OmegaGMGS2.sf2` | 278,600,888 (265.7 MB) | `7f8b25dd6fd03f90d7e903b7b07b07d9a758c41cefb7e656a258eec3d4836c78` | [link](https://archive.org/download/soundfont-collection_202601/OmegaGMGS2.sf2) |

### Reality GM/GS

Folder: `reality-gmgs/` | Version: original and falco mod 1.2

License (as stated by the author): INFO chunks: "Public Domain" / "Art Gallery" (Takeshi Tan). **Local test asset only** (no redistribution license).

Note: GM + GS; 33 MB and 36 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Reality_GMGS_falcomod.sf2` | 35,921,264 (34.3 MB) | `fec5318b9753c74c31aa8a78f66a5567bdc72e0cdf3ec8e2a4e3c50853758554` | [link](https://archive.org/download/soundfont-collection_202601/Reality_GMGS_falcomod.sf2) |
| `Reality_gm_gs.SF2` | 32,756,862 (31.2 MB) | `4f91ba4a79c2198fbf6e7b457c1aa43524764d4d88a12b438897188ef19bddc4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Reality_gm_gs.SF2) |

### Compifont

Folder: `compifont/` | Version: 13082016

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 1 GB compilation bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Compifont_13082016.sf2` | 1,021,568,246 (974.2 MB) | `5ebe313a7abd300fe788fa42777da164b6b78166c9e10c8bef1edd9cf184dded` | [link](https://archive.org/download/soundfont-collection_202601/Compifont_13082016.sf2) |

### JNSGM (Jordi Navarro Subirana)

Folder: `jnsgm/` | Version: 2.0 (1999), two uploads

License (as stated by the author): INFO chunk: "(c) Jordi Navarro Subirana". **Local test asset only** (no redistribution license).

Note: GM; 32 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Jnsgm.sf2` | 33,187,490 (31.7 MB) | `dc48cb5c322cab23fce1b18442066be30ccc49a184603c7a3bf7615003ee137d` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Jnsgm.sf2) |
| `Jnsgm2.sf2` | 33,187,490 (31.7 MB) | `dc48cb5c322cab23fce1b18442066be30ccc49a184603c7a3bf7615003ee137d` | [link](https://archive.org/download/soundfont-collection_202601/Jnsgm2.sf2) |

### WinGroove (SF2 conversion)

Folder: `wingroove/` | Version: unversioned

License (as stated by the author): INFO chunk: original WinGroove softsynth by Hiroki Nakayama, conversion by Zorilla. **Local test asset only** (no redistribution license).

Note: GM; 3.5 MB conversion of the 1990s Japanese WinGroove software synthesizer set (ROM-class size).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `WinGroove.sf2` | 3,507,136 (3.3 MB) | `12b43d428448f90540d3e3beead1f6b5964e9199b0335e34d0b827f002506a05` | [link](https://archive.org/download/soundfont-collection_202601/WinGroove.sf2) |

### Casio CTK-230 SoundFont

Folder: `casio-ctk230/` | Version: 2018

License (as stated by the author): INFO chunk: "(C) Casio Computer Co., Ltd." (Dekyo Ongen). **Local test asset only** (no redistribution license).

Note: GM; 3.2 MB sampled from a Casio CTK-230 keyboard (ROM-class).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `CTK-230_SoundFont.sf2` | 3,222,086 (3.1 MB) | `bf50f600620fa735adbb2e5f52c17f08b842b5f01beb82f2248dd9866368c781` | [link](https://archive.org/download/soundfont-collection_202601/CTK-230_SoundFont.sf2) |

### Merlin banks (Lavio Pareschi)

Folder: `merlin/` | Version: GMpro 3.15, audigy 1.14, creative 4.15, gmv22, gmv32 3.2, gold 4.10, grand 5.37, orchestra 1.40, silver 4.10, special 2.05, symphony 1.21, vienna 3.20

License (as stated by the author): INFO chunks: "(c) Pixel Arts". **Local test asset only** (no redistribution license).

Note: GM; family of 12 to 171 MB banks.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `merlin_GMpro(v3.15).sf2` | 48,976,174 (46.7 MB) | `d2f05a292c1a8e6ca31a01e52890db0b377811f57832471fba4156afc86fb6f4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_GMpro%28v3.15%29.sf2) |
| `merlin_audigy(v1.14).sf2` | 38,799,118 (37.0 MB) | `2593dd56017181a01b0960be5e422d6186624c891bf0a12fe4978f2b8cfb6c68` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_audigy%28v1.14%29.sf2) |
| `merlin_creative(v4.15).sf2` | 23,831,954 (22.7 MB) | `9b710b84da95dd26e8d5b60ea766c5a97fb442cd9c241f763fd37355eaf08e26` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_creative%28v4.15%29.sf2) |
| `merlin_gmv22.sf2` | 29,562,696 (28.2 MB) | `abc2d4c73357678ea1216685d69e7b704e7bb3937f8b2b3f04ba49ff5913a585` | [link](https://archive.org/download/soundfont-collection_202601/orquestal/Colecciones/Collections%20part%203/merlin_gmv22.sf2) |
| `merlin_gmv32(v3.2).sf2` | 35,423,702 (33.8 MB) | `fcab5ae468e439e0e5d1dd38ff0bd9ffaa5fbc7350c60988d7ddab7eec9fd786` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_gmv32%28v3.2%29.sf2) |
| `merlin_gold(v4.10).sf2` | 38,767,452 (37.0 MB) | `805e48627fdb86df2201e63d355d67f0fdc6f77e0d13e41b0da3e84fe0d00766` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_gold%28v4.10%29.sf2) |
| `merlin_grand(v5.37).sf2` | 61,409,722 (58.6 MB) | `2d959eeddda4186abdb48604b91d9864158be8e545f42a5c96a4fcee9925e9b2` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_grand%28v5.37%29.sf2) |
| `merlin_orchestra(v1.40).sf2` | 70,795,732 (67.5 MB) | `b7775e1b01dd80efe00b06cda407864bdecf2ae2d67e88138322693bd4e1fa76` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_orchestra%28v1.40%29.sf2) |
| `merlin_silver(v4.10).sf2` | 11,567,012 (11.0 MB) | `2bd24e579d8e2692e27e7d05fa026e812a5ade1c587414bc90b1167e406af707` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_silver%28v4.10%29.sf2) |
| `merlin_special(v2.05).sf2` | 57,463,802 (54.8 MB) | `5a6aafca53ceaabf5c0bc8f20a8b2ec067cef2d173c803ba4e77e2435dc0caba` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_special%28v2.05%29.sf2) |
| `merlin_symphony(v1.21).sf2` | 171,305,360 (163.4 MB) | `521a953c5983de7bd2997b08ca5fe69afcf010d9358b5675f8fc8932b897f95e` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_symphony%28v1.21%29.sf2) |
| `merlin_vienna(v3.20).sf2` | 90,208,960 (86.0 MB) | `979aacefea20bdce0bd0b0bf37b224a616c981eeae6d0fdb3d17acd61580e3ef` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/merlin_vienna%28v3.20%29.sf2) |

### S_J Orchestral GM

Folder: `sj-orchestral-gm/` | Version: 2002 (fixed)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 74 MB orchestral-leaning GM bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `S_J_Orchestral_GM fixed.sf2` | 74,449,030 (71.0 MB) | `8a28febcc8a45cb36908dd8b182bb4cdf0b5d94f7484dfbf2043731731ab8b96` | [link](https://archive.org/download/soundfont-collection_202601/orquestal/Colecciones/Collections%20part%203/S_J_Orchestral_GM%20fixed.sf2) |

### 16.5 MB GM/GS/MT-32 Bank (LuckyMax)

Folder: `luckymax-gm-gs-mt32/` | Version: 2.51

License (as stated by the author): INFO chunk: "Copyright (c) LuckyMax". **Local test asset only** (no redistribution license).

Note: GM + GS + MT-32 map; 16.9 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `16.5mg_gm_gs_mt32_v2.51_bank.sf2` | 16,944,892 (16.2 MB) | `c5971c61f49f749837b0bcf7e2b5d17d5bd6d2b60df19f98da320315a029813f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/16.5mg_gm_gs_mt32_v2.51_bank.sf2) |

### 24.1 MB Mega Sound Bank

Folder: `mega-sound/` | Version: 2.0 (1997)

License (as stated by the author): INFO chunk: no commercial use without permission (afterDAN & RiceBug). **Local test asset only** (no redistribution license).

Note: GM; 24.7 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `24.1mg_mega_sound_v2.0_bank.SF2` | 24,735,272 (23.6 MB) | `6eedee40eb3fcb9d49f858729532a314dac1336d00746c2f90c5a158f2d5de48` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/24.1mg_mega_sound_v2.0_bank.SF2) |

### 27.3 MB Symphony Hall Bank

Folder: `symphony-hall-bank/` | Version: unversioned

License (as stated by the author): INFO chunk: "Public Domain".

Note: GM; 28 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `27.3mg_symphony_hall_bank.SF2` | 28,013,272 (26.7 MB) | `8d546a69f862f53b67bec3aa650cb87cd784f3a5dfe173f0158cf5683fe90357` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/27.3mg_symphony_hall_bank.SF2) |

### 32 MB GM Stereo (NTONYX)

Folder: `ntonyx-32mb-gm-stereo/` | Version: 2000/2001

License (as stated by the author): INFO chunk: "Copyright 2001 by NTONYX" (Yuri Smolyakov). **Local test asset only** (no redistribution license).

Note: GM; 32.5 MB stereo bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `32MbGMStereo[ntonyx.com].sf2` | 32,461,770 (31.0 MB) | `488489bac8eba61dcb9a6dc188c8aa5a41b8bd2e294163408ae9a4c530aca578` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/32MbGMStereo%5Bntonyx.com%5D.sf2) |

### Saphyr Two Thousand GM/GS Bank

Folder: `saphyr-2000-gm-gs/` | Version: 41.8 MB

License (as stated by the author): No license text (Saphyr 2000 enr.). **Local test asset only** (no redistribution license).

Note: GM + GS; 42.9 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `41.8mg_saphyr_two_thousand_gm_gs_bank.sf2` | 42,879,226 (40.9 MB) | `1ad4293bb0648c54c737acbd95ecb6b7b9a9a9e7acfb360a5bd9425a2153bd84` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/41.8mg_saphyr_two_thousand_gm_gs_bank.sf2) |

### The NES SoundFont (8bitsf)

Folder: `8bitsf/` | Version: 2009

License (as stated by the author): INFO chunk: "(C)2009 The Eighth Bit". **Local test asset only** (no redistribution license).

Note: GM-mapped NES (2A03) style chiptune bank, 6.7 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `8bitsf.SF2` | 6,744,228 (6.4 MB) | `3394f3436ffcec4ab094949942dc601d6f20ef4595af3291230b7261ff89bb2c` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/8bitsf.SF2) |

### Acapella GM

Folder: `acapella-gm/` | Version: 2019

License (as stated by the author): INFO chunk: Stain, 2019. **Local test asset only** (no redistribution license).

Note: GM where every program is a voice sample; 20.6 MB oddity.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Acapella GM.sf2` | 20,601,588 (19.6 MB) | `9768af78002aea53eb458cf6215de7b64637d3b357cd0e782e37e48ec43e734b` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Acapella%20GM.sf2) |

### Aspirin 160 GM/GS

Folder: `aspirin-gmgs/` | Version: 2015 update

License (as stated by the author): INFO chunk: "Public Domain".

Note: GM + GS; 16.6 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Aspirin_160_GMGS_2015.sf2` | 16,624,854 (15.9 MB) | `3637780957f63cebf4c69ee48c4b76dcafba7bc162ebf1a3d1ec8fe43e14f4da` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Aspirin_160_GMGS_2015.sf2) |

### DSoundFont (Ultimate / Plus / V4)

Folder: `dsoundfont/` | Version: V4, Plus V4, Ultimate, DSFU4 parts 2-3, T8-T9 patch

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; very large bank family (V4 580 MB SF2; Ultimate 2.65 GB sfArk). sfArk files unpacked with sfarkxtc, except `DSoundfont Ultimate.sfArk`: sfarkxtc aborts it reproducibly at 1,949,040,640 bytes (header announces 4,174,257,468), so only the `.sfArk` is kept.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `DSFU4 Part 2.sf2` | 2,532,435,208 (2.4 GB) | `ccebb00ed936f84ffa0449547ffa8d9346fbd6b5dfbb7bce6a069eaa7378e4aa` | unpacked from `DSFU4 Part 2.sfArk` with sfarkxtc |
| `DSFU4 Part 2.sfArk` | 1,163,449,013 (1.1 GB) | `a726d4e9d30f2d213c6f9ec8d980ed7d5c3342842b727966913322de0074a1a1` | [link](https://archive.org/download/GMSoundfonts/DSFU4%20Part%202.sfArk) |
| `DSFU4 Part 3.sf2` | 1,349,710,342 (1.3 GB) | `4dd249c76e88b4accc091f3f57f4fc539aac265d206308414eaf0a23a277816d` | unpacked from `DSFU4 Part 3.sfArk` with sfarkxtc |
| `DSFU4 Part 3.sfArk` | 482,781,517 (460.4 MB) | `411fe41ea4dd0a36138bfdcaaf8de9c6982a699e7d5e8126e9f975bc7ea0806d` | [link](https://archive.org/download/GMSoundfonts/DSFU4%20Part%203.sfArk) |
| `DSoundFontV4.sf2` | 580,195,370 (553.3 MB) | `01815e87817138cc30e798152d3cc66991f853305eac34660bb588437c09fee4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/DSoundFontV4.sf2) |
| `DSoundFont_Plus_V4.sf2` | 1,470,027,858 (1.4 GB) | `e21bf70196d1ca766f1e89c83a2b022a1a062980772f57e23f598ccbdfdb8b61` | unpacked from `DSoundFont_Plus_V4.sfArk` with sfarkxtc |
| `DSoundFont_Plus_V4.sfArk` | 640,571,319 (610.9 MB) | `b2a91750b79a957e7cccad91ee8148fceefbaa083aabb5bf8e391c60a7a01a1a` | [link](https://archive.org/download/GMSoundfonts/DSoundFont_Plus_V4.sfArk) |
| `DSoundfont Ultimate.sfArk` | 2,650,819,394 (2.5 GB) | `88aa8ba19e620463cea1b6270b9d5c12ae9ecb224d671555b9e7e3dc5053186b` | [link](https://archive.org/download/GMSoundfonts/DSoundfont%20Ultimate.sfArk) |
| `T8-T9 Patch for DSoundfont Ultimate or 5.0.sf2` | 9,967,590 (9.5 MB) | `19af15ebe46f94c5554c385bb3dfa62369194887dfb377a21d81c6169f6fa5e4` | unpacked from `T8-T9 Patch for DSoundfont Ultimate or 5.0.sfArk` with sfarkxtc |
| `T8-T9 Patch for DSoundfont Ultimate or 5.0.sfArk` | 4,555,939 (4.3 MB) | `4e43d26e39862f80bf2551ca3946a95adb9aeba3454df590a083811e172bce46` | [link](https://archive.org/download/GMSoundfonts/T8-T9%20Patch%20for%20DSoundfont%20Ultimate%20or%205.0.sfArk) |

### FMSynthesis

Folder: `fm-synthesis/` | Version: 1.40 (2018)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 130 MB sampled FM-synthesis bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FMSynthesis1.40.sf2` | 129,661,102 (123.7 MB) | `26c4d802da653782c62dd398c5c4f57aa24f75233f93e6a7a828583fa84f939b` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/FMSynthesis1.40.sf2) |

### FatBoy

Folder: `fatboy/` | Version: 0.786 (SF2) and 0.790 (sfArk)

License (as stated by the author): Free (author's site); no license text in the file. **Local test asset only** (no redistribution license).

Note: GM + GS; 330 MB bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `FatBoy-v0.786.sf2` | 330,513,518 (315.2 MB) | `8aac3471ea8873b6526325918fff5c83008dccecdc89ecc88d32a7205289307e` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/FatBoy-v0.786.sf2) |
| `FatBoy-v0.790.sf2` | 320,147,782 (305.3 MB) | `b0b40d57aa4c413c74996c8d32a9b17f5dd97a07577eabacbfce2d3d439e2690` | unpacked from `FatBoy-v0.790.sfArk` with sfarkxtc |
| `FatBoy-v0.790.sfArk` | 142,347,026 (135.8 MB) | `200dab3b1674777bacc2bebdf55c7066edb9e6dacb9b54d4926ebbd927ce16c1` | [link](https://archive.org/download/GMSoundfonts/FatBoy-v0.790.sfArk) |

### GMExtBank

Folder: `gm-ext-bank/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 133 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GMExtBank.SF2` | 133,326,806 (127.2 MB) | `72da8e89286a63b1a02cbb972c1c0752b6b235325064b2e9a531a84a546f27f3` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/GMExtBank.SF2) |

### JCLive

Folder: `jclive/` | Version: 2.1

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 52 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `JClive21.sf2` | 52,489,526 (50.1 MB) | `061496ea337e774386dec86f8e66dafa5c36f81b7650ea48baaceac68480fc6f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/JClive21.sf2) |

### Jurgen GM/GS Bank

Folder: `jurgen-gm-gs/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 63 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Jurgen_GM_GS_Bank.sf2` | 63,450,970 (60.5 MB) | `59b6ac2437fd769b2fd77948f4e07c3f2def4c74e9534ddf2a5a8baabc80d9a9` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Jurgen_GM_GS_Bank.sf2) |

### MagicSF

Folder: `magic-sf/` | Version: ver. 2

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 71 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `MagicSFver2.sf2` | 71,183,704 (67.9 MB) | `ded4cc54347b5e6c8b157988ec1116676ddae944b84b527088c92a2fd8262c3f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/MagicSFver2.sf2) |

### Musica Theoria

Folder: `musica-theoria/` | Version: v2

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 30.5 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Musica_Theoria_v2_(GM).sf2` | 30,522,278 (29.1 MB) | `ee84e2c7dad370bca1cbd27d2a2847058ba5afe2a3f15148447fdcc4323d1e20` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Musica_Theoria_v2_%28GM%29.sf2) |

### Nintendo 64 GM SoundFont

Folder: `nintendo-64-gm/` | Version: ver. 2.0

License (as stated by the author): Ripped game console samples. **Local test asset only** (no redistribution license).

Note: GM-mapped N64 sample bank, 12.5 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Nintendo_64_ver_2.0.sf2` | 12,499,702 (11.9 MB) | `9979a7dea52892c2eb4a7a234af6df04b1edc39213519881b06c0fe0797c8b0f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nintendo_64_ver_2.0.sf2) |

### Nokia phone GM sets

Folder: `nokia-gm/` | Version: Series 30, Nokia 30, 3220 (RH-37), 6230i (RM-72), Series 40 5th Ed. FP1

License (as stated by the author): Ripped from Nokia phone firmware. **Local test asset only** (no redistribution license).

Note: GM; mobile-phone ROM GM sets from 0.2 to 8.7 MB, the same size class as a mask-ROM GM chip.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Nokia_30.sf2` | 2,193,356 (2.1 MB) | `3aedde6c3aed609c9300cfd1e16921871902a5fe42cff66a134996829158edea` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nokia_30.sf2) |
| `Nokia_3220__RH-37.sf2` | 3,116,748 (3.0 MB) | `df954d8a8b1f3cdb5b705c402f701e20187971a3b8ff049af54fca8072225650` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nokia_3220__RH-37.sf2) |
| `Nokia_6230i_RM-72_.sf2` | 227,480 (222.1 KB) | `df9d755f5bb49db3388ced688c274272843b24fe78159c4187ba68b29c9db5d4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nokia_6230i_RM-72_.sf2) |
| `Nokia_S30.sf2` | 6,295,582 (6.0 MB) | `62132dacbb81fd23460df06b55f841be4321b753258c4c1b121df9a6fc0c22f4` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nokia_S30.sf2) |
| `Nokia_Series_40_5th_edition_FP1__GM_.sf2` | 8,743,102 (8.3 MB) | `83ece4fde2bbc1c91a23d8c8a99f065d594a73848dd22b6e344e8d793fecee0b` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Nokia_Series_40_5th_edition_FP1__GM_.sf2) |

### PC51d

Folder: `pc51d/` | Version: 51d

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 58.8 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `PC51d.sf2` | 58,811,246 (56.1 MB) | `fd07e8b4c8da6511fe8baf7a9fbcaba32e429144d5da28a41741ef8669255c2b` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/PC51d.sf2) |

### RealFont

Folder: `realfont/` | Version: 2.1 (SF2) and 2.3 (sfArk)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 106 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `RealFont 2.3.sf2` | 203,374,750 (194.0 MB) | `3fbd94d0107b7bf94bdc229451d3efb4e69a2b1c12cb61141dee70216da36ba7` | unpacked from `RealFont 2.3.sfArk` with sfarkxtc |
| `RealFont 2.3.sfArk` | 84,575,937 (80.7 MB) | `f8fed2adbc756477d1a3ce2d60cf7c2a1978e6981727f400a583e274e8069efd` | [link](https://archive.org/download/GMSoundfonts/RealFont%202.3.sfArk) |
| `RealFont_2_1.SF2` | 105,991,124 (101.1 MB) | `d0886ed8cc884deeb745a5fb7012a9a864995b500f92bef81a7c0de48da970fa` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/RealFont_2_1.SF2) |

### SONiVOX GS250

Folder: `sonivox-gs250/` | Version: unversioned

License (as stated by the author): Commercial SONiVOX library. **Local test asset only** (no redistribution license).

Note: GM + GS; 264 MB (with the `.afpk` preset pack).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `SONiVOX_GS250.afpk` | 368 (368 B) | `64501b7269dee91cad78285b2d360081360c63050e88446575bb2cbbbd4e0de1` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/SONiVOX_GS250.afpk) |
| `SONiVOX_GS250.sf2` | 263,646,912 (251.4 MB) | `38cb03f7710ca5412e4bd9baacf4a87b346ecee2728c36329d9a8384dc3d73e6` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/SONiVOX_GS250.sf2) |

### SOMSAK SoundFont

Folder: `somsak/` | Version: 2016 V2.8

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 179 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Soundfont_SOMSAK_2016-V2.8.SF2` | 179,489,842 (171.2 MB) | `0c477d77afbb40c071f45c7fb5f0ee2b1ee38ea09d906d25fd141a7eb6a654b6` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Soundfont_SOMSAK_2016-V2.8.SF2) |

### Super Nintendo Unofficial (GM)

Folder: `snes-gm/` | Version: update

License (as stated by the author): Ripped game console samples. **Local test asset only** (no redistribution license).

Note: GM-mapped SNES sample bank, 1.9 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Super_Nintendo_Unofficial_update.sf2` | 1,852,478 (1.8 MB) | `4dac36355a0d8b163e14a5ddb7f2a422db647610f507a9bada225755f39f7763` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Super_Nintendo_Unofficial_update.sf2) |

### The Fairy Tale Bank

Folder: `fairy-tale-bank/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 210 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `The_Fairy_Tale_Bank.sf2` | 209,819,694 (200.1 MB) | `8a331057ff24b6238717533dfefe1fa58faf1f093468789e36dfe640a00dccd2` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/The_Fairy_Tale_Bank.sf2) |

### The Ultimate Megadrive SoundFont

Folder: `megadrive-gm/` | Version: v1.5

License (as stated by the author): Ripped game console samples. **Local test asset only** (no redistribution license).

Note: GM-mapped Sega Mega Drive (YM2612) bank, 63 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `The_Ultimate Megadrive_Soundfont[v1.5].sf2` | 63,248,366 (60.3 MB) | `21824792094d3ee16d7ba3a566945480b85edb62c8ac1af9ddb0eb6884e66978` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/The_Ultimate%20Megadrive_Soundfont%5Bv1.5%5D.sf2) |

### The Ultimate Wii SoundFont

Folder: `wii-gm/` | Version: V1-1

License (as stated by the author): Ripped game console samples. **Local test asset only** (no redistribution license).

Note: GM-mapped bank, 17.7 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `The_Ultimate_Wii_Soundfont_V1-1.sf2` | 17,681,950 (16.9 MB) | `df51eac2a5e50c0dd7785697ae4a5539829f4d7a61baad4c87973c3f8d726cf7` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/The_Ultimate_Wii_Soundfont_V1-1.sf2) |

### The Xioad Bank

Folder: `xioad-bank/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 64.7 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `The_Xioad_Bank.sf2` | 64,655,700 (61.7 MB) | `c17a7947e42c3cb23a50674fac7d4d865cacfd0e1723bc64cafd8dcde81fc602` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/The_Xioad_Bank.sf2) |

### Ultimate

Folder: `ultimate-sf2/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 8.5 MB AWE-era bank.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Ultimate.SF2` | 8,525,956 (8.1 MB) | `f10db369482486a34138da13492adea38dfbb187ff2af088f66b80b842764f24` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/Ultimate.SF2) |

### WeedsGM

Folder: `weeds-gm/` | Version: 3

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 54.9 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `WeedsGM3.sf2` | 54,894,076 (52.4 MB) | `4de36fdec6a1f972d3b32ac35ff1c3178ceb0fc05ff5c9fb94aebde5197f8568` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/WeedsGM3.sf2) |

### AnotherGS (bennetng)

Folder: `anothergs-bennetng/` | Version: v2.1

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 34 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `bennetng_AnotherGS_v2-1.sf2` | 34,117,830 (32.5 MB) | `b59614082822a7719ceed68a3787e22b24a185ffe08fa744a7da41eac59681e0` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/bennetng_AnotherGS_v2-1.sf2) |

### Cadenza LP

Folder: `cadenza-lp/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 46 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `cadenza-lp.sf2` | 45,964,872 (43.8 MB) | `360dfad13a237892ff4e387666d3b643caf05215470634b787cdaa4e4bf7a26f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/cadenza-lp.sf2) |

### PH-GM2

Folder: `ph-gm2/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 39.7 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `ph-gm2.SF2` | 39,728,906 (37.9 MB) | `87a3ec9d3bc150c825266da50bf47d148e10a6f7f252a0be073dd47a7cc8dd3f` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/ph-gm2.SF2) |

### Synergi 8 MB

Folder: `synergi-8mb/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 8.5 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `synergi-8mb.sf2` | 8,492,190 (8.1 MB) | `73bb27ae8a36052e54fdb4749aed971b0d3b1f8ba83a4621995db8c7d963b9a9` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/synergi-8mb.sf2) |

### TGK3

Folder: `tgk3/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 42 MB.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `tgk3.SF2` | 42,353,044 (40.4 MB) | `1dcfda25cc9d61d8d4c9ca8c4f997c9cb899f5ff4ea5ed2cd4d46f62018bec36` | [link](https://archive.org/download/free-soundfonts-sf2-2019-04/tgk3.SF2) |

### Hyper-VSC SF-GS (Bill90) / Hyper SC mix GS

Folder: `hyper-vsc-bill90/` | Version: v2

License (as stated by the author): File name says "Grey Area - Nonfree" (Roland VSC samples). **Local test asset only** (no redistribution license).

Note: GS; banks built from the Roland Virtual Sound Canvas; sfArk unpacked with sfarkxtc. Both sfArk files unpack to the same SF2 (SHA-256 `33b991e7...`), so only the first unpacked copy is kept.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Bill90 Hyper-VSC SF-GS v2 [Grey Area - Nonfree].sf2` | 86,531,178 (82.5 MB) | `33b991e762bf3fe0a20c828d87e7c66063d15565cdee0a7774e3dbf54e0a11c0` | unpacked from `Bill90 Hyper-VSC SF-GS v2 [Grey Area - Nonfree].sfArk` with sfarkxtc |
| `Bill90 Hyper-VSC SF-GS v2 [Grey Area - Nonfree].sfArk` | 40,704,256 (38.8 MB) | `5e3461dc38fb59b416ded6368f8de59b52d61d0792f14c761fe0f3e98e59f232` | [link](https://archive.org/download/GMSoundfonts/Bill90%20Hyper-VSC%20SF-GS%20v2%20%5BGrey%20Area%20-%20Nonfree%5D.sfArk) |
| `Hyper_SC_mix_GS_v2.sfArk` | 40,704,227 (38.8 MB) | `018c2d1e23fcfd8306661d04381a2b2b3b88434e5edd12fcc88cfb810e9f9b75` | [link](https://archive.org/download/GMSoundfonts/Hyper_SC_mix_GS_v2.sfArk) |

### ETERNAL

Folder: `eternal/` | Version: V6.0.1 (m1 hotfix 2020-11-08)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 1.8 GB sfArk. sfarkxtc aborts this file reproducibly at 1,267,523,584 bytes (header announces 3,151,079,716); the truncated output was removed, only the `.sfArk` is kept.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `ETERNAL V6.0.1.eternal6_m1_hotfix-20201108.sfArk` | 1,809,388,611 (1.7 GB) | `c224014d583c95efbc7bca490bd2594543d0667d75aae8cdba3585ead0906fb0` | [link](https://archive.org/download/GMSoundfonts/ETERNAL%20V6.0.1.eternal6_m1_hotfix-20201108.sfArk) |

### Giant SoundFont (melodic bank)

Folder: `giant-soundfont/` | Version: V6.5

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 354 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `GiantSoundfontV6.5MelodicBank.sf2` | 1,114,485,860 (1.0 GB) | `4ae40fa783e00b430102dae093eb8e707fe575d5e462ae4dda9104cd413486aa` | unpacked from `GiantSoundfontV6.5MelodicBank.sfArk` with sfarkxtc |
| `GiantSoundfontV6.5MelodicBank.sfArk` | 353,539,660 (337.2 MB) | `f1933201b02bb317d881be678be87be7ce7e5053bb010ec2e6538b8943529d3e` | [link](https://archive.org/download/GMSoundfonts/GiantSoundfontV6.5MelodicBank.sfArk) |

### HiDef

Folder: `hidef/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 2.78 GB sfArk. Unpacked with sfarkxtc to a 4.2 GB SF2 (larger than the 3 GB download limit, kept because it was produced locally).

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `HiDef.sf2` | 4,235,617,758 (3.9 GB) | `ec6c44252915bd2c49d8caa48a7dc8df1aebb0fbc5cf5e5ee9a38ff412837750` | unpacked from `HiDef.sfArk` with sfarkxtc |
| `HiDef.sfArk` | 2,777,167,122 (2.6 GB) | `983db217fa4497e624de74972977f7bb0e0e23c4947ab23f8fa8b45e46290e13` | [link](https://archive.org/download/GMSoundfonts/HiDef.sfArk) |

### Isseki (balanced CFG)

Folder: `isseki/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 34 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Isseki balanced CFG.sf2` | 71,251,952 (68.0 MB) | `b452b7983dde91f887ebfecf6d9ccc19501d35a411982587acddfab3d3afe6bb` | unpacked from `Isseki balanced CFG.sfArk` with sfarkxtc |
| `Isseki balanced CFG.sfArk` | 33,955,924 (32.4 MB) | `461205b6922fee1f0da5aea9bd9a56a61f4300025bd8fcee3bc796c48c33e63a` | [link](https://archive.org/download/GMSoundfonts/Isseki%20balanced%20CFG.sfArk) |

### KOR SoundFont - GM Complete

Folder: `kor-gm/` | Version: 5.0

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 874 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `KOR Soundfont - GM Complete 5.0.sf2` | 2,085,939,832 (1.9 GB) | `52c887875c9b19d89f521bcb2d31dcec52e9937124f8b71269a5b10591064473` | unpacked from `KOR Soundfont - GM Complete 5.0.sfArk` with sfarkxtc |
| `KOR Soundfont - GM Complete 5.0.sfArk` | 873,850,956 (833.4 MB) | `a75c97cbe7db303a36b4a8bc4afbd44885590f2ed8ebf72ff379f424976096ec` | [link](https://archive.org/download/GMSoundfonts/KOR%20Soundfont%20-%20GM%20Complete%205.0.sfArk) |

### LauGM

Folder: `laugm/` | Version: 1.0 and 1.1

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 61 MB sfArk each.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `LauGM_v1.1.sf2` | 130,434,570 (124.4 MB) | `7e0d7ec565032f87e42cb6d602ce58a1fc632fb756364a20e4af6ffd5154c563` | unpacked from `LauGM_v1.1.sfArk` with sfarkxtc |
| `LauGM_v1.1.sfArk` | 61,360,557 (58.5 MB) | `b188c09e6b429373e51832b86a37930da0c54f9cd993005ee157f0f9a4c08198` | [link](https://archive.org/download/GMSoundfonts/LauGM_v1.1.sfArk) |
| `LauGMv1.0.sf2` | 130,264,294 (124.2 MB) | `72490d285f55c72676bab728ad95d04a1b89cd11853026205d58ae60fea95d44` | unpacked from `LauGMv1.0.sfArk` with sfarkxtc |
| `LauGMv1.0.sfArk` | 61,315,592 (58.5 MB) | `eba80dabd49a6c0fc710cd45c5f5e968e9bf55c255574703ef246b0ee096a995` | [link](https://archive.org/download/GMSoundfonts/LauGMv1.0.sfArk) |

### Miracle

Folder: `miracle/` | Version: 2 and 2C

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; small (5 to 8 MB) AWE-era banks, sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Miracle2.sf2` | 12,580,080 (12.0 MB) | `1c783336f030331a1c1ef435195183f1ee9d980f17bafd15101f367d91eb5d9f` | unpacked from `Miracle2.sfArk` with sfarkxtc |
| `Miracle2.sfArk` | 7,582,383 (7.2 MB) | `0f08cd45c62f794e00b89a1ae31e77b05c9f0ea8f58c94cd29e6ba309d8450f7` | [link](https://archive.org/download/GMSoundfonts/Miracle2.sfArk) |
| `Miracle2C.sf2` | 7,217,366 (6.9 MB) | `88fa417519220c328f30ed841c644adc80edd6d192477394da58ea0dcb39e599` | unpacked from `Miracle2C.sfArk` with sfarkxtc |
| `Miracle2C.sfArk` | 4,659,558 (4.4 MB) | `eb47fb5e1761433daad184a7262f5e8904927d5320d3edc300e676801392958a` | [link](https://archive.org/download/GMSoundfonts/Miracle2C.sfArk) |

### Musyng (predecessor of Musyng Kite)

Folder: `musyng/` | Version: unversioned

License (as stated by the author): Freeware, no license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 590 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Musyng.sf2` | 1,747,075,246 (1.6 GB) | `6455b86a03b79f3c3b03ee44ba514468332f85d3178d53f933357b075f0a5e07` | unpacked from `Musyng.sfArk` with sfarkxtc |
| `Musyng.sfArk` | 589,984,099 (562.7 MB) | `78e52c22d9e87e7bf1cad11dd9589e7102c553903100e5fed8efa06d5a8f85b8` | [link](https://archive.org/download/GMSoundfonts/Musyng.sfArk) |

### NeoGM

Folder: `neogm/` | Version: unversioned

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 1.65 GB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `NeoGM.sf2` | 2,972,040,504 (2.8 GB) | `1395325de75d11b717dd4b769ad94ee8e0ee04695e3251a2fed29030ccb687cc` | unpacked from `NeoGM.sfArk` with sfarkxtc |
| `NeoGM.sfArk` | 1,650,042,360 (1.5 GB) | `075b590c379c3f5278fb9863d457626d5271ea68f349e646984da7d2d2c14d28` | [link](https://archive.org/download/GMSoundfonts/NeoGM.sfArk) |

### Orpheus

Folder: `orpheus/` | Version: 1.047e

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + GS; 526 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Orpheus_1.047e.sf2` | 1,267,418,150 (1.2 GB) | `2cc953eb4bbc765f3627c0c4813d9830a12162f92dab5bbd1c3246cb9528c0ef` | unpacked from `Orpheus_1.047e.sfArk` with sfarkxtc |
| `Orpheus_1.047e.sfArk` | 525,858,623 (501.5 MB) | `1dac38d535d137dbebc981c3820045a325e04bb741e52de876a442c7b10783fd` | [link](https://archive.org/download/GMSoundfonts/Orpheus_1.047e.sfArk) |

### Papelmedia SF2 GM

Folder: `papelmedia-gm/` | Version: 2007

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM; 106 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Papelmedia SF2 GM 2007.sf2` | 241,069,824 (229.9 MB) | `d91500987b56a2d114932dd1e0cb240aae503265e381fad862fae6f653193120` | unpacked from `Papelmedia SF2 GM 2007.sfArk` with sfarkxtc |
| `Papelmedia SF2 GM 2007.sfArk` | 105,807,693 (100.9 MB) | `e50b14a7e64ce9e16c426ee24c0a49deb0bd1c68252e1c14a7550e3b8cd4d7eb` | [link](https://archive.org/download/GMSoundfonts/Papelmedia%20SF2%20GM%202007.sfArk) |

### Realistic SoundFont V2 Libre

Folder: `realistic-libre/` | Version: V1 (16-bit, sub-2 GiB, XGMS)

License (as stated by the author): No license text. **Local test asset only** (no redistribution license).

Note: GM + XG; 534 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Realistic_Soundfont_V2_Libre_V1__16bit_Sub2GiB_XGMS.sf2` | 1,766,147,204 (1.6 GB) | `cf8ac9329055a1faf0b5071e7cdbddf68c1954986862ac2c91b3c94036ec5a9b` | unpacked from `Realistic_Soundfont_V2_Libre_V1__16bit_Sub2GiB_XGMS.sfArk` with sfarkxtc |
| `Realistic_Soundfont_V2_Libre_V1__16bit_Sub2GiB_XGMS.sfArk` | 533,894,832 (509.2 MB) | `40fc43846a9bf7a5aa7e557f3a93aed7d104edeab0b25f44a7834b3cf92d3329` | [link](https://archive.org/download/GMSoundfonts/Realistic_Soundfont_V2_Libre_V1__16bit_Sub2GiB_XGMS.sfArk) |

### Titanic 200 GM/GS (Luke Sena)

Folder: `titanic-200/` | Version: v1.2

License (as stated by the author): File name says "[NC]" (non-commercial). **Local test asset only** (no redistribution license).

Note: GM + GS; 122 MB sfArk.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `Titanic 200 GM GS v1.2 [Luke Sena] [NC].sf2` | 288,906,882 (275.5 MB) | `79abfb5d7e6ae5e4e5a26262c465e45673fb0b66fcce11632fe2529506ccbb99` | unpacked from `Titanic 200 GM GS v1.2 [Luke Sena] [NC].sfArk` with sfarkxtc |
| `Titanic 200 GM GS v1.2 [Luke Sena] [NC].sfArk` | 121,588,934 (116.0 MB) | `2aab34e176ad63be4df2b505d9159dae5b6d936aac60ba6f60e80fd76417c8f4` | [link](https://archive.org/download/GMSoundfonts/Titanic%20200%20GM%20GS%20v1.2%20%5BLuke%20Sena%5D%20%5BNC%5D.sfArk) |

### PersonalCopy (Jim Roe)

Folder: `personal-copy/` | Version: 5.1f (INFO: v5r3f, 2002)

License (as stated by the author): "Redistributable, no modification permitted" (as packaged by Linux distributions); INFO chunk: "PersonalCopy.com Networks - 2002".

Note: GM; 120 MB classic Doom-era bank; `PC51f.sf2.gz` from the author's site, gunzipped to `PC51f.sf2`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `PC51f.sf2` | 63,017,562 (60.1 MB) | `c069060d8f39389c1544c40b54a92f928f86fecb868d6b8e44cad63fb5e0412b` | gunzip of `PC51f.sf2.gz` |
| `PC51f.sf2.gz` | 53,770,806 (51.3 MB) | `e69cf923897022dcf6416ab8d433f877a9f2734fde2ecaa0bb270a17ba63cd16` | [link](http://www.personalcopy.com/Downloads/PC51f.sf2.gz) |

### OPL2/OPL3 FM GM instrument banks (libADLMIDI collection)

Folder: `opl-fm-banks-libadlmidi/` | Version: libADLMIDI master (downloaded 2026-10-03)

License (as stated by the author): Per bank (`fm_banks/LICENSE-*.txt`, `list-of-banks.txt`); library LGPL-2.1 / GPL-3.

Note: GM FM patch banks for the Yamaha OPL2/OPL3 (AdLib, Sound Blaster FM): AIL/Miles `.ad`/`.opl`, HMI `.bnk`, DMX `.op2` (Doom), `.ibk`, `.tmb`, `.wopl`. Only `fm_banks/` and the license files were unpacked from the repository zip.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `libADLMIDI-master.zip` | 3,070,605 (2.9 MB) | `f83c5d4df735ae560686297b7bb8f46ea64f3e85901cdce0e2aa521a66516e7e` | [link](https://codeload.github.com/Wohlstand/libADLMIDI/zip/refs/heads/master) |
| `libADLMIDI-master/` (directory, 196 files) | 2,540,333 (2.4 MB) | - | extracted from `libADLMIDI-master.zip` |

### OPN2 (YM2612) FM GM instrument banks (libOPNMIDI collection)

Folder: `opn-fm-banks-libopnmidi/` | Version: libOPNMIDI master (downloaded 2026-10-03)

License (as stated by the author): Per bank (readme files next to each `.wopn`); library LGPL-2.1 / GPL-3.

Note: GM FM patch banks for the Yamaha YM2612/YM2608 (OPN2/OPNA) as `.wopn`.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `libOPNMIDI-master.zip` | 1,431,240 (1.4 MB) | `121c17f398071d34157fda8c4ceab273e566ca26015a9b432eaeeed521479c3b` | [link](https://codeload.github.com/Wohlstand/libOPNMIDI/zip/refs/heads/master) |
| `libOPNMIDI-master/` (directory, 26 files) | 1,005,164 (981.6 KB) | - | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/Doom32x-fixx.wopn` | 17,750 (17.3 KB) | `b44805d82a211a7d84824cb6911f335ad1bcbe46c6adc8e233971ae0ac0739e5` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/Nineko.wopn` | 53,214 (52.0 KB) | `74c49cea0fa118af979594598c7b9af16e09ea35a01bb7f6e76374493e110dfa` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/Tiny Toon Adventures.wopn` | 26,616 (26.0 KB) | `dfd03186692baced0a8ceeebf5176c998de5ab97c3aecb747bd58d70ecd60bed` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/Tomsoft.wopn` | 62,080 (60.6 KB) | `bbae9d7b7275035f487a310ea3fbd70cee243daff3f3aad9c4fd5b000ddb7190` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/fmmidi.wopn` | 17,750 (17.3 KB) | `5464425499c776625505218554ec7ee255c14a978e551103a5e07721a44650f0` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/gems-fmlib-gmize.wopn` | 62,080 (60.6 KB) | `bab2f14758e6a2466115f9b08046618e317cfd143cb88a5350867b2721a803b4` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/gm-old.wopn` | 17,750 (17.3 KB) | `da6f2ba73368c9338fafaf914e240cd7df7483edbef65ceba6abf820402f46a6` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/gm.wopn` | 186,204 (181.8 KB) | `1e8dfe7d799a3c9a61337971b8ef9dfc36e9ee9cf1bb9242b061d2e481b8d6c9` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/gs-by-papiezak-and-sneakernets.wopn` | 274,864 (268.4 KB) | `488327960374b95dcc302e5586622a62fc21bdda7082f92a40258002ed267f40` | extracted from `libOPNMIDI-master.zip` |
| `libOPNMIDI-master/fm_banks/xg.wopn` | 186,204 (181.8 KB) | `1e8dfe7d799a3c9a61337971b8ef9dfc36e9ee9cf1bb9242b061d2e481b8d6c9` | extracted from `libOPNMIDI-master.zip` |

### DMXOPL (OPL3 GM bank for Doom's DMX)

Folder: `opl-fm-dmxopl/` | Version: DMXOPL3 branch and release v2.13a (`GENMIDI.GS.wopl`)

License (as stated by the author): MIT (repository).

Note: GM (+ GS in the `.wopl`); modern OPL3 replacement for Doom's `GENMIDI` FM bank; showcase MP3s are part of the repository zip.

| File | Size | SHA-256 | Source |
|------|------|---------|--------|
| `DMXOPL-DMXOPL3.zip` | 46,657,854 (44.5 MB) | `152e4d479a7cd314fb5802b234a808e3f318a9cd5350e268937533281d8debc0` | [link](https://codeload.github.com/sneakernets/DMXOPL/zip/refs/heads/DMXOPL3) |
| `GENMIDI.GS.wopl` | 118,767 (116.0 KB) | `57f96bb2b5947a21e9af8febc2a318e9374af3cc7b0700832404af94567edfc5` | [link](https://github.com/sneakernets/DMXOPL/releases/download/v2.13a/GENMIDI.GS.wopl) |
| `DMXOPL-DMXOPL3/` (directory, 325 files) | 47,639,458 (45.4 MB) | - | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI - Vanilla.op2` | 11,908 (11.6 KB) | `45855c59f7b330d34f8d49c14962d8db12875b6e46ea7352b855165e8f238e78` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI(GS).wopl` | 212,069 (207.1 KB) | `f2e7fb4b644f26f0650b41b8f7d324dedd86c420d14d156f4bae9a1e58be9161` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI(Kirby test).wopl` | 101,803 (99.4 KB) | `acc0c1f1a7058b5fb798534481567ada9168f30e9d5e7bb153250ad06fe48f22` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI(XG).wopl` | 212,069 (207.1 KB) | `613df60cf4f040ec1b6d9006c848d7325192aa324e045b4bcec8325a00ada58b` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI.op2` | 11,908 (11.6 KB) | `92fc03c7734c45b1ece2bfad1f79d1abbd2b27ec6b45fbfb1c5a74f5ce0138c9` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/GENMIDI.wopl` | 16,983 (16.6 KB) | `87bc5cf20e49a37e15557c52d95d25ffd153728ff8dcfb8166f92469cfe39862` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/Contributions/Papiezak/GENMIDI (papiezak).wopl` | 33,947 (33.2 KB) | `ef951b041e8150958b6c14d097fd619ac1434a805fa4615145cbfdaf3a92827d` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/Contributions/Patch93/GENMIDI(Patch93).op2` | 11,908 (11.6 KB) | `579e83f1122f3f36fd31d7365873842dfe7b096755460f874248ae59667e59e1` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/attic/GENMIDI2.op2` | 11,908 (11.6 KB) | `bc6074358ad233b4b9015d3f0d3fdcbb426ded7ce3a41e1f28f2ec065afc2e63` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/attic/GENMIDIsnare.op2` | 11,908 (11.6 KB) | `a2b416029476d2632980e12c77d0197de5386bf8d6971f8fb9c48de9ced12919` | extracted from `DMXOPL-DMXOPL3.zip` |
| `DMXOPL-DMXOPL3/idgames/DMXOPL.ZIP` | 11,616 (11.3 KB) | `ee4341607621865a334ce5cbd7d07c643817575c033e6e9623a6f41c1dff92e0` | extracted from `DMXOPL-DMXOPL3.zip` |

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
- FreePats: [freepats.zenvoid.org](https://freepats.zenvoid.org/SoundSets/general-midi.html)
- PersonalCopy: [personalcopy.com](http://www.personalcopy.com/)
- FM banks: [libADLMIDI](https://github.com/Wohlstand/libADLMIDI), [libOPNMIDI](https://github.com/Wohlstand/libOPNMIDI),
  [DMXOPL](https://github.com/sneakernets/DMXOPL)
- archive.org collections used: [free-soundfonts-sf2-2019-04](https://archive.org/details/free-soundfonts-sf2-2019-04),
  [soundfonts_201910](https://archive.org/details/soundfonts_201910) (Sound Blaster family banks),
  [GMSoundfonts](https://archive.org/details/GMSoundfonts) (sfArk collection),
  [soundfont-collection_202601](https://archive.org/details/soundfont-collection_202601),
  [sound_blaster_archive](https://archive.org/details/sound_blaster_archive),
  [musyng-kite](https://archive.org/details/musyng-kite)

## Tools

`.sfArk` files were unpacked with `sfarkxtc` compiled from source
([raboof/sfarkxtc](https://github.com/raboof/sfarkxtc) + [raboof/sfArkLib](https://github.com/raboof/sfArkLib), GPL-3)
outside the repository; the downloaded sfArk Windows/Mac tools bundled in the Arachno zip were not run. Check: the
Arachno sfArk unpacks to exactly the same bytes as the author's SF2. An unpacked file is named after its `.sfArk`;
where that name was already taken by a downloaded SF2 it gets the suffix ` (from sfArk)`. Unpacked copies that came
out byte-identical to a file already present were removed again (noted in the section).
Not unpacked: `eternal/ETERNAL V6.0.1...sfArk` and `dsoundfont/DSoundfont Ultimate.sfArk` (sfarkxtc aborts them
reproducibly part-way; truncated outputs removed) and `unison/Unison/Unison.sfArk` (sfArk V1 format, unsupported).

## Not obtained

- **Dream CleanWave GM set of the SAM2695 itself**: it lives in the chip's mask ROM and Dream does not distribute
  it. The DreamBlaster S2 cannot load banks, so no S2 bank exists. The only "SAM2695" SF2 found
  (`dream-sam2695-sf2/`) is a community build, not a ROM dump. Closest material here: the Dream GMBK5X banks and
  the DreamBlaster community banks.
- **Musical Artifacts** (`musical-artifacts.com`): every scripted request (direct `/artifacts/...` and `/uploads/...`
  URLs, WebFetch) gets a Cloudflare challenge (HTTP 403); web.archive.org holds only the site's static images, no
  bank files; the Chrome browser extension was not connected in this session. Missing because of this:
  [Chorium RevB](https://musical-artifacts.com/artifacts/1474) (`Chorium_fork.sf2`, 27.7 MB),
  [Chorium Rev A: Albatwo MOD](https://musical-artifacts.com/artifacts/3567), the site's own copy of
  [Yamaha db50 XG](https://musical-artifacts.com/artifacts/2809) (`Yamaha_DB50XG_Presets.sf2`; a file of that name is
  in `yamaha-xg-collection/`). These three pages answer scripted link checks with HTTP 403 as well; open them in a browser.
- **polyphone.io soundfont catalogue** (444 entries tagged GM / GS / XG / MT-32, mostly phone and game ROM rips,
  e.g. "Windows MIDI Converted to SF2"): every download needs a signed-in account; not created.
- **VOGONS "Soundfonts that mimic old hardware" (t=45600)**: the 2015 SC-55 / DB50XG / AWE64 Gold preset banks were
  on OneDrive; the links now answer HTTP 403.
- **PersonalCopy Lite 4.1 (`PCLite.sf2`)**: no longer on the author's site (404), the Fedora package is retired;
  only the full PersonalCopy 5.1f was obtained.
- **GeneralUser GS 1.511 / 1.52 and 1.43**: no copy found (the web.archive.org snapshot of the 1.43 zip is a
  758-byte error page). 1.35, 1.4, 1.44 (three builds), 1.442, 1.471, 2.0.2, 2.0.3 beta are in `generaluser-gs-older/`.
- **Larger than 3 GB, skipped**: archive.org `sc-55-sf2` (`SC-55 SF2.7z`, 3,105,695,916 bytes, a single 7z) and
  `apollo-gmgs-v-1.105` (`Apollo GMGS v1.105.sf2`, 4,239,641,894 bytes).
- **Bulk mixed collections, not mirrored as a whole** (mostly instrument, loop and game soundfonts; the GM banks
  inside them that are listed above were downloaded one by one): archive.org
  `500-soundfonts-full-gm-sets` (39.4 GB), `ZSF-Distribution` (39.5 GB), `soundfont-collection-1` (34.4 GB),
  `Soundfonts-collection-anapan.ca-2024-misc` (10.6 GB), `gabedudleyssf2collection` (5.4 GB), `z-doc-soundfonts`
  (4.7 GB), `sf2-soundfonts-free-use` (2.9 GB zip), the instrument folders of `soundfont-collection_202601`, and the
  Edirol SD-90 single-instrument rips (2.7 GB).
- **Sound Blaster Live! / Audigy / AWE64 driver CD images** (0.3 to 1.3 GB ISOs on archive.org): not downloaded;
  the banks they ship (CT2MGM, CT4MGM, CT8MGM, 8MBGMSFX, AWE64 SFBANK) are already here from
  `sound_blaster_archive` and `AWE64.zip`.
- **Roland / Yamaha mask-ROM dumps** (archive.org: SC-55 / SC-55mkII / SC-8850 wave ROMs, MT-32 / CM-32L ROMs):
  raw chip images for hardware emulators (Nuked-SC55, Munt), not sound banks; out of scope, not downloaded.
- **SAM2195 datasheet on docs.dream.fr**: removed there (HTTP 404); the copy here comes from Serdaco.
- **A separate SAM2695 MIDI implementation chart or other SAM2695 application notes**: Dream publishes none
  besides the datasheet (which contains the MIDI implementation) and the SAM2195 migration note.
- Not downloaded on purpose: the X3M copies of the Serdaco banks (same banks as the X2 files).
