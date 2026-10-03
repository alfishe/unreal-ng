# Profi CP/M disk images

CP/M floppies for the two Profi boards, collected 2026-10-03. Per-image details (source, hashes,
contents, how to boot) are in the README of each board folder:

| Folder | Board | Images |
|:--|:--|:--|
| [`v5/`](v5/README.md) | Profi v5 (Kondor boards 5.0x, BIOS 1.x / 2.x by Micco) | the Kondor "Copy K" system disk, two user system disks |
| [`v3/`](v3/README.md) | Profi v3.2 (Kramis BIOS V0.2 / V0.3) | Klug CP/M 2.3 |

## The Profi CP/M floppy format

All images here use the native Profi CP/M format: 5.25" (or 3.5" DD), 80 cylinders (Klug: 82), two
sides, 5 sectors of 1024 bytes per track (800 KB), logical track = cylinder * 2 + side, sectors
R = 1..5 in order. On cylinder 0 / side 0 the fifth sector carries ID **R = 9**: it is the boot
sector. CP/M uses 2 KB blocks with 16-bit block numbers and a 4 KB (128-entry) directory. Micco /
Kondor system disks have no reserved tracks (the directory starts at cylinder 0 / side 0, sector 1, and
the boot sector is the first half of block 2, the file `BOOTK.COM`); Klug CP/M reserves the first two
cylinders for its system (directory at cylinder 2 / side 0).

## How the BIOS ROMs boot a disk, and why the images are split by board

Both BIOS families read sector R=9 of cylinder 0 / side 0 and start the code through the 16-bit
word at offset `#102` of that sector:

- **BIOS 1.0 / 2.0 (v5, "Загрузка системы CP/M")**: reads the whole 1024-byte sector to `#5D25`,
  then `LD SP,#5E27 : RET` (BIOS 2.0 `PB20-36F5F7BD` at `#0872` / `#1723`; BIOS 1.0 `PB10-10DA289A` at
  `#061B` / `#17A7`).
- **Kramis V0.2 / V0.3 (v3)**: reads the sector to `#9000`, copies only the first **288 bytes** to
  `#5D25` (`LD HL,#9000 : LD DE,#5D25 : LD BC,#0120 : LDIR`), then `LD HL,(#5E27) : JP (HL)`
  (V0.2 `PJV02-77327F52` at `#1417`, V0.3 at the same code `#1708`).

So a disk whose start word points inside the first 288 bytes boots on both families; one whose
word points further in only boots from a v5 BIOS. The Micco / Kondor boot sectors start at `#5FB7`
(`CPM.UDI`) or `#5FC4` (all other v5 images): offset `#292` / `#29F`, outside the Kramis copy, so they
are **v5 images**. Klug CP/M starts at `#5D25` (offset 0) and boots from either.

The Kramis menu entry for the disk boot: the V0.2 menu offers "Sinclair 128", "Profi-DOS",
"Sinclair", "TR-DOS" and "TEST" (the ROM survey in the Profi analysis materials, `profi-roms.md`,
kept outside the repository); "Profi-DOS" is the CP/M entry (the v3 front-panel switch is labeled
"ON/OFF SP-DOS", SP-DOS being Sinclair Profi MicroDOS). That this entry runs the routine above was
read from the ROM, not run in the emulator.

## `../CPM.UDI` (the older image next to this folder)

`testdata/machines/profi/CPM.UDI` is the vtrd.in release "Profi CP/M (for Profi) by Micco
Software'92", byte for byte:

- **Source:** <https://vtrd.in/system/CPMPROFI.zip> (360 571 bytes, holds `CPM.UDI` dated
  2002-03-24), listed on <https://vtrd.in/system.php> under operating systems. Fetched 2026-10-03.
- **CRC32:** `CA8291F1`, **SHA-256:** `ee31e8c5690b3ccb155b8b92786993707dd3d50b21ddee57b943a6a6843e1199`
- **Board:** v5 (boot word `#5FB7`, see above; it also carries `SYSPRO5.BIN`, a v5 BIOS image, and
  the text "Profi+v5.03"). It belongs in `cpm/v5/`; it stays where it is because tests reference it.
- **Format:** UDI, 80 x 2, 5 x 1024 B (cylinder 0 / side 0: R = 1-4, 9).
- **Contents:** the Micco "Concurrent BIOS" system (user 15: `BDOS.BIN`, `BIOS.BIN`, `DOSBIOS1.DRV`,
  `DSKKE6.DRV`, `EDKP.DRV`, `KBDK1.DRV`, `DSPK.DRV`, `LSTP.DRV`, `TIMER.DRV`), `BOOTK.COM`, and a
  user's collection of ROM images (`SYSPRO5.BIN`, `TRDOS-0x.BIN`, `TURBO-90.BIN`, test ROMs, sync PROM
  dumps `SAMX6.BIN` / `SAMX12.BIN`), `PGM.COM`, `WRDEMO.COM`, `DSPINST.COM`, `EDSKINST.COM`.
  `AUTOEXEC.BAT` holds only comment lines. It is a user's disk, not a factory system disk.
- **CONFIG.SYS:** `DOSBIOS1 / DSKKE6 / EDKP / KBDK1 / DSPK STD / LSTP KOI8 / TIMER`. All drivers and
  `STD.FNT` are on the disk; there is no file named `KOI8.*`.

### About `LSTP KOI8` and the HALT at `#828F`

On the emulated v5 this disk's loader stops at its failure trap (HALT with DI at `#828F`, error 3 "file not
found"). Traced on the emulator: the loader reads the directory, finds and parses `CONFIG.SYS`, loads and starts each
driver; this disk's `LSTP.DRV` copies its parameter from the command tail at `#0080` into its own file-name buffer
(`#414B`, default `STD     FNT` at `#4140`) and calls the loader's file search (`CALL #8003`) for `KOI8    FNT`,
which is not on the disk. The newer Kondor / Micco disks in [`v5/`](v5/README.md) also say `LSTP KOI8` but boot
to `A>` on the emulator: their printer driver does not load a font file. So this image is an inconsistent user
disk: replacing `LSTP KOI8` with `LSTP STD` (and fixing the sector and file CRCs) boots it to the Concurrent BIOS
`A>` prompt on the emulator.

