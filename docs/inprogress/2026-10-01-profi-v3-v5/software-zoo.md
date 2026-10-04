# The Profi firmware and software zoo

**Date:** 2026-10-04 · part of [README.md](README.md) · open items in [TODO.md](TODO.md)

The Profi had several boards (v3.2, v4, v5.0x, v5.06, "Profi+"), several BIOS families from different authors, and
operating systems that each expect a particular BIOS and port decode. Not every pair works together, also on real
hardware. This page collects what we found by running each one on the emulator and reading its code, so a failure
can be told apart from an emulation bug. Every item says how it was checked.

Disk images and ROMs named here that are not in the repository are kept in the local collection
(`~/Downloads/zx-spectrum/profi/`, each folder with a README of sources and checksums).

## 1. The BIOS families

| BIOS | Board | Author | Where | Notes |
|:--|:--|:--|:--|:--|
| Kramis V0.2 (10.1990) + TR-DOS 5.03 | v3 | JV "KRAMIS" | `data/rom/profi/kramis-v02.rom` | its TR-DOS 5.03 cannot load Klug CP/M (section 3) |
| Kramis V0.3 + TR-DOS 5.04T | v3 | "Computer Profi" | `data/rom/profi/kramis-v03.rom` | the `PROFI3` default since 2026-10-03 |
| ROM Bios 1.0 / 2.0 + TR-DOS 5.04T / 6.08 | v5 | Micco Software | `data/rom/profi.rom` (2.0, the `PROFI` default), `data/rom/profi/bios*.rom` | the VG93 at `#1F..#7F` from the SYS ROM |
| Bios 2.1-2.3 (2013-2021) | v5, Karabas | community | collection `system-rom/later-v5/` | IDE boot (`#28CE`), RAM disk, Fatall |
| ROM BIOS Plus 0.32 (1998-2013) | "PROFI Plus" | Vadim (Star Software) | collection `system-rom/later-v5/bios-plus-032-vadim.rom` | needs `[PROFI] ExtPorts=sys` (section 5) |
| Award Modular BIOS 4.50PG | v5 | P.C.C.C. | collection | CMOS setup, HDD |

The ROM layout is the same for all: four 16K pages, SYS (the BIOS), TR-DOS, 128, 48 ([roms.md](roms.md)).

## 2. How a BIOS boots a disk

Both families read sector R = 9 of cylinder 0 / side 0 and start the code through the word at offset `#102` of
that sector ([testdata/machines/profi/cpm/README.md](../../../testdata/machines/profi/cpm/README.md)):

- **BIOS 1.0 / 2.0 (v5, "Загрузка системы CP/M")** read the whole 1024-byte sector to `#5D25`, then
  `LD SP,#5E27 : RET`.
- **Kramis V0.2 / V0.3 (v3, "Profi-DOS")** read it to `#9000` and copy only the first **288 bytes** to `#5D25`,
  then `LD HL,(#5E27) : JP (HL)`.

So a boot sector whose start word points into the first 288 bytes boots from both families; one that points
further in boots only from a v5 BIOS:

| System | Boot word | Boots from |
|:--|:--|:--|
| SP-DOS (MicroDOS, BIOS by V. Tereschenko) | `#5D25` | both (`SpDosBootsToItsShell` on `PROFI` and `PROFI3`) |
| Klug CP/M 2.3 | `#5D25` | both, with a TR-DOS that can read it (section 3) |
| Micco / Kondor "Concurrent BIOS" CP/M | `#5FC4` (`#5FB7` on `CPM.UDI`) | v5 only (`CpmBootsFromTheKondorSystemDisk`) |
| PQ-DOS (FAT12, 9 x 512-byte sectors) | `#5E60` | see section 6 |

## 3. Klug CP/M 2.3 needs TR-DOS 5.04T or later

Klug CP/M's boot sector reads the system tracks through the TR-DOS ROM's sector routines. TR-DOS 5.03 (Kramis
V0.2) switches to double stepping on this 5 x 1024-byte disk: every read lands on a doubled cylinder (Record Not
Found), and the half-loaded system crashes. The same happens with V0.2 on a v5 board. Kramis V0.3 (TR-DOS 5.04T) and
BIOS 2.0 (TR-DOS 6.08) boot it. Checked by an FDC trace on the emulator: the VG93 follows TR-DOS's own seeks and
track-register writes, so this is the software, not the board. This is why `PROFI3` defaults to Kramis V0.3.

The same Klug disk is filed by KLUG's BBS as "CP/M 2.2 for Sinclair 128" (byte-identical): on a Pentagon 128
without its RAM/ROM switch it stops at "Can't turn off ROM".

## 4. SP-DOS reads the disk in hi-res, polling `#BF`

SP-DOS's loader sets `#DFFD = #B0` (hi-res, CP/M, RAM at `#0000`), copies itself to `#0000`, and reads the system
with the VG93 at `#1F..#7F`, polling DRQ / INTRQ on the CP/M system port `#BF` and taking the bytes with `INI`.
On a v5 in hi-res the CPU runs at 5 MHz. The emulator's VG93 used to count time in CPU T, so its clock stepped
back at every frame boundary when the CPU ran faster than 3.5 MHz, and every read ended in Lost Data: SP-DOS hung
at "Загрузка системы CP/M...". Fixed: the VG93 and the tape keep a 3.5 MHz time base on the Profi
(`SetBaseClockTimeBase`, [design-hires.md](design-hires.md) section 4). The v5 test fails without the fix.

## 5. Two port decodes: the 5.0 PROM and Karabas Pro (`[PROFI] ExtPorts`)

The v5 has an **extended port map**: VG93 at `#83/#A3/#C3/#E3`, system port `#3F`, 8255 at `#87..#E7`, IDE
`#xxCB/#xxEB`, COM `#8F..`, RTC `#BF/#DF` ([decoder-prom.md](decoder-prom.md)). When it is on the bus differs:

| Variant | The extended map is decoded when | Who expects it |
|:--|:--|:--|
| `ExtPorts=cpm` (default) | CP/M (`#DFFD` bit 5) and ROM14 (`#7FFD` bit 4) | the 5.0 board's decoder PROM (transcribed from two manuals); BIOS 1.0 / 2.0 |
| `ExtPorts=sys` | also while the SYS ROM runs: DOS latch on and ROM14 = 0 | Karabas Pro (`karabas-pro.vhd`: `(cpm and rom14) or (dos_act and not rom14)`); ROM BIOS Plus, PQ-DOS |

They exclude each other in the SYS ROM state: Karabas decodes no VG93 at `#1F..#7F` there, while BIOS 1.0 / 2.0
boot disks through exactly those ports from the SYS ROM. With `ExtPorts=sys` BIOS 2.0 reaches its menu and its
tests, but its CP/M and SP-DOS disk boots fail (checked: `CpmBootsFromTheKondorSystemDisk` and
`SpDosBootsToItsShell` fail with it). What the 5.06 / Profi+ periphery CPLD decodes is not known (no source); the
BIOS Plus title "Personal Computer PROFI Plus" suggests those boards follow Karabas. The v3 has no extended map at
all (its PROM has no ROM14 input).

The `#DFFD` write decode differs between boards too: `[PROFI] DffdDecode=emulators|v50|v506`
([research-profi-v5-open-items.md](research-profi-v5-open-items.md)).

**ROM BIOS Plus on `PROFI`** (0.32, checked 2026-10-04 with the port trace) **and on `PROFI-PLUS`** (0.41h1 with
the 8255, 8253 and 8251 emulated, 2026-10-04: `ProfiPlusBoot_Test.BoardTestReportsEveryDeviceOk`): its "Test
results controller's board"

| Device | 0.32, `ExtPorts=cpm` | 0.32, `ExtPorts=sys` | `PROFI-PLUS` (0.41h1) | Why |
|:--|:--|:--|:--|:--|
| Floppy Disc Controller, FDD0 / FDD1 | Fail | Ok (80 cylinders) | Ok | probes `IN #83`, `IN #C3` |
| RTC | Fail | Ok | Ok | `#BF` / `#DF` |
| Sound Chip | Ok (YM2149F) | Ok | Ok | `#FFFD` / `#BFFD` (a test needs `SoundCardScope`, section 10) |
| Parallel interface | Fail | Fail | Ok | the 8255 on `#87..#E7`: mode `#90`, PC2 set / reset read back, B = 2 read back |
| Serial interface | Fail | Fail | Ok | the 8253 (`#EF` control, counter 1 mode 3 loaded with `#0010` and read back through `#AF`, counter 0 the baud divider at `#8F`) and the 8251 (reset 4 x `#01`, `#40`; mode, command; status at `#F3` not `#FF`); then `OUT #B3,1` |
| HDD0 / HDD1 | Fail | Fail | Fail without an image | no image attached |

BIOS Plus keeps the result in `(IY + 2)` with IY = `#4000` (0.41h1 SYS page `#06A5..#06E4`), one bit per device that
failed: bit 0 FDC, 1 parallel, 2 and 3 serial, 4 RTC, 5 / 6 the sound chip (AY / YM: one of them clears), 7 the hard
disk; the screen prints the serial interface "Ok" when bits 1 and 2 are clear. With the COM port taken out it reads
`#AC` (serial and HDD fail).

## 6. PQ-DOS

PQ-DOS (Vadim / Star Software, from about 1997, on the MSX-DOS 1 sources) is almost compatible with MicroDOS, CP/M
and MSX-DOS 1/2, uses FAT12/16 with subdirectories and boots from a floppy or an IDE disk; without a suitable ROM
BIOS it loads its own "ROM-BIOS emulator" ([AC News #63](https://zxpress.ru/en/ezines/acnews/63/pq-dos-operating-system-for-profi-computer-with-hard-drive-support-cp-m-and-ms-dos-compatibility)).
Checked with the disk `pqdos1.fdi` (PQ-DOS 2.1, collection `profi/dos/pq-dos/`):

| Machine | Result |
|:--|:--|
| `PROFI` + ROM BIOS Plus 0.32 + `ExtPorts=sys` | boots: "PQ-DOS Startup Menu", then `A:\>`. Its default menu entry wants `C:\DOS\CMD.COM` on a hard disk; DOS Navigator 2.0.16 asks for "BIOS version 0.40 or higher" |
| `PROFI3` (Kramis V0.3, "Profi-DOS") | "ROM-BIOS emulator installed [OK]", "PQ-DOS Loading...", reads the system, programs the 8255 (`OUT #7F,#90`), then loops at `#52B0..#52C1` (not investigated) |
| `PROFI` (BIOS 2.0, CP/M entry) | the boot returns to the BIOS menu |

Compatibility with Profi CP/M (2026-10-04, [analysis](../../disasm/machines/profi-plus/pqdos-hdd-programs/README.md)):
of the 116 programs on the PQ-DOS 2023 HDD image most start; FLINES, WERT#, PINGVIN# fail because BDOS function 98
(parse filename) is not implemented, MAT prints nothing because BDOS function 9 stops at a NUL byte; JAZZY and COLUMNS
are open. Run such programs from a Micco CP/M floppy.

## 7. Disks that fail by themselves

- `testdata/machines/profi/CPM.UDI` (vtrd.in "Profi CP/M by Micco Software'92", a user's disk): its `CONFIG.SYS`
  loads `LSTP KOI8`, but the disk has no `KOI8.FNT`, so the loader stops at its error trap (`#828F`, code 3, "file
  not found"). A copy with the line removed boots to `A>`.
- Klug CP/M on Kramis V0.2: section 3.

## 8. Firmware we only have in a defective dump

The PROFI-XT keyboard controller (8035) firmware v1.27 exists only as the dump `9A8E2686`, which cannot work: no
`EN I`, and its main loop never fetches a key. KLUG's BBS (2005) had the same bytes, so the defect is old. The
emulator runs a reconstruction with 5 bytes replaced (`data/rom/profixt/README.md`); a clean re-dump would settle it.

## 9. Switches that change what boots

- v5 front-panel **CP/M** switch (`[PROFI] CpmSwitch`): holds `#DFFD` at `#00`; at power-on the BIOS then starts
  Spectrum 128 (`CpmSwitchAtPowerOnStartsSpectrum128`).
- v3 front-panel **"ON/OFF SP-DOS"**: the v3.2 manual says it disables the extra modes, like the v5 CP/M switch; not
  modeled on the v3 yet.

## 10. A trap in the test build

`core-tests` fits no AY, GS or MoonSound unless a test asks for it (`SoundCardScope`, `core/tests/_helpers/`). A
BIOS or program run in a test without `SoundCardScope(TestSound::TurboSound)` sees no sound chip (ROM BIOS Plus
then reports "Sound Chip: Fail"). The `DISABLED_RunProgram` probe in `profi_boot_test.cpp` asks for the AY.
