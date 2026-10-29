# Wild Sound test data

Programs and modules for the Wild Sound ISA card on the Sprinter
([requirements](../../../docs/inprogress/2026-10-09-wildsound/requirements.md) R40 and acceptance §3,
[design](../../../docs/inprogress/2026-10-09-wildsound/design.md) §10). The XM and S3M EXEs play one track
through the card's XM player: the original `prosiak.exe`, and EXEs built from it with the converter and the linker
in [`tools/machines/sprinter/wildsound/`](../../../tools/machines/sprinter/wildsound/README.md). The PT3 EXEs play
through the card's two AY chips ([`wsay`](../../../tools/machines/sprinter/wildsound/wsay/README.md)).

```text
README.md                 this file
wildsound.ucompose.yaml   the folder as a FAT16 disk for ide0.slave (DSS drive D:)
prosiak.exe               the original (Rob F.)
xm/                       consul.exe faster2.exe discharz.exe anarchy.exe youwould.exe test_xm.exe
s3m/                      frontend.exe techrock.exe test_s3m.exe
pt3/                      spring.exe (one chip) summer.exe melo.exe (TurboSound: both chips)
src/                      the source modules
ref/                      reference data of the openmpt123 renders (JSON: 20 ms RMS envelope, zero crossings)
```

## Files

### Programs

| File | Bytes | SHA-256 | Track | Built from |
|:--|--:|:--|:--|:--|
| `prosiak.exe` | 150 003 | `84b0264cda3df88e7df16acc7a3a626eebf247c3c4f677e1ef7220ab4c864de9` | Prosiak (`src/prosiak.xm`) | the original |
| `xm/consul.exe` | 138 479 | `2c3fc4702297b4f2ab07973fc45f3eb19ccf1eac2f9cfd78c15be51ead1faa14` | "I am BAD Coder." | `src/consul.xm` |
| `xm/faster2.exe` | 97 655 | `3e774257b15aea6b41cf48dfe45ef76664857afd7926fd2c88a958948b7dd552` | "Japanesed Faster ..." | `src/faster-2.xm` |
| `xm/discharz.exe` | 279 176 | `2498587807461b134248461bddbe5790a22518ca8a4cb353ff02f06bfc1bdab0` | "Discharge" | `src/discharz.xm` |
| `xm/anarchy.exe` | 5 068 | `a9f4cdb8f2d4286e1108c8714f6649c49d36a30a80fd4d3911e7213af0702104` | "Controlled Anarchy" | `src/npo_-_controlled_anarchy.xm` |
| `xm/youwould.exe` | 22 193 | `4f2690db968569052adfa6c22a20bca2e5d2432270391abd8789e80ae6a4ecfb` | "You Would Be Here" | `src/miafan2010_-_you_would_be_here.xm` |
| `xm/test_xm.exe` | 3 666 | `79b78eb236fd4386eacd251189275ab5db034bf1150a7722a701d0978e930fed` | "Test Module" | `src/test.xm` |
| `s3m/frontend.exe` | 5 757 | `c6d4162949df5aaf9db9a371bc373f98fca5eeda31083034c7144a12c5bab577` | "A New Frontend" | `src/kj_jose_-_a_new_frontend.s3m` |
| `s3m/techrock.exe` | 57 623 | `c21b9f94eda927eab095cd55da2de4fdf384bb7c6ad1418447ed0ffeea5ed961` | "kc-techrockloop" | `src/kc-techrockloop.s3m` |
| `s3m/test_s3m.exe` | 3 576 | `07203dea83aac2189f5e93ef771dd9aef6561d8b272fe8920bc55b9179546c54` | "S3M_Test" | `src/test.s3m` |
| `pt3/spring.exe` | 9 309 | `7f0e76ab3336401b827f5b9f39114a341f500e198bf318f39e99a81ac9c76355` | "-=+ SpRiNg +=- version for cc999", SirIl/4D, 1999 (one chip) | `src/spring.pt3` |
| `pt3/summer.exe` | 9 199 | `019bfb5077829ff49208d4ab07378a5bcdcfb03987b5011b7bf604716e7f2f1e` | "Dreaming of Summer", MmcM (TurboSound) | `src/summer.pt3` |
| `pt3/melo.exe` | 21 745 | `df986fcacf66ce3c7aa96fae03770434e640a27e735f5197388d70566973157f` | "Stellar One", Cj Splinter feat. MmcM (TurboSound; module 2 runs into window 3) | `src/melomagic.pt3` |

PT3 sources: `spring.pt3` (6 216 bytes, SHA-256 `f26229a8...`) from github.com/mborik/ayplayers `songs/` (commit
`454e283`); `summer.pt3` (3 934 bytes, `88deea74...`) and `melomagic.pt3` (16 480 bytes, `5961381206...`) from the
ZX Spectrum Next distribution's `demos/ts-songs/` (its `authors.txt`: taken from zxart.ee).

The stage-2 loader prints the track name as its banner line (wslink's default) between "This Text From Shadow
Page ..." and "XM Init ... OK"; `prosiak.exe` prints its own `!!! WOW HOW DOW !!!`.

### Source modules and what each track tests

| File | Bytes | SHA-256 | Origin | Licence |
|:--|--:|:--|:--|:--|
| `src/prosiak.xm` | 177 815 | `ee867c3d67336411f7de732d7fc0cfb228d3f2006488b8a6a97627c00a28d51d` | Wild-Sound repository (below), `MasonXM/Music/PROSIAK.XM` | none stated |
| `src/consul.xm` | 181 771 | `c0b745e02a5d876891bc62e3f597b0cc13c89f7220fbd537f229de6208e40653` | same, `CONSUL.XM` | none stated |
| `src/faster-2.xm` | 132 457 | `fb0b6f33fba5a4730fd06dbada59faf51e08388348880a3e63d60d5ff522b6d3` | same, `FASTER-2.XM` | none stated |
| `src/discharz.xm` | 359 957 | `7e16d691c9103c21228851a30e951fd17b816f25cae80eedb3ecf0e0f7e4e635` | same, `DISCHARZ.XM` | none stated |
| `src/npo_-_controlled_anarchy.xm` | 25 726 | `4dd9d49ae7e12b8be3a8d49b8e5a3b3523b8962b86a787706dbf6d3ab254c97f` | The Mod Archive [#173441](https://modarchive.org/index.php?request=view_by_moduleid&query=173441), New Plum Order | Public Domain (Mod Archive licence field) |
| `src/miafan2010_-_you_would_be_here.xm` | 27 529 | `98bb745c0ccabe831f19f3d9124d0ea538e8c14c1a4a0f57e0159cbff1951ee5` | The Mod Archive [#172936](https://modarchive.org/index.php?request=view_by_moduleid&query=172936), Katie Cadet | Public Domain |
| `src/test.xm` | 6 279 | `38995d872538fcf469efd9ef46840f397bdf6220f393cb7f849fe5258a208293` | [OpenMPT](https://github.com/OpenMPT/openmpt) `test/test.xm` (last change f5e896e3, 2024-11-04) | BSD-3-Clause (OpenMPT) |
| `src/kj_jose_-_a_new_frontend.s3m` | 18 336 | `92ef772586a6d4a4101d887a661aff966d57fe55c7107a55fdddecff6c7f4d02` | The Mod Archive [#178540](https://modarchive.org/index.php?request=view_by_moduleid&query=178540), K. Jose | Public Domain |
| `src/kc-techrockloop.s3m` | 69 648 | `0004c6ba9a9243d6d857e672b3f7a19e1062d620b4af5bcec6589354a90f9c7a` | The Mod Archive [#175418](https://modarchive.org/index.php?request=view_by_moduleid&query=175418), Katie Cadet | Public Domain |
| `src/test.s3m` | 880 | `8c06df94967dd66df35d2cf200ec155542c47d0346acb32d9725d739e9c348ab` | OpenMPT `test/test.s3m` (last change 7ce001e3, 2024-11-09) | BSD-3-Clause (OpenMPT) |

- **Wild-Sound repository**: [github.com/RobusAsmCoder/Wild-Sound](https://github.com/RobusAsmCoder/Wild-Sound),
  commit `edd3b11b` (2021-05-23), folder `STM_AYX32_TSL/TMT/MasonXM/Music/`: the card author's (Rob F.) own test
  set for his MasonXM converter. The repository has no licence file; the modules are used here as test material only
  ([testdata/NOTICE.md](../../NOTICE.md)).
- **`prosiak.exe`**: Rob F.'s Wild Sound demo, the program on the MAME Sprinter pack's system disk
  (`sp_hdd_sys.chd`, `DEMOS/WILDSND`); this copy from the Sprinter developer notes collection
  (`sprinter/peripherals/developer-interest/disk-notes/prosiak.exe`). No licence stated; test material.
- **The Mod Archive modules**: the licence is the field on each module's page ("Licensed under the Public Domain
  license", 2026-10-09). Downloaded from `https://api.modarchive.org/downloads.php?moduleid=<id>`; the MD5 on each page
  matches the file.
- **OpenMPT `test/`**: unit-test modules of the OpenMPT tracker, under the project's BSD-3-Clause licence
  (copyright OpenMPT Project Developers and Contributors). Raw files from the `master` branch (at `c1331b0f`).

What the tracks cover (effects as used in the patterns of the order list; "env" = instruments with a volume or
panning envelope, which the card's upload format has no room for):

| Track | Ch | Freq | Speed / BPM | Effects (count) | Card limits hit (wsconv warnings) |
|:--|--:|:--|:--|:--|:--|
| prosiak | 4 | Amiga | 2 / 125 | 0 1 2 3 4 8 9 A B C D E6 EB EE F | none |
| consul | 4 | Amiga | 3 / 125 | 1 2 A C F (C 1075, A 301) | none |
| faster2 | 8 | Amiga | 4 / 125 | 1 2 3 4 9 A C E1 F; volume-column panning (1946) | env on instruments 1, 6, 12; instrument 12 a ping-pong loop to the sample's end |
| discharz | 10 | Amiga | 3 / 125 | 0 1 2 3 (3038) 4 9 A C E1 E9 EA EB F; volume-column panning (5614) | 31 key-offs; env on 3, 11, 12; 50 206-byte track pack: two track chunks |
| anarchy | 32 | linear | 6 / 125 | EC (159) | linear table (the card's firmware uses Amiga periods); the 32-channel maximum |
| youwould | 8 | linear | 3 / 180 | F | linear table |
| test_xm | 2 | linear | 5 / 139 | 1 8; volume column (set, slide, panning) | instrument 1 has 3 samples (only sample 0 kept), env, a 16-byte sample with a 7-byte ping-pong loop, 1 key-off |
| frontend | 8 | (S3M) | 5 / 125 | A B D G (126) T; 21 note cuts | none |
| techrock | 16 | (S3M) | 3 / 60 | C S8 (panning); 37 note cuts | none |
| test_s3m | 4 (2 enabled) | (S3M) | 254 / 33 | every letter A-Z once (on a disabled channel) | stereo sample (left kept), AdLib instrument dropped, speed 254 |

### Reference data (`ref/*.json`)

Rendered WAVs are not stored (3.5 MB each). For each track, `ref/<track>.json` holds the render's SHA-256 and its
20 ms envelope: the render command and tool version, `onset_sample` (first sample of (L+R)/2 above 0.01 of full
scale), then per 882-sample window from the onset `rms_x10000` (RMS of (L+R)/2 times 10 000, rounded) and
`zero_crossings` (sign changes inside the window). `test_xm.json` has no windows: OpenMPT renders `test.xm` as
silence (its notes carry no instrument number), and so does the card.

```bash
openmpt123 --batch --force --samplerate 44100 --no-float --dither 0 --filter 2 --end-time 20 -q -o consul.wav src/consul.xm
```

openmpt123 0.8.9 (libopenmpt 0.8.9+r25652, Homebrew). `--dither 0` makes the render reproducible (the default
dither is random); two renders were byte-identical for every track.

## How the EXEs were built

From the repository root (`tools/machines/sprinter/wildsound/`, 2026-10-09):

```bash
W=testdata/sound/wildsound; T=tools/machines/sprinter/wildsound; B=scratch/wsbuild
build() {  # module stem subfolder
  python3 -I $T/wsconv.py $W/src/$1 -o $B --base $2
  python3 -I $T/wslink.py $B/$2 -o $W/$3/$2.exe --prosiak $W/prosiak.exe
}
build consul.xm consul xm
build faster-2.xm faster2 xm
build discharz.xm discharz xm
build test.xm test_xm xm
build npo_-_controlled_anarchy.xm anarchy xm
build miafan2010_-_you_would_be_here.xm youwould xm
build test.s3m test_s3m s3m
build kj_jose_-_a_new_frontend.s3m frontend s3m
build kc-techrockloop.s3m techrock s3m
```

The PT3 EXEs (the player assembles with sjasmplus 1.23: `$SJASMPLUS` or PATH):

```bash
P=tools/machines/sprinter/wildsound/wsay
python3 -I $P/wsaybuild.py $W/src/spring.pt3 -o $W/pt3/spring.exe
python3 -I $P/wsaybuild.py $W/src/summer.pt3 -o $W/pt3/summer.exe
python3 -I $P/wsaybuild.py $W/src/melomagic.pt3 -o $W/pt3/melo.exe
```

A rebuild gives byte-identical EXEs. Every `wslink` run ended with "verified: script, every record decoded and
unpacked in place (emulated), equal to the chunks". Card storage used (of 999 424 bytes): consul 175 391,
faster2 129 704, discharz 356 522, anarchy 24 390, youwould 26 179, test_xm 700, frontend 18 255, techrock 71 238,
test_s3m 876.

## The disk and how to run a track

`wildsound.ucompose.yaml` builds a 40 MiB MBR disk with one FAT16 partition from the folder's `*.exe` files
(`src/` and `ref/` left out): `D:\PROSIAK.EXE`, `D:\XM\*.EXE`, `D:\S3M\*.EXE`, `D:\PT3\*.EXE` (a PT3 EXE plays
until a key; its sound is the `ws_ay1` / `ws_ay2` rows). Put it on `ide0.slave` with the DSS
system disk on `ide0.master`:

```text
emulator_manage {"action":"create","model":"SPRINTER","sprinter_isa_slot1":"wildsound"}
media {"action":"insert","slot":"ide0.master","path":"<MAME pack>/IMG/sp_hdd_sys.chd"}
media {"action":"insert","slot":"ide0.slave","path":"<repo>/testdata/sound/wildsound/wildsound.ucompose.yaml"}
emulator_manage {"action":"reset"}
# about 1500 frames later Flex Navigator 1.15 is up; type on its command line, then Enter:
type_input {"action":"type","text":"d:\\xm\\consul.exe"}
```

The screen shows the banner, "XM Init ... OK", "Wild Sound ... ISA #0 165931.23042021", "XM TRK Data" (twice for
discharz), "XM INS HDR", "XM INS" and "XM PLAY"; the music is the `ws_xm` row of the mixer (capture it with
`/audio/capture` `{"source":"ws_xm"}`). The upload before the first note takes about 3 s of emulated time for
prosiak and consul, 2 s for faster2, 7 s for discharz and under 2 s for the small tracks.

The disk size matters: with a smaller volume (3.9 MB) the composer writes partition type `#04` (FAT16 below
32 MB), and DSS 1.71.57 answers "Invalid name" for `d:`; at 40 MiB the partition is type `#06` and drive D: works.

## Verification on the emulated Sprinter (2026-10-09)

Each EXE was run as above (unreal-qt from branch `wildsound-card`, BIOS 3.06 Hotfix 2, DSS 1.71.57) and the
`ws_xm` row captured in one 29 s capture that starts about 1 s before the music. The capture was compared with
the openmpt123 render over 20 s:

- **envelope**: Pearson correlation of the 20 ms RMS envelopes of (L+R)/2, both cut at their onsets;
- **zero crossings**: the same for the per-window zero-crossing rates (pitch and timbre);
- **lag**: the best offset of the second half's envelope within ±0.5 s (tempo drift; 0 = none);
- **aligned**: envelope / zero crossings again after aligning on the best offset within ±300 ms (19.7 s compared).

| Track | Envelope | Zero crossings | Lag | Aligned envelope / zero crossings | RMS emulated / reference |
|:--|--:|--:|--:|:--|:--|
| prosiak | 0.972 | 0.919 | 0 ms | 0.971 / 0.918 | 0.096 / 0.143 |
| consul | 0.994 | 0.988 | 0 ms | 0.994 / 0.989 | 0.089 / 0.133 |
| faster2 | 0.976 | 0.906 | 0 ms | 0.979 / 0.907 | 0.078 / 0.168 |
| discharz | 0.988 | 0.879 | 0 ms | 0.988 / 0.879 | 0.106 / 0.244 |
| anarchy | 0.998 | 0.000 (see below) | 0 ms | 0.996 / 0.000 | 0.170 / 0.513 |
| youwould | 0.869 | 0.580 | 180 ms (see below) | 0.933 / 0.718 (offset +9 ms) | 0.071 / 0.109 |
| test_xm | silent | silent | - | - | 0 / 0 |
| frontend | 0.994 | 0.975 | 0 ms | 0.993 / 0.976 | 0.063 / 0.094 |
| techrock | 0.966 | 0.931 | 0 ms | 0.975 / 0.946 | 0.074 / 0.114 |
| test_s3m | 0.005 | -0.072 | -500 ms | 0.006 / -0.056 | 0.036 / 0.015 |

Notes on the numbers:

- **Level**: the emulated card is quieter than OpenMPT by a factor that grows with the channel count (0.67 at 4
  channels, 0.46 at 8, 0.43 at 10, 0.33 at 32): the player's mixer scales by 0.5/√max(4, channels). Correlation
  is not affected.
- **anarchy**: both signals are a DC-offset pulse pattern that never crosses zero, so both zero-crossing series are
  constant and their correlation is undefined (reported as 0). The envelope matches (0.996).
- **youwould** (8 channels, BPM 180, only `Fxx` effects): the envelope matches chunk by chunk (0.77-1.00 per
  2-4 s chunk), with a slow drift of the emulated playback behind OpenMPT (+5 ms at 4-8 s, +15 ms at 16-20 s),
  which lowers the unaligned numbers. At BPM 180 a tick is 612.5 samples at 44.1 kHz; the emulated player keeps the
  fraction, OpenMPT's tick length differs from it by about 0.08 %, which is the drift measured (16 ms over 20 s).
  Not a player bug. (The card's own firmware rounds the tick to whole samples of its ~55.4 kHz clock: 770 samples.)
- **techrock** (BPM 60): the same tick rounding, +5 ms by 12-20 s.
- **test_xm**: silent on the card and in OpenMPT: the pattern's notes have no instrument number, so nothing is
  triggered. The EXE checks that a module with a multi-sample instrument, envelopes, a 16-byte ping-pong sample and
  a key-off converts, uploads and plays (the screen reaches "XM PLAY").
- **test_s3m** (below 0.85, explained): the OpenMPT unit-test module, not music. Its initial speed is 254 ticks
  per row at tempo 33: OpenMPT holds row 0 for 19.2 s (one low tone, then row 1 at 19.2 s). `wsconv` writes 254
  into the track header's `TMP`, and the emulated player clamps the initial speed to 1-31 (FT2's range,
  `WildSoundXmPlayer::Start`), so its rows lasted 31 ticks (2.35 s): row 1's high note (`B-7`) sounded at 2.3 s and
  row 2's note cut (`^^`, converted to volume 0) silenced the channel at 4.7 s. **Fixed after this run**: the
  player now takes the header's speed up to 255. Re-run on the fixed build: second by second the same pitch as
  OpenMPT (12-13 Hz until 19 s, then 3 099 Hz) and the row change at 19 s; the level is 4 x OpenMPT's (the S3M's
  master volume does not reach the card's format). The reference firmware takes `TMP`
  as it is (`TIK = TMP` per row, reference-firmware §D.1, `TMP` a u16), so a real card would hold the row for
  254 ticks like OpenMPT: the clamp is a difference from the firmware, met only with S3M speeds above 31. The
  module's effects all sit on channels the S3M disables (settings `#81`, `#82`), which the converter drops.

Per 2-4 s chunk (5 ms windows, onsets aligned) every other track keeps a constant 0 ms offset over the 20 s
(no tempo drift); the lowest chunk correlations are prosiak 12-20 s (0.76-0.81), discharz 0.3-2 s (0.83-0.86),
frontend 1-2 s (0.48) and techrock 4-12 s (0.80-0.87), where the overall envelope stays above 0.96. Envelopes and
volume-column panning that the card's format leaves out (faster2, discharz) cost little at this resolution, and
panning does not enter a mono comparison. Key-offs (discharz, 31): the emulated player cuts the note, as FT2 does
for an instrument without a volume envelope (`wsconv` warns that the card's firmware plays note 97 as a note; the
emulated player does not reproduce that).

### PT3 (the AY chips)

Run from an mtools FAT16 disk on `ide0.slave` with the same EXEs (2026-10-09), compared with the same player
assembled for the Pentagon's own AY (`#FFFD` / `#BFFD`, IM 2 pacing) playing the same module:

| Track | Row | Envelope | Lag | Pitch vs Pentagon |
|:--|:--|--:|--:|:--|
| spring (one chip), 5 s | `ws_ay1` | 0.956 | 0 ms | x 1.009 (median) |
| summer (TurboSound) chip 0, 30 s | `ws_ay1` | 0.978 | 0 ms | x 1.014 |
| summer chip 1, 9 s | `ws_ay2` | 0.914 | 0 ms | x 1.000 |
| melo (TurboSound) chip 1, 19 s | `ws_ay2` | 0.933 | 0 ms | x 1.000 |

The expected pitch ratio is 1.014: the card's AY clock is 1 774 400 Hz, the Pentagon's 1.75 MHz. Details:
[wsay README](../../../tools/machines/sprinter/wildsound/wsay/README.md).
