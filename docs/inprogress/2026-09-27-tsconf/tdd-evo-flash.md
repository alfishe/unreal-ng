# ZX-Evo flash ROM writes (TS-Conf and ATM3)

**Created:** 2026-10-05 · **Status:** implemented (TS-Conf audit gap G2) with the saved flash
([section 6](#6-saving-the-flash-owner-decision-2026-10-06)), not committed

The ZX-Evo's ROM is a flash chip. Both firmwares let the Z80 write it, which is how TS-BIOS and BaseConf images are
updated from the machine itself. unreal-ng dropped every ROM write (the TS-Conf RTL audit, gap G2 in
[../2026-10-05-tsconf-rtl-audit/memory-ports.md](../2026-10-05-tsconf-rtl-audit/memory-ports.md)). This note says how
the board does it and how unreal-ng models it.

## 1. The board

| What | Value | Source |
|:--|:--|:--|
| Chip | 29F040 (512 K x 8 NOR flash), PLCC32, D3 | pentevo `pcad/rev_c/zxevo.sch`, `pcad/rev_d/zxevo.sch` (part `29F040-K`); `pcad/rev_c/TODO.txt` (rev C: "D3 replaced with 29F040-K, PLCC32 without a socket") |
| Datasheets kept with the board | AMD Am29F040B, ST M29F040B | pentevo `docs/Chips/am29f040b.pdf` (publication 21445 rev E), `docs/Chips/m29f040b.pdf` (April 2002) |
| IDs (autoselect) | AMD: manufacturer `#01`, device `#A4`; ST: `#20`, `#E2` | Am29F040B table 3 / 4 (page 10, 14); M29F040B table 5 (page 7) |
| Sectors | 8 x 64 KB, A18..A16 select the sector | Am29F040B "Sector Address Tables" (page 9) |
| Reset pin | none (PLCC32: A18-A0, DQ7-0, /CE, /OE, /WE): the command state survives a Z80 reset | Am29F040B connection diagram |

The flashers know more chips: NedoOS `evoflash.com` (DimkaM, 2023) recognizes `#E220` ST M29F040, `#A401` AMD
Am29F040B, `#D7BF` SST39LF/VF010 and `#B7BF` SST39SF040 (its table at `#12C0`). The SST parts erase 4 KB sectors;
unreal-ng models the board's 29F040.

### 1.1 Commands (Am29F040B table 4)

Unlock and command cycles compare A10..A0 only (note 4), so `555` / `2AA` work at any page, and the `5555` / `2AAA`
of the SST parts match too.

| Command | Bus cycles (address / data) |
|:--|:--|
| Reset | `XXX / F0` |
| Autoselect | `555/AA 2AA/55 555/90`, then reads: `X00` manufacturer, `X01` device, `SA X02` sector protection (0) |
| Byte program | `555/AA 2AA/55 555/A0 PA/PD` (bits only go 1 -> 0) |
| Chip erase | `555/AA 2AA/55 555/80 555/AA 2AA/55 555/10` |
| Sector erase | `555/AA 2AA/55 555/80 555/AA 2AA/55 SA/30`, further `SA/30` within 50 us |
| Erase suspend / resume | `XXX/B0`, `XXX/30` (not modeled: no ZX-Evo flasher uses them) |

### 1.2 Status while it works (Am29F040B table 5, pages 15-18)

| Bit | Program | Erase |
|:--|:--|:--|
| DQ7 (Data# polling) | complement of the programmed bit 7 | 0 |
| DQ6 (toggle bit) | toggles on every read | toggles |
| DQ5 | 1 when the operation failed (a 1 programmed over a 0) | same |
| DQ3 | - | 0 inside the 50 us command window, 1 once erasing |

### 1.3 Times (Am29F040B page 29, M29F040B table 6)

| Operation | Am29F040B typ / max | M29F040B typ / max | unreal-ng |
|:--|:--|:--|:--|
| Byte program | 7 / 300 us | 8 / 150 us | 10 us |
| Sector erase | 1 / 8 s | 0.6 / 4 s | 1 s |
| Chip erase | 8 / 64 s | 5 / 20 s | 8 s |
| Sector-erase command window | 50 us | 50 us | 50 us |

## 2. How the FPGA drives the chip

The flash's A13..A0 are the Z80's, A18..A14 are the window's ROM page (`rompg[4:0]`), /OE is the read strobe, /CE is
"this window shows ROM" (`csrom`).

### 2.1 TS-Conf (`fpga/current/z80/zmem.v`)

```verilog
wire w0_we    = memconf[1];                          // :67  MEM_CONFIG bit 1
wire rom_n_ram = win0 && !w0_ram && !vdos;           // :81  ROM only in window 0
assign csrom   = rom_n_ram;                          // :295
assign romoe_n = !memrd;                             // :296
assign romwe_n = !(memwr && w0_we);                  // :297
assign rompg   = xtpage[0][4:0];                     // :298 the page window 0 shows
```

A write reaches the chip when MEM_CONFIG.W0_WE is set and window 0 shows ROM (not W0_RAM, not vdos), in normal or
mapped mode (mapped: page `{PAGE0[7:2], ~DOS, ROM128}`). `top.v:424-428` puts `rompg` on the pins (`rompg0_n`,
`dos_n`, `rompg2..4`); a board-level inversion of a page bit would only renumber pages, which the image layout already
reflects. TS-BIOS's own tooling and the Wild Commander ROM writer (`soft/WC/source/plugins/rom_writer/PLUG04.ASM`)
use MEM_CONFIG `#06` (normal, ROM, W0_WE) to write and `#0E` (RAM in window 0) after.

### 2.2 BaseConf / ATM3 (`fpga/base`)

```verilog
// z80/zports.v:841-857  port #BF bit 1
romrw_en_reg <= din[1];
// z80/zmem.v:186-196
rompg[4:0] = page[4:0];                                              // the accessed window's page
assign romwe_n = wr_n | mreq_n | (~romrw_en) | wrdisable;            // :193
assign csrom   = romnram;                                            // :196 the accessed window shows ROM
// mem/atm_pager.v:141,186  wrdisable = the window's #xBF7 bit; 0 with the pager off
```

Any window that shows ROM takes writes while `#BF` bit 1 is set and its `#xBF7` write protection is off. NedoOS
`evoflash.com` writes `#BF` = 3 (shadow ports + write enable), switches window 3 between ROM pages 1 and 0 for the
unlock cycles at `#D555` / `#EAAA` (flash `5555` / `2AAA`) and programs through window 1; `#BF` = 1 after.

## 3. Design

| Part | Where | What |
|:--|:--|:--|
| The chip | `core/src/emulator/io/flash/flash29f040b.{h,cpp}` (shared with NeoGS) | Command state machine, status bits, busy times; works on an external array (`bindArray`) or its own |
| The board glue | `core/src/emulator/memory/atm/evoflash.{h,cpp}` (`EvoFlash`) | Host bus overlay: CPU writes to the write windows go to the chip, reads from ROM windows return the chip's status while it is not in read-array mode; time base; TTD region |
| TS-Conf | `TsConfMemory::UpdateModelBanks` | write window = `0x01` when W0_WE and window 0 shows ROM |
| ATM3 | `PortDecoder_ATM3::SyncFlashWindows` (end of `updateMemoryBanks`) | write windows = ROM windows without `#xBF7` protection, when `#BF` bit 1 |
| TTD | `core/src/debugger/ttd/atm/ttdevoflash.{h,cpp}` | blob `EvoFlash` (PeripheralId 61), region `EvoFlash` (18) |
| Saved flash | `EvoFlash` persistence; `core/src/emulator/memory/atm/evoflashrequest.{h,cpp}` (automation) | the per-machine file ([section 6](#6-saving-the-flash-owner-decision-2026-10-06)) |

**The array is the ROM.** The chip works on `Memory::ROMBase()` (ROM pages 0-31, the 512 KB `zxevo.rom` /
`zxevo-fe.rom` image). A programmed byte is what the CPU reads from that page afterwards; nothing is copied. A window
showing a ROM page beyond 31 (a 1 MB ATM3 image) does not reach the chip.

**Cost.** `EvoFlash` is a host bus overlay installed only while a window takes writes or the chip is not in
read-array mode (`EvoFlash::Sync`). With W0_WE / `#BF` bit 1 off and the chip idle the CPU's memory path does not see
it; the decoders call `SetWriteWindows` when they map banks (port writes), which returns at once when nothing changed.
Other machines have no `EvoFlash`. While installed, a read costs one test (`arrayMode`) and a write one mask test.
A/B: [section 5](#5-verification).

**Status reads.** While programming, erasing, in autoselect, or after a failure, every CPU read from a ROM window
returns the chip's output (status or ID), also with the write enable off (/OE alone). The overlay stays installed for
that and is removed at the next bank mapping or frame end after the chip returns to read-array mode
(`PortDecoder_*::OnFrameEnd`).

**Time.** The chip counts base clock t-states (`EvoAvrWait::BaseNow`: `t_states` + the frame's t-state at the base
clock), 3.5 M per second. A Z80 reset restarts `t_states` at 0, so the decoders' `reset()` completes a running
operation at once (`EvoFlash::OnMachineReset`). The chip has no reset pin, so its other state (autoselect, a failed
operation) survives the reset, as on the board: a machine reset in autoselect boots into ID bytes until a power cycle
or `F0`.

**ID.** The AMD Am29F040B (`#01` / `#A4`), the first datasheet in the pentevo tree. ST's `#20` / `#E2` is a one-line
change (`EvoFlash::kVendor`); no setting.

## 4. TTD

| Item | Stream | Notes |
|:--|:--|:--|
| Command state (mode, toggle, DQ7 source, sectors, pending program, window end, busy end) | blob `EvoFlash`, id 61, 33 bytes (`ttd.ksy`) | v1 and engine; registered on TSL and ATM3 |
| The 512 KB array (ROM pages 0-31) | engine region `EvoFlash`, id 18, `evo.flash`; pieces marked by the chip's programs and erases | v1 keeps no ROM (as for the EEPROMs: `ttdbench` `V1Lacks`) |
| The overlay | derived: `EvoFlash::LoadState` and every bank mapping run `Sync` | |

Every outside input is already recorded: the writes are CPU bus cycles, the time is the machine's. The engine's first
capture of a session stores the region whole (512 KB, the ROM); after that only pieces the chip changed. Debugger
edits of ROM bytes do not mark pieces (as before: ROM edits are not part of a recording).

The ROM signature a session carries (`TimeTravelController::ComputeRomSignature`, hashed over the ROM region) is taken
when recording starts. A replay source set up after a flash write hashes the changed ROM and can report "another ROM";
the region restore itself is exact. Noted, not changed.

## 5. Verification

Tests (`core-tests`):

| Test | What |
|:--|:--|
| `TsConfMemory_Test.MEM6_RomWriteEnableReachesTheFlash` | the audit's test: program sequence with MEM_CONFIG `#06` changes the byte, with W0_WE = 0 it does not |
| `EvoFlashTsConf_Test.*` | array = ROM; overlay life cycle; DQ7 / DQ6 polling then data; DQ5 failure; autoselect IDs; sector erase of four pages with DQ7 / DQ3; mapped mode page; blob round trip mid-erase; TTD registration; reset during an operation; **Wild Commander ROM writer** routines (PLUG04) erase and program |
| `EvoFlashAtm3_Test.*` | `#BF` bit 1 and `#xBF7` gate the windows; program through window 3; **NedoOS evoflash.com** routines (command helper `#09BC`, page writer `#09DC`) read the ID `#A401`, erase sector 1, program a 16 KB page |
| `TTDEvoFlash_Test.SeeksRestoreTheFlashBytesAndTheChipState` | a TS-Conf program programs a byte and starts an erase while the controller records; every checkpoint restores the bytes and the chip state; seeking back undoes the write |
| `TTDModelStateContract_Test` | id 61 in the table and in `ttd.ksy` |

Real flashers for a manual check: Wild Commander's `ROM_PROG.WMF` (WC 1.11, opens a `.ROM` file of 64 or 512 KB) on
TS-Conf; NedoOS `evoflash.com zxevo.rom` on the ATM3 (NedoOS release, `bin/evoflash.com`, doc `doc/evoflash.txt`);
the ERS's own "update ROM" (pentevo `rom/mainmenu/src/flasher.a80`) and the TR-DOS `flash_pe` (pentevo
`z80_soft/flash_pe/`, reads `ZXEVO.ROM` from the SD card) on the ATM3. The recipe
([.recipe/machines/tsconf.md](../../../.recipe/machines/tsconf.md#flashing-the-rom-mem_configw0_we)) was checked on a
running `unreal-qt` through the WebAPI.

A/B (performance guidelines §4): A = `80f11e046` (master), B = this change, both with the new `BM_HostFrame_ATM3_*`
benchmark; rounds A B A B A B B A B A; 1-minute load fell from 41 to 13 during the run (above the guideline's 12, so
indicative). CPU time per host frame, minimum per side and the mean of the paired differences:

| Frame | A min (us) | B min (us) | B vs A (min) | Pairs (B - A) / A | Mean |
|:--|--:|--:|--:|:--|--:|
| TSConf fast | 1818.0 | 1842.7 | +1.4 % | +0.5 +2.2 -1.8 +1.2 +0.9 | +0.6 % |
| TSConf debug | 1877.3 | 1870.2 | -0.4 % | +0.4 +2.5 -0.2 +1.5 -2.6 | +0.3 % |
| ATM3 fast | 1939.4 | 1949.0 | +0.5 % | +0.6 +0.5 -1.1 +1.2 -0.8 | +0.1 % |
| ATM3 debug | 2097.8 | 2078.8 | -0.9 % | +0.3 -0.9 -1.1 -2.1 -2.8 | -1.3 % |
| Pentagon fast (control, code unchanged) | 1514.1 | 1521.0 | +0.5 % | +0.4 +0.7 -0.4 +2.6 +2.4 | +1.1 % |

No sign holds across the pairs; the untouched Pentagon moves as much as the changed machines: within noise, as
expected for a change outside the per-access path.

Mutation check: with `EvoFlash::onWrite` returning at once (the old behavior, writes dropped) 13 of the 16 tests above
fail (all but the registration and the overlay-gate checks).

Fixtures: the new blob is part of every TS-Conf and ATM3 recording, so `testdata/machines/tsconf/ttd/sprites.ttd`
(`TTD_Corpus_Test`: "device sets differ", 21 vs 20) and `testdata/ttd/engine/tsconf_sprites.ttd`
(`TimeTravelControllerCorpus_Test`) need re-recording. Expected-value updates: `testdata/slots/fitted-devices.txt`
(atm3, ts-conf: `61:32`), `testdata/ttd/bench/v1-ci-gate.txt` (ATM3/idle blob and file rows),
`TTDTsConfState_Test.TTD1` (7 ids).

## 6. Saving the flash (owner decision 2026-10-06)

The options were: keep the flash in memory only, write it back to the ROM file, or save it to a file of its own
per machine. The owner chose the last: **a flashed board stays flashed across restarts, and the shipped image is
never written** (TS-Conf and ATM3 both read `zxevo.rom`).

| What | How |
|:--|:--|
| File | `zxevo-flash-<machine>-<SHA-256>.rom`, machine `tsconf` or `atm3`, the hash of the ROM image as loaded from its file (its bytes up to 512 KB) |
| Folder | the settings folder, `FileHelper::GetWritablePath()`: where NeoGS keeps its reprogrammed flash (`neogs-flash-<SHA-256>.rom`, `[NGS] FlashWrite=persist`), named the same way |
| Content | the whole 512 KB array, as the chip holds it |
| Loaded | when the ROM loader puts an image into the ROM pages (`ROM::LoadROM` -> `PortDecoder::OnRomLoaded` -> `EvoFlash::OnRomImageLoaded`): machine creation and a ROM reload. The file named by this image's hash replaces the array; a file of another size is ignored with a warning |
| Another ROM image | its hash names another file: a new `zxevo.rom` never takes an old flash. Files of the same machine with another hash are reported (log warning at load, `other_image_files` in the status) and not used |
| Written | after a program / erase completes, once the chip has been quiet for 50 frames (about a second, `kSaveQuietFrames`), on request, and when the machine goes away (`PortDecoder::BeforeRelease`, the start of `Core::Release`, while memory still exists). Written to `<file>.tmp` and renamed over the file |
| Never written | while a TTD replay owns the machine (`ttdReplayActive`): the debounce, the request and the machine's end all skip it. Inside a session TTD is the source of truth; a TTD restore does not mark the flash unsaved |
| Discard | deletes the file; nothing is saved until the chip changes again; asks for a ROM reload, so the shipped image is back after the next reset |
| Tests | core-tests switch persistence off in `main()` (`EvoFlash::SetPersistenceAllowed(false)`) and put the settings folder in the process's scratch folder, so a test that flashes never changes the ROM another test boots; `EvoFlashPersist_Test` turns it on in a folder of its own |

Why the whole image and not a diff: 512 KB is small, the file is then a ROM image any tool or a real board's
flasher (`evoflash.com`, WC's ROM writer) can take, and loading it needs no base: a diff would have to be applied to
exactly the image it was made from, which the hash in the name guarantees anyway, and would gain only disk space.

Automation (the same core calls, `evoflashrequest.h`; save and discard run on the machine's thread):

| Surface | Status | Save now | Discard |
|:--|:--|:--|:--|
| WebAPI | `GET /api/v1/emulator/{id}/memory/rom/flash` | `POST .../memory/rom/flash` `{"action":"save"}` | `{"action":"discard"}` |
| CLI | `romflash` / `romflash status` | `romflash save` | `romflash discard` |
| MCP | `emulator_manage` `rom_flash_status` | `rom_flash_save` | `rom_flash_discard` |
| Lua / Python | `rom_flash_state()` | `rom_flash_save()` | `rom_flash_discard()` |

unreal-qt has no menu entry: neither the NeoGS flash save nor the CMOS has one, and the machine's storage actions
live in automation.

Tests: `EvoFlashPersist_Test.AFlashedByteSurvivesTheMachineAndDiscardUndoesIt` (flash, close, a new machine has the
byte, discard, the next machine has the shipped byte), `AnotherRomImageNeverTakesTheFile` (another hash and a
truncated file are not used; ATM3 and TS-Conf files are separate), `SavedAfterAQuietSecond`,
`NothingIsWrittenWhileAReplayOwnsTheMachine`, `OffMeansNoFileAndOtherMachinesHaveNone`.
