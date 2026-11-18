# MGT +D: TODO

Status: **P0-P4 done** (P4.5 2026-11-18), landed on master 4531969d5 (2026-11-18): the card, the autostart, the
`48K-PLUSD` / `128K-PLUSD` machines in the Sinclair family of the Qt and handheld menus, the recipe
`.recipe/machines/plusd.md`. Open: P5 (the `basic` param on the planes), P6.

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
  BASIC `BORDER n` loop, or CODE only), one per plan. Was: fixture work disks into `testdata/machines/plusd/`. Candidates (catalog pool sha256 prefixes):
  `+SYS` + `AUTO`: Super Mario Fruit Machine 2 `a9735f4a40b5`; `+SYS` only: Chase H.Q. `e87f1e4e2462`; no DOS + `Loader`:
  Quann Tulla (classified list); no DOS + `AUTO*`: Doom Pit `5d5fe503c848`; one 48K snapshot: Behind Closed Doors 5
  `299cfbfcdde2`; several snapshots: Return of the Roach `acf98bef8054`; CODE only: Черный Ворон. No +D disk in the corpus
  holds only a 128K snapshot: that fixture is written by the emulator (`SAVE` of a type 9 file via the snapshot button)
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
  autostart / state transfer filters: 586 passed, 5 skipped (2026-11-16). Full suite and gcc check at the end of the work

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
- [x] P3.3 TTD device type `PlusD = 71` (free on master and every branch), instance `<slot>.plusd`, blob v1 (paging,
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
- [x] P4.2 `PlusDAutostart` (`slots/cards/plusd/plusdautostart.*`), owned by `DiskAutostart`, stepped at the frame
  end by `MainLoop`. Plans `run` / `run+load` / `system-disk+load` / `mount` and the start command as in
  [autostart.md §3](autostart.md#3-the-autostart-plan). "DOS loaded": the whole `+SYS` body seen at the +D RAM's
  `#2000` (latched: G+DOS rewrites its last byte), the +D paged out, then the ROM editor waiting for a key (editor
  monitor). Result fields `plan`, `command`, `dos` (REST, host actions); `state fdc` +D node `autostart` (plan,
  stage, steps with frames). Tests `PlusDAutostart_Plan.*`, `PlusDAutostart_Test.*` (7, 30-310 ms each)
- [x] P4.3 `LoadDisk` / `AutostartDisk` of an `.mgt` go to `fdd.plusd1` / `plusd2`; no +D: refused, naming
  `48K-PLUSD`, `128K-PLUSD` and the edge slot (D-3); a drive other than 1 / 2: refused. `LoadDisk_ByExtension` no
  longer mounts MGT in a Beta drive
- [x] P4.4 Corpus (2026-11-17): `PlusDAutostart_Corpus.AllImages` (skipped unless `UNREAL_PLUSD_CORPUS` = the
  classified TSV and `UNREAL_PLUSD_POOL` = pool/images; table to `UNREAL_PLUSD_CORPUS_OUT`). Machine: 128K when the
  disk holds a type 9 snapshot, else 48K, both with the card in `edge.1`; 100 frames, autostart, steps, then until the
  +D stays paged out 50 frames (at most 4000), 100 frames sampled. Nothing tuned per title. 243 images, 7 min:

  | Plan | Images | Steps ok | Running after | Not running |
  |---|---|---|---|---|
  | `run` | 19 | 19 | 12 | 5 G+DOS text on screen, 2 still in G+DOS |
  | `run+load` | 8 | 8 | 6 | 1 G+DOS report, 1 still in G+DOS |
  | `system-disk+load` | 215 | 215 | 214 | 1 back at the copyright |
  | `mount` | 1 | - | - | CODE only (Черный Ворон), as designed |
  | **all** | **243** | **242** | **232** | **11** |

  Load to settle (frames after the start command, +D paged out for good): median 197, p90 292, max 2139; G+DOS
  reads about one sector per revolution, so long adventures take 10-40 s. Before the editor wait of P4.2 five
  `AUTO*` disks without `+SYS` (Doom Pit, Seto Taisho x2, Sophia x2) ran only by chance (the swap came before the
  ROM looked for `AUTO*`) and were reported failed.

- [x] P4.5 (2026-11-18) The ones that did not run, then every image on three configurations. Fixes found on the way,
  none per title:
  - **The 128K reset**: the autostart reset into whatever `RESET=` said (48 BASIC by default), so on a 128K every work
    saw a 48K. Now it resets into the machine's BASIC (128K menu, then `128 BASIC`); `AutostartDisk(..., basic)`
    takes `48` / `128` ([autostart.md, the BASIC of the reset](autostart.md#the-basic-of-the-reset)). H-7 settled.
  - **"Loading ended"** in the corpus run is now the WD1772 idle (not busy, the motor off) for 300 frames, not the
    +D paged out: the +D traps KEY-SCAN, so a BASIC `INKEY$` menu (the Zenobi part pickers) keeps paging it in.
    300 frames, so a loader's `PAUSE` between two loads is not taken for the end.
  - The "G+DOS Modified Bootloader" disks show a banner, `PAUSE 160`, then load: they run.

  `PlusDAutostart_Corpus.AllImages` on `48K` (48 BASIC), `128K` (128 BASIC) and `128K/48` (a 128K in 48 BASIC), the
  card in `edge.1`; verdict from the screen: a BASIC report, the copyright (reset), the FDC still busy (in-dos), else
  running. Columns `min` (the first config it runs on), `runs_on`, `fails_on`. 243 images, 47 min:

  | Config | Running | Reset | Report | Not started |
  |---|---|---|---|---|
  | 48K | 217 | 19 | 6 | 1 |
  | 128K | 241 | 0 | 1 | 1 |
  | 128K/48 | 237 | 0 | 5 | 1 |

  Tags: `min 48K` 217 (run on all three), `min 128K` 24, none 2. The 26 that do not run everywhere, each checked
  on the disk: 18 load a 128K snapshot (type 9), 2 are Chase H.Q.'s 128K version, 4 need 128 BASIC (`SAVE !` on the
  RAM disk, `PLAY` strings, 128K paging from BASIC), 1 mounts only (CODE, Черный Ворон):

  | Title | Image | min | runs_on | not running |
  |---|---|---|---|---|
| Chase H.Q. | `8186c971f5f2` | 128K | 128K 128K/48 | 48K: reset |
| Horrorscope | `308f006dc3a3` | 128K | 128K 128K/48 | 48K: reset |
| The Krazy Kartoonist Kaper | `547378eb87d7` | 128K | 128K 128K/48 | 48K: reset |
| Stranded | `d460b9d7055b` | 128K | 128K 128K/48 | 48K: reset |
| Alien-B | `19e7748619bc` | 128K | 128K | 48K: report `4 Out of memory, 60:1`; 128K/48: report `C Nonsense in BASIC, 60:1` |
| Plus D Blue Peter | `c9b838fced80` | 128K | 128K | 48K: report `C Nonsense in BASIC, 60:1`; 128K/48: report `C Nonsense in BASIC, 60:1` |
| The Lost Twilight | `a200d9ead285` | 128K | 128K 128K/48 | 48K: reset |
| (EN)(128K)[p] | `746ee6443593` | 128K | 128K 128K/48 | 48K: report `4 Out of memory, 140:3` |
| Rhyme Cryme | `0a8707b53298` | 128K | 128K 128K/48 | 48K: reset |
| The Apprentice | `5b348bdc2fdc` | 128K | 128K 128K/48 | 48K: reset |
| The Final Chorus | `8bf4574d3650` | 128K | 128K 128K/48 | 48K: reset |
| The Bardic Rite | `0ce57291f17c` | 128K | 128K 128K/48 | 48K: reset |
| Leopold the Minstrel | `33a728a11144` | 128K | 128K 128K/48 | 48K: reset |
| Murder - He Said | `76030848ec2c` | 128K | 128K 128K/48 | 48K: reset |
| Super Mario Fruit Machine 2 | `a9735f4a40b5` | none | - | 48K: report `C Nonsense in BASIC, 200:1`; 128K: report `0 OK, 200:1`; 128K/48: report `C Nonsense in BASIC, 200:1` |
| The Gods of War | `4708344d6758` | 128K | 128K 128K/48 | 48K: reset |
| Knight Life | `abd63f30dd7c` | 128K | 128K 128K/48 | 48K: reset |
| Stalker | `4ba205e2a746` | 128K | 128K 128K/48 | 48K: reset |
| POKE for ZX | `99f80666cb81` | 128K | 128K | 48K: report `C Nonsense in BASIC, 290:1`; 128K/48: report `C Nonsense in BASIC, 290:1` |
| Jigsaw | `079f3e4bee07` | 128K | 128K | 48K: report `C Nonsense in BASIC, 20:2`; 128K/48: report `C Nonsense in BASIC, 20:2` |
| The Beast | `23b6cd0cfd43` | 128K | 128K 128K/48 | 48K: reset |
| Черный Ворон | `ac123c4c724d` | none | - | 48K: not-started `Autostart: no program to start - mou`; 128K: not-started `Autostart: no program to start - mou`; 128K/48: not-started `Autostart: no program to start - mou` |
| Murder Mystery Weekend | `b6672044662d` | 128K | 128K 128K/48 | 48K: reset |
| The Khangrin Plans | `a598fef03eac` | 128K | 128K 128K/48 | 48K: reset |
| The Thirty-Nine Steps | `df02a394c3c8` | 128K | 128K 128K/48 | 48K: reset |
| The Pendant of Logryn | `0e015504a91a` | 128K | 128K 128K/48 | 48K: reset |

  Open: **Super Mario Fruit Machine 2** checks `PEEK 23611 = 221` (FLAGS) at line 10 and says "128K game in 48K mode"
  otherwise. Ours: 204 in 48 BASIC, 220 in 128 BASIC (bit 0, "no leading space", differs). With that line removed
  it runs on the 128K. Which value the real 128K + +D gives after `RUN` is not known: compare Fuse / MAME before
  changing anything.

### P5: docs, planes, platforms
- [ ] P5.1 Docs and recipes of design.md §11
- [ ] P5.2 All planes for the new fields / actions; MCP live block `plusd.py`
- [ ] P5.3 Build zero warnings; `docker/linux/build.sh --test`; MSVC / MinGW
- [ ] P5.4 A/B benchmark (test-plan §7)
- [ ] P5.5 New-platform review checklist §6

### P6: catalog hand-over
- [ ] P6.1 Note for zx-software-catalog: 48K / 128K + Plus D, SAM `skip:not-zx-format` (38), verdict from `plan`; move `classify_mgt.py` there

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
