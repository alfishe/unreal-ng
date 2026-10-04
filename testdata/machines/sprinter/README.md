# Sprinter Sp2000 test fixtures

Disk-operating-system material for the PetersPlus Sprinter (Sp2000), collected
2026-10-01 for the unreal-ng Sprinter work (design:
[docs/inprogress/2026-09-28-sprinter/](../../../docs/inprogress/2026-09-28-sprinter/)).
Estex DSS (Disk Sub System) is the Sprinter's own DOS; it boots from a
standard PC-format FAT12 floppy.

| Path | Content |
|:--|:--|
| `dss_1_62_92.img` | the DSS 1.62.92 bootable 1.44 MB floppy, a raw PC sector dump |
| `dss160r/` | five files of the official DSS 1.60R binary release (February 2003) |
| `golden/setup-menu.png` | BIOS 3.04 SETUP 1.58 menu as `ScreenSprinter` renders it (736x288; reviewed by eye, phase S2): the golden image of `SprinterBoot_Test.Bios304_SetupSavesSettingToCmos`. The MAME captures are in `reference/` |
| `golden/dss-prompt.png` | DSS 1.62.92 at its `B:\>` prompt after the floppy boot, as `ScreenSprinter` renders it (736x288; reviewed by eye, phase S3a): the golden image of `SprinterBoot_Test.Dss162_BootsFromTheHdFloppyToThePrompt`, compared pixel for pixel |
| `golden/acctest.png` | the accelerator test `ACCTEST.EXE` (below) after it drew its picture, run from the DSS floppy with BIOS 3.04 (phase S5): the golden image of `SprinterBoot_Test.Dss162_AccTestCopiesItsPictureWithTheAccelerator`; its picture area equals MAME's (`reference/mame-acctest-306.png`) |
| `golden/fn-panels.png` | Flex Navigator 1.10 (the floppy's `fn`) with both panels on `B:\FN`, phase S5: the golden image of `SprinterBoot_Test.Dss162_FlexNavigatorDrawsWithTheAccelerator` |
| `software/acctest.exe` | the accelerator test program (below) |
| `dss/1.71.66/` | Estex DSS 1.71.66 built from source (community `master` `ae08ad9`): kernel, shell, installer, boot loader and symbols; see its README |

## `dss_1_62_92.img` - the DSS 1.62.92 boot floppy

- **Source:** <https://app.sprinter.ru/os/estex-dss162> (version 1.62.93,
  status "Release", published 12.01.2022). Download
  <https://app.sprinter.ru/downloads/os/estex-dss162/240>, served as
  `dss_1_62_92.zip` (879 492 bytes); the zip holds this one file, dated
  2021-03-24. Fetched 2026-10-01.
- **Not included:** the same page offers the 1.62.93 patch,
  <https://app.sprinter.ru/downloads/os/estex-dss162/293> (`dss_1_62_93.zip`,
  13 294 bytes: `system.dos` 16 035 bytes and `SYSTEM.EXE` 7 424 bytes, dated
  2021-04-21), to be copied over the floppy's files.
- **Size:** 1 474 560 bytes (80 cylinders x 2 sides x 18 sectors x 512 bytes,
  PC order: c0/h0, c0/h1, c1/h0, ...).
- **CRC32:** `8D61BE4E`
- **SHA-256:** `268623be380fea5ac800afad2a3c564600f3ae0d33e8e56ce961d23928fbcac2`

### Layout

Boot sector (LBA 0) BIOS Parameter Block:

| Field | Value |
|:--|:--|
| jump | `EB 3C 90` |
| OEM name | `DSS 1.60` |
| bytes per sector | 512 |
| sectors per cluster | 1 |
| reserved sectors | 10 (the boot sector + the 9-sector DSS loader) |
| FATs | **1** (a PC floppy has 2) |
| root directory entries | 224 (14 sectors) |
| total sectors | 2 880 |
| media descriptor | `F0` |
| sectors per FAT | 9 |
| sectors per track | 18 |
| heads | 2 |
| hidden sectors | 0 |
| extended BPB | signature `29`, serial `2D3A96C2`, label `NO NAME`, type `FAT12` |
| signature | `55 AA` at 510 |

- **LBA 1-9:** the DSS loader. LBA 1 starts with the text `Starting...`
  followed by Z80 code.
- **LBA 10-18:** the FAT. **LBA 19-32:** the root directory (cylinder 0,
  side 1, sectors 2-15). **LBA 33:** cluster 2, the start of the data area.

Root directory entries (in order):

| Name | Kind | First cluster | Size |
|:--|:--|--:|--:|
| `SYSTEM.DOS` | file (the DSS kernel) | 2 | 16 035 |
| `SYSTEM.EXE` | file (the command shell) | 34 | 7 424 |
| `SYSTEM.BAT` | file | 49 | 94 |
| `INSTALL.BAT` | file | 50 | 624 |
| `BIN` | directory | 52 | |
| `DEMOS` | directory | 582 | |
| `DOCS` | directory | 1035 | |
| `FM` | directory | 1108 | |
| `FN` | directory | 1217 | |
| `KEYBOARD` | directory | 1328 | |
| `ZX` | directory | 1354 | |
| `ESTEX.ZIP` | file | 1785 | 336 804 |
| `INSTALL.TXT` | file | 1072 | 3 083 |
| `GAMES` | directory | 1282 | |
| `PICS` | directory | 1316 | |

The tracks are high density (500 kbit/s): the WD1793 reads them only with the
Sprinter's port `#BD` set to HD (2 MHz clock). At the default 250 kbit/s every
READ SECTOR ends in Record Not Found.

### Used by

`core/tests/loaders/disk/loader_rawpc_test.cpp`, fixture
`LoaderRawPcFloppyDss_Test`:

- `Wd1793ReadsTheRealBootSectorAndLoaderAt500Kbps` - the BPB above, the OEM
  name and `Starting...` at LBA 1, read through the WD1793;
- `Wd1793ReadsTheRealRootDirectoryAt500Kbps` - locates the root directory from
  the BPB and finds `SYSTEM.DOS` and `SYSTEM.EXE`;
- `Wd1793FindsNoRecordOnTheRealImageAt250Kbps` - Record Not Found at DD.

The tests skip when the image is missing.

## `dss160r/` - DSS 1.60R release files

- **Source:** the `release/` folder of <https://gitlab.com/sprinter-computer/dos>
  at commit `c7f0de8` (2023-02-03), local copy
  `emulators/gitlab/sprinter-computer-dos`. DSS 1.60R was the last official
  binary release by Peters Plus Ltd (February 2003).
- **Taken:** the files a DSS boot reads, plus the release notes. The release
  has no `SYSTEM.BAT`; `SYSTEMX.BAT` is its template (the floppy above ships its
  own `SYSTEM.BAT`). Not taken: the installer and upgrade tools (`INSTALL.*`,
  `UPGRADE*.*`, `SYSCOPY*.BAT`, `DSSSETUP.MNU`) and the other `CMD/*.EXE`
  utilities.
- The files are byte-exact copies (`RELNOTES.TXT` and `SYSTEMX.BAT` keep their
  CRLF line endings; `dss160r/.gitattributes` stops Git from normalizing them).

| File | Size | CRC32 | SHA-256 | What |
|:--|--:|:--|:--|:--|
| `SYSTEM.DOS` | 16 364 | `9870E0B3` | `949babed967e846d4f75de13cf88c008a8927d62dbbab1191cb9b14dbf988c58` | the DSS kernel |
| `SYSTEM.EXE` | 6 969 | `244853DB` | `92de2010410a6403badbbbd91dacdeabf48253b98de48c4d19dfedcf10e3828a` | the command shell |
| `SYSTEMX.BAT` | 21 | `F5E47D43` | `24feb9ffab8c1ab2299a95963e90bedda9752ed319d5556adc101c208fb4fe07` | startup batch template: `SET PATH=c:\cmd;c:\` |
| `CMD/BOOT.EXE` | 2 453 | `E43963B7` | `5425c1f83e9269c29d631c8d249e2de18616ead7113acf2b51792a0cd2447de1` | writes the DSS boot sector and loader to a disk |
| `RELNOTES.TXT` | 3 192 | `8E140D6C` | `cf8f9a937b3901f3a6dadd999ec98481b0f6143db61678dc4a532011011a34e9` | the release notes (English) |

Not used by a test yet; they are the input for building a DSS 1.60R boot disk
once the Sprinter machine runs.

## `software/acctest.exe` - the accelerator test

- **Source:** `TESTS/ACCTEST.EXE` of the HDD system disk in the owner's MAME pack
  (`mame_release_v306_25.05.2025/IMG/sp_hdd_sys.chd`, raw image `sp_hdd_sys.img`, FAT16 at LBA 63; the
  same folder holds the accelerator description `DOCS/ACCEL_R.TXT`). Author and date unknown (the
  disk's files are all dated 2019-01-01).
- **Size:** 4 632 bytes. **CRC32:** `BCE4E9A4`. **SHA-256:**
  `9c5f77e8b299aa423171b2b83fe8a7e5f1cd58e8c4649dcb36b54f19c8866466`.
- **What it does:** a DSS `EXE` (code from offset `#16`, loaded at `#8100`): sets the 320 x 256 mode
  and a palette (from `#8200`), puts graphics page `#50` into window 3 and copies the 64 x 64 picture at
  `#8300` (offset `#216`) into rows 0-63 with the accelerator (`LD D,D : LD A,#40`, `LD L,L : LD A,(HL)`,
  `LD (DE),A`, `LD B,B` per row), then waits for ESC.
- **Used by:** `SprinterBoot_Test.Dss162_AccTestCopiesItsPictureWithTheAccelerator` (copied with a new
  `SYSTEM.BAT` onto a scratch copy of `dss_1_62_92.img`), and
  [.recipe/machines/sprinter-accelerator.md](../../../.recipe/machines/sprinter-accelerator.md).

## License

- **DSS 1.60R** (`dss160r/`): the repository README states the source code was
  released to the community in 2009, "we believe it was done under PUBLIC
  DOMAIN terms".
- **DSS 1.62.92 floppy:** no license stated on the download page; used as test
  material only (see [testdata/NOTICE.md](../../NOTICE.md)).
- **ACCTEST.EXE** (`software/`): no license stated; used as test material only (same notice).
