# MGT +D: TODO

Status: **P0-P4 done** (P4.5 2026-11-18), landed on master 4531969d5 (2026-11-18): the card, the autostart, the
`48K-PLUSD` / `128K-PLUSD` machines in the Sinclair family of the Qt and handheld menus, the recipe
`.recipe/machines/plusd.md`. 2026-11-19: P5.2 (the `basic` param on every plane, the MCP `inspect_state fdc` text
for the card). P5.1, P5.4 (overlay only while paged in, card FDCs stepped behind a sleep flag), P5.5 done, landed
e4399ec70 (2026-11-19); G+DOS writes (P4.6) 9761657f2. Build checks are left to the GitHub CI / Release runs.
Deferred: P0.6 (the PAL dump), P7 (DISCiPLE).

## Phases

### P0: data
- [x] P0.1 ROMs stay `data/rom/gdos-pd.rom` (+D G+DOS v1a) and `data/rom/gdos.rom` (DISCiPLE GDOS v3); hashes in `data/rom/README-ROMS.md`, equal to MAME
- [x] P0.2 UNI-DOS v2 / G+DOS v1 by CRC32 in Data/zx-spectrum (roms, emulators), Projects/emulators, ~/Projects/GitHub: none (2026-11-15); not needed for the work
- [x] P0.3 WD177X-00 data sheet in `docs/WD1793/datasheeets/WD177X-00/` (step 6 / 12 / 2 / 3 ms, settle 15 ms); H-6 stays open for the board's chip
- [x] P0.4 System disk (D-2): `data/boot/plusd/gdos-2a-system.mgt`, written by `PlusDSystemDisk_Test.ConfigWritesTheSystemDisk`
  (System Tape v2a `CONFIG` on `48K-PLUSD`, every answer its default, `Y` to confirm the FORMAT, `N` to "repeat"; tape
  named by `UNREAL_PLUSD_SYSTEM_TAPE`, skipped without it). Two runs give the same file; recipe and hashes in
  `data/boot/plusd/README.md`. Against S-DISK: the `+SYS 2a` directory entry and its 14 sectors are identical (body
  sha256 `ba1582991bed6c7c`), so no configuration byte differs; the disk adds CONFIG's own four files.
  `PlusDSystemDisk_Test.CatListsDisk`: `RUN` loads `+SYS` (banner `G+DOS2a`), `CAT 1` lists the five files (1 s)
- [x] P0.5 Not needed: `plusdautostart_test.cpp` writes its work disks itself (`GdosDisk`: the system disk plus a
  BASIC `BORDER n` loop, or CODE only), one per plan. Was: fixture work disks into `testdata/machines/plusd/`; a
  128K-snapshot disk is written by the emulator (`SAVE` of a type 9 file via the snapshot button)
- [ ] P0.6 PAL equations of `alice_pal20l8.ic4` if the dump can be found: settles H-1, H-2, H-3, H-5

### P1: WD1772 variant and two controllers
- [x] P1.1 `WdVariant`, constructor with ports / drives from the caller; register-level `ReadRegister` / `WriteRegister`.
  The Beta constructor keeps `coreState.diskDrives[]` and history `beta128`; a card passes its drives and a history id.
  `selectDriveLine(-1)` selects an internal empty drive (no disk, no index); `setSide()` is the card's side latch
- [x] P1.2 1772 rules: step rates, `h` = spin-up disable, motor / spin-up, no READY, status bits 7 / 5, no side compare,
  `E` / `V` settle 15 ms, force interrupt I2 / I3. The FSM keeps the WD1793 bit meanings; `Status1772()` presents
  bit 7 = MO, Type I bit 5 = spin-up done, and a Type II / III that ended not ready (no disk) as RNF. Deviation: with
  no disk the real chip waits for index pulses for ever; the emulation ends the spin-up after 6 revolutions' time.
  Spin-up state `_spinUpDoneAt` is in the +D card's TTD blob (P3.3); `_variant` is configuration, not state
- [x] P1.3 Controller list `EmulatorContext::cardFdcs` (Add / RemoveCardFdc), stepped in `MainLoop::OnCPUStep` /
  `OnFrameEnd` and checked in the Z80 inert test. The other `pBetaDisk` uses (SZX, TTD, MachineStateTransfer,
  DiskFastLoad, DiskAutostart, devicestate, TR-DOS analyzer, Profi / Sprinter decoders) are Beta-only and stay
- [x] P1.4 `FloppyDriveSlot(context, CardDrive{fdd, id, label, tags, aliases})`; the drive carries its slot id
  (`FDD::setSlotId`), the controller reports writes by slot id (`SlotIdOfSelectedDrive`), `BeforeImageGrow` too.
  Found on the way (pre-existing, not +D): every Beta FDD has `_driveID` 0 (no setter), so `BeforeImageGrow` of drives
  B-D goes to `fdd.a` and `NC_FDD_DISK_INSERTED/EJECTED` name drive 0
- [x] P1.5 Tests: `wd1772_test.cpp` (11), `FloppyDriveSlot_Test.CardDriveHasItsOwnSlot`; FDC / FDD / slot / media / TTD /
  autostart / state transfer filters: 586 passed, 5 skipped (2026-11-16). Full suite at the end of the work

### P2: the card
- [x] P2.1 `Function::DiskMgt` (`disk.mgt`), `CardDef plusd` (edge bus, needs `/RDROM /M1 /NMI`, claims `00FF` on
  `E3 EB F3 FB EF E7 F7`, media `fdd.plusd1/2`), sources Fuse `plusd.c` / MAME `mgt.cpp`, `CardType` without TTD types
  (recording refused naming the slot until P3.3, now type `PlusD`)
- [x] P2.2 `slots/cards/plusd/plusdcard.*`: overlay `#0000-#3FFF` (ROM `rom/gdos-pd.rom`, RAM writable), M1 hook with
  `machineM1HookEnd = #0300` and traps `0008 / 003A / 0066 / 028E`, `#E7` IN pages in (bus left alone) / OUT pages
  out, `#EF` latch (bit 0 drive 1, bit 1 drive 2, none = no drive, bit 7 side), `#F7` reads `#FF` (no printer, P3.2).
  /RESET pages in, clears the latch, resets the WD1772, keeps the RAM. A missing ROM or a taken M1 hook refuses the
  card with the reason. `WD1793::PeekRegister` added for `ICard::Peek`
- [x] P2.3 Variants `48K-PLUSD`, `128K-PLUSD` (aliases `48K-PLUS-D`, `128K-PLUS-D`) in `machinevariants.cpp`:
  `edge.1 = plusd` on top of the machine's slots, the 48K's Beta 128 off. No separate INI configs: the variant is the
  preset (as `48K-BETA128`)
- [x] P2.4 `state fdc` (every plane through the existing report): a machine without a Beta 128 reports the +D's WD1772
  (`controller` "WD1772 (MGT +D)", `slot`, `paged`, `control_register`, drives by `media_slot`, 1772 status bits
  `motor_on` / `spin_up_done`); with a Beta 128 the card is listed in `card_controllers`. Bank names in
  `inspect_state`: open, moves to P5.2
- [x] P2.5 `plusdcard_test.cpp` (12): fit on 48K, absent card, FDC ports, latch, overlay ROM / RAM, M1 traps, reset,
  G+DOS boots 48K / 128K to BASIC and pages out after its RAM set-up (first interrupt: `#02A2` clears the 8K, ~3
  frames, `'X'` at `#3DE4`), variants, preset, `state fdc`. Slots matrix regenerated: 48K / 128K / +2 fit, +2A / +3
  `A (needs /RDROM)`, Next bus fight `#E3` / `#EB` with its DivMMC. `CatListsDisk` needs the system file `+SYS`
  loaded from disk: moved to P0.4
### P3: NMI, printer, snapshots, TTD
- [x] P3.1 Snapshot button = NMI. No card-action verb exists in the slot framework; the NMI / magic button every
  plane already has (`RequestMNI`, Qt action) reaches the +D through its `#0066` M1 trap, so no `plusd.snapshot`
  action was added (deviation from design §3.2). `SnapshotButton48K` (long tier): NMI, `4` saves "Snap F" (type 5,
  97 sectors, one sector per turn, ~20 s emulated), the program goes on paged out; after NEW `LOAD d1"Snap F"S` puts
  it back
- [x] P3.2 Printer port: OUT `#F7` latches the data, strobe (control bit 6) takes the byte on its first edge and counts
  it on the second (as Fuse), IN `#F7` reads `#FF` (no printer: busy). `state fdc` shows `printer.data_latch`,
  `bytes_printed`, `last_byte`. **File sink deferred**: the codebase has no printer infrastructure and slot options are
  enum / set only (no path option); a sink goes with a shared printer device later
- [x] P3.3 TTD device type `PlusD = 75` (was 71; master took 71-74 for the Pentagon 2.666 LE at the landing), instance `<slot>.plusd`, blob v1 (paging,
  latch, printer, no-drive line, spin-up deadline, 8K RAM, the WD1772's 254-byte blob + 112-byte command context);
  fingerprint adds `slots.<slot>.rom` (FNV-1a of the ROM); MachineStateTransfer binds it as a floppy device.
  `WD1793` gained `kTtdStateSize`, `SpinUpDoneAt` / `RestoreSpinUpDoneAt`, `NoDriveSelected` /
  `RestoreNoDriveSelected`, `RestoreTaskRegisters`
- [x] P3.4 SZX `PLSD` (Fuse layout, 19-byte header, RAM; written uncompressed without a ROM, compressed RAM read; a
  UNI-DOS / custom ROM warns), `.z80` v3 hardware 3 / 6 with byte 59 (paged) and byte 83 = 16 (DISCiPLE warns).
  Snapshot loads keep the paging the file states: `Emulator::LoadSnapshotNow` brackets the load
  (`Begin/EndSnapshotLoad`), a format without +D state runs paged out (the loader's reset would page in).
  **No automatic model switch** (D-3 spirit): a PLSD / hardware 3-6 file on a machine without a card loads with the
  +D part reported ignored; saving a 128K-PLUSD as anything but 48K / 128K drops the +D with a warning
- [x] P3.5 Tests: `plusdcard_test.cpp` +3 (`PrinterStrobeTakesTheLatch`, `TimeTravelRegistersTheCard`,
  `StateRoundTrip`), `plusdsnapshot_test.cpp` (4: SZX with / without the card, .z80 paging, .sna paged out),
  `plusdsystemdisk_test.cpp` `SnapshotButton48K` (long), `TtdSeekDuringRead` (record `CAT 1`, seek into a frame
  with a command in flight and to the end: card blob and screen match). `SaveLoadRoundTrip` is covered by the
  snapshot save / `LOAD ...S` of `SnapshotButton48K` (write and read of 97 sectors)

### P4: autostart
- [x] P4.1 `unrealdisk::GdosFileSystem` (`gdosfs.h/.cpp`): directory (80 entries, type, hidden / protected, name,
  sectors, first sector, the 9-byte tape header), `Read` along the sector links (510 data bytes each, header
  stripped), free sectors; read-only `DiskFs` volume `gdos` (probed before +3DOS). Tests `GdosFs_Test.*` (3)
- [x] P4.6 (2026-11-19) G+DOS writes in unreal-disk: `GdosFileSystem::Write` / `Delete` / `Rename` /
  `SetAttributes` as G+DOS `SAVE` / `ERASE` / `RENAME` / `PROTECT`, `HIDE` (tape header in the entry and before the
  data, first free sectors in map order); the `gdos` volume writable, so `zxdisk put / rm / mv / attrib` and the
  media verbs `write` / `delete` / `rename` / `attrib` take G+DOS disks. Layout: `disk-file-systems.md`, G+DOS
  layout. Tests `GdosFs_Test.WriteCodeAfterTheLastFile`, `DiskFsPutReplaceRenameProtectDelete`, `WriteOnABlankDisk`;
  `PlusDAutostart_Test.RunsAProgramWrittenByTheDiskTool` (G+DOS in the emulator loads and runs a written program)
- [x] P4.2 `PlusDAutostart` (`slots/cards/plusd/plusdautostart.*`), owned by `DiskAutostart`, stepped at the frame
  end by `MainLoop`. Plans `run` / `run+load` / `system-disk+load` / `mount` and the start command as in
  [autostart.md §3](autostart.md#3-the-autostart-plan). "DOS loaded": the whole `+SYS` body seen at the +D RAM's
  `#2000` (latched: G+DOS rewrites its last byte), the +D paged out, then the ROM editor waiting for a key (editor
  monitor). Result fields `plan`, `command`, `dos` (REST, host actions); `state fdc` +D node `autostart` (plan,
  stage, steps with frames). Tests `PlusDAutostart_Plan.*`, `PlusDAutostart_Test.*` (7, 30-310 ms each)
- [x] P4.3 `LoadDisk` / `AutostartDisk` of an `.mgt` go to `fdd.plusd1` / `plusd2`; no +D: refused, naming
  `48K-PLUSD`, `128K-PLUSD` and the edge slot (D-3); a drive other than 1 / 2: refused. `LoadDisk_ByExtension` no
  longer mounts MGT in a Beta drive
- [x] P4.4 `PlusDAutostart_Image.One` (skipped unless `UNREAL_PLUSD_ONE` names an `.mgt`; `UNREAL_PLUSD_MODEL`,
  `UNREAL_PLUSD_BASIC`): the autostart of one disk with the steps, the BASIC position and the screen every 250
  frames, for a closer look at a disk that does not start. Before the editor wait of P4.2 an `AUTO*` disk without
  `+SYS` ran only by chance (the swap came before the ROM looked for `AUTO*`).
- [x] P4.5 (2026-11-18) Fixes found on the way, none per title:
  - **The 128K reset**: the autostart reset into whatever `RESET=` said (48 BASIC by default), so on a 128K every work
    saw a 48K. Now it resets into the machine's BASIC (128K menu, then `128 BASIC`); `AutostartDisk(..., basic)`
    takes `48` / `128` ([autostart.md, the BASIC of the reset](autostart.md#the-basic-of-the-reset)). H-7 settled.

### P5: docs, planes, platforms
- [x] P5.1 Docs and recipes of design.md §11: `docs/features/slots.md` and `media.md`, `.recipe/machines/slots.md`,
  `.recipe/media/insert-disk.md` and `load-snapshot.md`, `.recipe/run/autostart-disk.md`, the device-history table,
  the `UNREAL_PLUSD_*` test variables in `docs/emulator/environment-variables.md`
- [x] P5.2 All planes for the new fields / actions (`basic` on REST, WebSocket, MCP, CLI, Python, Lua, `media.open`); MCP live block `test_plusd.py`
- [x] P5.4 A/B benchmark (test-plan §6; results there). The card as landed cost a 48K without it about 3 % (the
  per-step loop over card FDCs) and kept its bus overlay on for good (48K-PLUSD idle +13.6 % over 48K). Fixed: the
  overlay is installed only while the card is paged in, and `MainLoop` steps card FDCs behind one flag that follows
  their sleep (`WD1793::setSleeping` -> `EmulatorContext::RefreshCardFdcWork`). After: 48K without the card at the
  level of a build with no card loop at all, 48K-PLUSD idle -13 % vs master, the G+DOS read +4 %
  (`core/benchmarks/emulator/plusd_frame_benchmark.cpp`)
- [x] P5.5 New-platform review checklist §6: the drives are media slots of the card (`fdd.plusd1/2`, the aliases,
  insert / swap / dirty / refused eject: `PlusDCard_Test.DrivesAreMediaSlots`), the card opens no file itself, its
  ports are claimed through the slot manager, the TTD round trip is tested (`TtdSeekDuringRead`,
  `TimeTravelRegistersTheCard`), no endpoint bypasses the managers; no INI keys of its own.
  The TSFM in the AY socket of `48K-PLUSD` is the shipped 48K's `[SLOTS]` (a retrofitted AY interface), and
  `fdd.a`-`fdd.d` are the machine's Beta drives as on every 48K: neither comes from the card

### P7 (later, D-4): DISCiPLE
- [ ] P7.1 Card `disciple` on the same code: ports, traps `0001/0008/0066/028E`, inhibit, `/DDEN`, Kempston on `1F`
- [ ] P7.2 GDOS 3d boot test

## Open hardware items

H-1 ... H-8 in [hardware-reference.md](hardware-reference.md#open-items).

## Owner decisions (2026-11-15)

- D-1 Presets are variants of 48K / 128K (`48K-PLUSD`, `128K-PLUSD`); no new `MM_*` model.
- D-2 The system disk is written by the emulator from the Plus D System Tape v2a (`CONFIG` with default answers);
  S-DISK serves only as a comparison. P0.4 therefore runs after P2.
- D-3 A `.mgt` on a machine without a +D is refused with a hint naming the presets; no automatic model switch.
- D-4 DISCiPLE later: P7 is not part of this work.

## Other emulators checked

- Xpeccy-plus (`dotkoval/xpeccy-plus`, `origin/main` `fef3e9df`, 2026-11-15): **no +D**. Disk interfaces are
  `DIF_NONE`, `DIF_BDI`, `DIF_P3DOS` only (`src/libxpeccy/diskif.c:149-152`); `plusd.rom` / `disciple.rom` appear
  only in the copyright note copied from Fuse (`config/roms/AMSTRAD.copyright`), and the ROM files are not shipped.
- References that do implement it: MAME `bus/spectrum/mgt.cpp`, Fuse `peripherals/disk/plusd.c`
  ([hardware-reference.md](hardware-reference.md)).
