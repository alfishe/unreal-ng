# ZX-Poly test corpus

Every publicly available piece of ZX-Poly content, collected 2026-09-28 for
the unreal-ng ZX-Poly work (design:
[docs/inprogress/2026-09-27-zxpoly/](../../../docs/inprogress/2026-09-27-zxpoly/)).
All of it comes from the zxpoly repository; a search of releases, the full git
history, zx-pk.ru and the web found nothing published elsewhere (§ How the
corpus was searched).

| Folder | Content | Count |
|:--|:--|:--|
| `zxp/` | ZX-Poly snapshots: 6 adapted games + 1 format fixture | 7 |
| `trd/` | bootable TR-DOS adaptations with a ZX-Poly multiloader (After The War 2, ZX-Word) | 2 |
| `loaders/` | the sources those TRDs are built from: `multiloader.asm`, the `zxpoly.mac` macros, the four per-CPU plane files, the original file; plus the reference loader library `asmloader/` (`zxpoly.i`, disk and tape loaders) | 2 projects + library |
| `sze/` | ZX-Poly Sprite Corrector projects: original data + edit mask + four planes, one per adaptation, including **After The War 1**, which exists nowhere else | 9 |
| `originals/` | the pre-adaptation Flying Shark snapshot and the code pokes two adaptations need | 3 |
| `rom/` | the ZX-Poly Test ROM (`zxpolytest.prom`) and its source (`testrom-src/`) | 1 + source |

## What `.zxp` is

The native snapshot format of the ZX-Poly Java emulator
([raydac/zxpoly](https://github.com/raydac/zxpoly)). It is written by the
ZX-Poly Sprite Corrector when an adaptation is exported. It holds the full
state of all four CPU modules of the quad-Z80 ZX-Poly machine.

Layout: big-endian, grammar
`zxpoly-emul/src/jbbp/snapshots/zxp/com.igormaznitsa.zxpoly.formats.ZXPParser.jbbp`.

| Offset | Size | Field |
|:--|:--|:--|
| 0 | 4 | magic `0xC0BA0100` |
| 4 | 4 | flags (0 in every file) |
| 8 | 1 | `#3D00` platform port: D0 nWAIT, D1 local reset, D2–D4 video mode, D5–D6 mapped CPU, D7 lock |
| 9 | 1 | `#FE` (border) |
| 10 | 4 × 5 | per module: `#7FFD`, R0, R1, R2, R3 |
| 30 | 11 × 4 × 2 | per module: AF, AF', BC, BC', DE, DE', HL, HL', IX, IY, IR |
| 118 | 4 + 4 + 4 | per module: IM, IFF1, IFF2 |
| 130 | 4 × 2 + 4 × 2 | per module: PC, SP |
| 146 | var. | per module: page count N, then N × (page index, 16384 bytes) |

A module stores only the RAM pages it needs:

- 48K games store pages 0, 2 and 5 (`#C000`, `#8000`, `#4000`), giving
  196 770 bytes;
- 128K games store all 8 pages, giving 524 470 bytes.

## `zxp/` — snapshots

All files come from the zxpoly repository, `master` at commit
[`794c254`](https://github.com/raydac/zxpoly/commit/794c2541ae80f12f12cc6e18b544c1536401c05e)
(2026-09-27). "Added" is the commit that first brought the file into the
repository, from its history.

| File | Game (year) | Adapted by | Mode | `#3D00` | Pages/module | Source path in zxpoly | Added | SHA-256 |
|:--|:--|:--|:--|:--|:--|:--|:--|:--|
| `Alien8.zxp` | Alien 8 (1985) | Igor Maznitsa, 2021 ("in one evening") | 4 (256×192, 16 colours) | `#91` | 0, 2, 5 | `adapted/Alien8/` | `53dc0cc` 2021-07-25 | `c8ef2645…2f0d34` |
| `buratino_adventures.zxp` | Adventures of Buratino (1993) | Igor Maznitsa, 2018 | 5 (512×384) | `#95` | all 8 | `adapted/BuratinoAdventures/` | `9f788c6` 2018-11-19 | `1fd02ef0…3c8435` |
| `ComandoQuatro.zxp` | Comando Quatro (1989) | the game's original programmer, [@jotarp](https://github.com/jotarp), 2024 | 4 | `#91` | 0, 2, 5 | `adapted/ComandoQuatro/` | `150c335` 2024-05-02 | `c564c5a1…5d216dd4` |
| `flyshark.zxp` | Flying Shark (1987) | Igor Maznitsa, 2019 | 7 (FLASH selects ZX-Poly vs classic cells) | `#9D` | all 8 | `adapted/FlyShark/` (was `flyshark_mode7.zxp` until `cd619eb`) | `d100112` 2019-11-14 | `9e3875c1…f11aacd` |
| `OFCZXPOLY.zxp` | [Official Father Christmas](http://www.worldofspectrum.org/infoseekid.cgi?id=0003493) (1989) | Igor Maznitsa, 2017 | 6 (mode 4 + INK==PAPER flood) | `#99` | 0, 2, 5 | `adapted/OfficialFatherChristmas/` (was `Official Father Christmas.zxp` until `c31135e`) | `70f213d` 2017-12-04 | `47926b6c…eebde62` |
| `SummerSanta2022.zxp` | [Summer Santa 2022 Update](https://spectrumcomputing.co.uk/entry/39005/ZX-Spectrum/Summer_Santa_2022_Update) (2022, Paul Jenkinson) | Igor Maznitsa, 2024 | 4 | `#91` | all 8 | `adapted/SummerSanta2022/` | `bd5fd5a` 2024-12-03 | `024e2b1b…5bebe7bb` |
| `fh.zxp` | (format test fixture) | zxpoly test suite | 4 | `#91` | 0, 2, 5 | `zxpoly-emul/src/test/resources/com/igormaznitsa/zxpoly/formats/` | `4b5bb58` 2017-08-14 | `9be119c1…8425f93` |

Full SHA-256 values:

```
c8ef26455d6316516c0a73cc9ed618d160096e915ff96c7254ea78e99c2f0d34  Alien8.zxp
1fd02ef07c3ab929ad47633b33b62862df9f1bfecd851e8f574cd897113c8435  buratino_adventures.zxp
c564c5a17cefe9269bc96ecfae43d5c5f0ff86b15d81df20806542ac5d216dd4  ComandoQuatro.zxp
9e3875c19ce0ad596886ecc6e0a66f5e23346f3eb015f646c842a8079f11aacd  flyshark.zxp
47926b6c3b807b8d92ec7959dbc39f93762111e93d2c8d0b6b5f482eeaebde62  OFCZXPOLY.zxp
024e2b1b2619c8ed1e1591e0287ae09025f8564fd549b895c31b84aa5bebe7bb  SummerSanta2022.zxp
9be119c177f0e59a4c1fc8432b4271c4e8ebf8fb0c9b9e1efde4917368425f93  fh.zxp
```

Per-game notes from the zxpoly README:

- **Official Father Christmas:** some level-3 elements could not be
  colorized, because colouring them desynchronized the CPUs (the level checks
  for empty screen areas).
- **Buratino:** the mirrored hero sprite could not take two colours.
- **Flying Shark:** combines classic ZX colouring for the panels with ZX-Poly
  colouring for the play field. It also needed three code pokes, which
  `adapted/FlyShark/poke.txt` documents.

## What the files contain: same state, different graphics

The files were parsed and compared module by module. The numbers show how
little a ZX-Poly adaptation actually changes.

- **Registers:** identical in all four modules in every file (PC, SP, all
  pairs, IM, IFF). The four CPUs execute the same instructions.
- **Platform state:** every file is saved locked, with slaves running and
  mapped CPU 0 (`#3D00` bit 7 = 1, bit 0 = 1, D5–D6 = 0). A file therefore
  starts directly in the steady state; no loader phase is involved.
- **Module registers:**
  - R0 = `#00` for module 0, and `#12`/`#14`/`#16` for modules 1–3 (IO
    writes disabled, heap windows at 128K/256K/384K);
  - R1–R3 = 0;
  - `#7FFD` identical in all modules.
- **Memory:** modules differ only in graphics data. Bytes that differ from
  module 0, over the pages each file stores:

| File | CPU1 | CPU2 | CPU3 | Of CPU1's, in the screen area |
|:--|--:|--:|--:|--:|
| Alien8 | 6 116 (12.4%) | 3 261 (6.6%) | 3 685 (7.5%) | 3 732 |
| Buratino | 3 667 (2.8%) | 3 489 (2.7%) | 3 524 (2.7%) | 1 284 |
| ComandoQuatro | 9 688 (19.7%) | 7 953 (16.2%) | 10 983 (22.3%) | 3 505 |
| FlyShark | 2 596 (2.0%) | 4 037 (3.1%) | 4 269 (3.3%) | 0 |
| OFC | 4 453 (9.1%) | 6 074 (12.4%) | 5 371 (10.9%) | 1 362 |
| SummerSanta2022 | 7 553 (5.8%) | 3 670 (2.8%) | 6 900 (5.3%) | 521 |
| fh | 0 | 8 | 8 | 0 |

The differing bytes form a small number of contiguous regions per page.
These are the game's sprite and tile tables (its graphics "atlases") plus
the screen at the moment the snapshot was taken. The ranges below combine
all three slave modules; addresses are Z80 addresses, using the files'
paging (page 5 = `#4000`, page 2 = `#8000`, page 0 = `#C000`):

| File | Screen bitmap | Graphics data regions (atlases) |
|:--|:--|:--|
| Alien8 | `#4001–#579E` (8 ranges) | `#6308–#7FFE` (2 ranges); `#8000–#A628` (22 ranges, 8.5 KB span) |
| Buratino | `#40BF–#57FF` (16 ranges) | `#61F8–#7FFF` (10 ranges); `#8005–#9C55` (23 ranges) |
| ComandoQuatro | `#4000–#57FF` (1 range) | `#5F3E–#7A79` (6 ranges); `#8528–#92EE` (4); `#E9A0–#F86F` (2) |
| FlyShark | none | `#8000–#97FF` (17 ranges); `#CA2B–#F495` (67 ranges, 8 KB span) |
| OFC | `#4021–#57DC` (53 ranges) | `#A4BC–#BFFF` (23 ranges); `#C001–#F999` (22 ranges) |
| SummerSanta2022 | `#408A–#577A` (45 ranges) | `#7A09–#7CFF` (1 range); `#907D–#BFFC` (44 ranges, 10.6 KB span); `#C020–#FFFE` (22 ranges) |
| fh | none | `#9F22–#9F31` (16 bytes) |

**Attributes never differ.** In every file, `#5800–#5AFF` is identical
across all four modules. Only the bitmap and the sprite/tile data differ,
which fits the video modes:

- mode 4 ignores attributes;
- modes 6 and 7 take them from module 0;
- mode 5 (Buratino) would allow per-module attributes, but the adaptation
  does not use that.

Consequence for storage (see
[quad-instance-architecture.md](../../../docs/inprogress/2026-09-27-zxpoly/quad-instance-architecture.md)
§4.4): a `.zxp` is really **one base snapshot plus per-module graphics
atlases**. The four full copies are redundant, and the ranges above are the
atlas map, extracted automatically. The importer turns a `.zxp` into:

- a base snapshot (module 0);
- per-module overlays for exactly these regions;
- the `#3D00` state.

## `trd/` — bootable adaptations with a multiloader

These are the "shippable" form of an adaptation. A normal TR-DOS disk boots
on CPU0 with the slaves held in WAIT. The multiloader loads one plane file per
CPU, streams planes 1–3 into the slave CPUs through the `#3D00` IO window
(`COPY2CPU`), sets each CPU's post-reset `JP` command, and locks the machine
with `SETPOLYMAIN` (`#93` = mode 4, `#97` = mode 5).

This is the input for the **loader path** in
[quad-instance-architecture.md](../../../docs/inprogress/2026-09-27-zxpoly/quad-instance-architecture.md)
§4.3 (acceptance test T12).

| File | Program | Mode | Disk catalog | Load / start | Source |
|:--|:--|:--|:--|:--|:--|
| `atw2.trd` | After The War 2, "partly adapted: just colorized some sprites" | 4 | `ploader.C` (1064 B at 18176), `boot.B`, `a1 0.C` … `a1 3.C` (33 970 B each, loaded at 24500 = `#5FB4`) | start `#BEA0` (48800) | `adapted/Atw2/target/`, built by `make.sh` |
| `zxword.trd` | ZX-Word text editor, "just improved font for 512×384" | 5 | `ploader.C` (1059 B at 18176), `boot.B`, `tzxun260.C` … `tzxun263.C` (16 384 B each), `ZXW2.6mh.W` (11 173 B) | start `#9801` (38913) | `adapted/ZxWord/target/` |

SHA-256:

```
a1457d6ae6eb68a4d8f3f745ed56ca0f25f20b1f2d5a03bdb30d5624414f01e7  atw2.trd
c07dde39e4b199e829899e038609e38acd12a40c8dc7f1ea7333c3be02152276  zxword.trd
```

## `loaders/` — how the TRDs are built

| Path | Content |
|:--|:--|
| `atw2/`, `zxword/` | `multiloader.asm` (the ZX-Poly boot logic), `zxpoly.mac`/`trdos.mac`/`other.mac` (macros), `make.sh` (sjasmplus + DOSBox `ZCOP.EXE` TRD copier; the tool binaries are not copied), `planes/` = the four per-CPU HOBETA plane files (`*C0.$C` … `*C3.$C`), `src/` = the original unadapted file, `others/` = extra disk files, `README.MD` from the project |
| `asmloader/` | the reference ZX-Poly loader library from `AsmLoader/`: `zxpoly.i` (macro API v1.02: `SETVIDEOMODE`, `SETRESCOMMAND`, `COPY2CPU`, `SETPOLYMAIN`, …), `zxpoly.m`, `diskload.asm`, `tapload.asm` |

The plane files are the atlases in their rawest form: four copies of the same
file that differ only in graphics bytes. Diffing `planes/*C1..C3` against
`*C0` gives the slot map directly, as for `.zxp`.

Known defects in `zxpoly.i` (found while reviewing the source):

- `SETWAIT13` toggles `#3D00` D1 (reset) instead of D0 (nWAIT);
- `SETSTOPADDR` references an undefined `addr`;
- `COPY2CPU` cleanup reads an uninitialized `(IX+0)` and never restores the
  target's R1.

The shipped loaders do not depend on them.

## `sze/` — Sprite Corrector projects

The editor's session format holds everything needed to regenerate an
adaptation:

- the original bytes;
- a per-byte "edited" mask;
- the four planes.

Layout (big-endian, `ZXPolyData.getAsArray`):

- magic `0xABBAFAFABABE0123`;
- a 4-char import plugin ID;
- an info block;
- data length N;
- then N bytes of original data, N bytes of mask, and four planes of N bytes
  each;
- a short tail.

| File | Import plugin | N | Planes 1–3 differ from plane 0 (bytes) | Matching `.zxp` |
|:--|:--|--:|:--|:--|
| `Alien8.sze` | `Z80S` (snapshot) | 49 152 | 6 116 / 3 261 / 3 685 | `Alien8.zxp` (identical counts) |
| `atw1.sze` | `Z80S` | 49 152 | 3 691 / 2 362 / 2 287 | **none**: After The War (part 1), never exported. Most likely the project's early After The War test colorization (see the history in [zxpoly-platform.md](../../../docs/inprogress/2026-09-27-zxpoly/zxpoly-platform.md) §1). Exporting it gives an eighth playable title |
| `atw2256x192.sze` | `LSZE` (one file from a disk container) | 33 970 | 1 260 / 1 501 / 1 395 | `trd/atw2.trd` (planes = `loaders/atw2/planes/`) |
| `buratino_adventures.sze` | `Z80S` | 131 072 | 3 667 / 3 489 / 3 524 | `buratino_adventures.zxp` |
| `ComandoQuatro.sze` | `Z80S` | 49 152 | 9 688 / 7 953 / 10 983 | `ComandoQuatro.zxp` |
| `flyshark.sze` | `Z80S` | 131 072 | 2 604 / 4 037 / 4 269 | `flyshark.zxp` |
| `OFCZXPOLY.sze` | `Z80S` | 49 152 | 4 453 / 6 074 / 5 371 | `OFCZXPOLY.zxp` |
| `SummerSanta2022.sze` | `Z80S` | 131 072 | 7 553 / 3 670 / 6 900 | `SummerSanta2022.zxp` |
| `zxword512x384.sze` | `LSZE` | 16 384 | 849 / 420 / 921 | `trd/zxword.trd` (planes = `loaders/zxword/planes/`) |

Sources: `adapted/*/*.sze`, `adapted/Atw2/src/`, `adapted/ZxWord/src/`,
`zxpolyeditions/atw1.sze`.

A `.sze` is the best **atlas** fixture:

- the original data is stored next to the four planes, so the "base + plane
  overlays" package of QI §4.4 can be derived and checked against the `.zxp`;
- the mask records which bytes the author actually touched.

## `originals/`

| File | What | Source |
|:--|:--|:--|
| `fshark_flash.z80` | Flying Shark snapshot prepared for mode 7 (FLASH set on the play-field cells); the base the `.zxp` was made from | `adapted/FlyShark/fshark_flash.z80` |
| `flyshark-poke.txt` | the three code pokes Flying Shark needs (`LD A,(40025)` → `LD A,134`/`NOP`: the game reads an attribute and fills the screen with it) | `adapted/FlyShark/poke.txt` |
| `ComandoQuatro.pok` | pokes shipped with Comando Quatro | `adapted/ComandoQuatro/ComandoQuatro.pok` |

## `rom/` — the ZX-Poly Test ROM

`zxpolytest.prom` (13 467 bytes) is the platform self-test and demo, which the
Java emulator boots by default. It needs the **coupled** machine
(QI §13: module-index reads, local reset with command injection, IO-window
reads and writes, RAM0 at `#0000`), so it is outside v1 scope. It is kept as
the fixture for that deferred work.

`testrom-src/` is its source (`zxpolytest.asm` v1.02, `strings.asm`, ZX0
depacker, font, and the 256/512 PNG demo images), built by `build`.
The `zxsc.jar`, `zx0` and `zasm` tool binaries are not copied.

Source: `zxpoly-emul/src/main/resources/com/igormaznitsa/zxpoly/rom/`, `TestROM/`.

## How the corpus was searched

- **The zxpoly repository at `794c254`:** every `*.zxp` file (7 files, all
  included).
- **Full zxpoly git history** (`git log --all -- '*.zxp'`): no deleted
  `.zxp` files. The only changes are two renames (`flyshark_mode7.zxp` →
  `flyshark.zxp`, `Official Father Christmas.zxp` → `OFCZXPOLY.zxp`) and
  in-place updates, and the current versions are the latest.
- **zxpoly GitHub releases 2.3.0–2.4.1-SNAPSHOT:** the assets are emulator and
  Sprite Corrector builds only. The emulator jar bundles only
  `zxpolytest.prom`.
- **Full zxpoly git history for all media** (`.trd`, `.scl`, `.sze`, `.z80`,
  `.sna`, `.tap`, `.prom`, HOBETA): nothing deleted except an old
  `opense.rom` (a standard ROM, 2015) and the rename of `zxpolytest.rom` →
  `.prom`.
- **Web search** (English and Russian): no ZX-Poly content published outside
  the zxpoly repository.
- **zx-pk.ru thread** ["Игры под ZX-Poly"](https://zx-pk.ru/threads/28517-igry-pod-zx-poly.html)
  (3 pages, 2017–2019): the author's posts about OFC, Buratino, ZX-Word and
  Flying Shark, all linking to the repository; no attachments.
- **[speccy.info/ZX-Poly](https://speccy.info/ZX-Poly):** project history
  only, no downloads. The site blocks automated fetching; the text was read
  in a browser.
- **Not copied** (not ZX-Poly content): the zxpoly TZX tape test fixtures
  (`zxpoly-emul/src/test/resources/tzx/`, ordinary games) and the three
  Spec256 archives (`…/snapshots/*_spec256.zip`), which belong to the
  Spec256 work ([2026-09-27-spec256](../../../docs/inprogress/2026-09-27-spec256/)).

## Licence

The zxpoly project, including everything in this folder, is published under
GNU GPL v3 (`LICENSE-zxpoly`, copied from the zxpoly repository), the same
licence as unreal-ng. The original games remain the property of their
respective authors and publishers; the adaptations are distributed by the
zxpoly project as part of its repository.
