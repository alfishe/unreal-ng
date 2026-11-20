# MGT +D: technical design

How the +D fits unreal-ng. Hardware facts: [hardware-reference.md](hardware-reference.md); boot and autostart:
[autostart.md](autostart.md); tests: [test-plan.md](test-plan.md); phases: [TODO.md](TODO.md). Code references
are to master at `ed9dc26ca` (2026-11-15).

## 1. Shape of the solution

The +D is a **card of the slot manager on the Sinclair edge bus** (`plusd`), not a machine model and not a variant
of the Beta 128 path. It brings:

| Part | New code | Built on |
|---|---|---|
| Card `PlusDCard` | `core/src/emulator/slots/cards/plusd/` | `KempstonCard` (card plumbing, TTD blob), `CardDef` / `CardType` |
| ROM / RAM overlay over `0x0000-0x3FFF` | `PlusDPaging` (inside the card) | `HostBusOverlay` + `IMachineM1Hook`, as `DivMmcPaging` does |
| FDC | `WD1793` with a chip variant `Wd1772` | `core/src/emulator/io/fdc/wd1793.*` |
| Two drives, media slots `fdd.plusd1`, `fdd.plusd2` | `FloppyDriveSlot` generalised to a controller prefix | `MediaManager`, `FloppyDriveSlot` |
| Control latch, printer port, NMI button | in the card | port claim table, `Emulator::RequestMNI` |
| Autostart | +D branch of `DiskAutostart` | [autostart.md](autostart.md) |
| Snapshots | `.z80` modes 3 / 6, SZX `PLSD` | `loader_z80.cpp`, `loaderszx.cpp` |
| Configurations | `48K + Plus D`, `128K + Plus D` | `[SLOTS] edge.1 = plusd` |

What it is the first of: no card pages memory today (cards are port devices); no machine has two floppy
controllers; `pBetaDisk` is assumed to be the only FDC in several places. These three are the real work; the +D
logic itself is small (Fuse: 520 lines).

## 2. Configuration ("the new configuration")

Two configuration presets, no new `MM_*` model:

| Preset | Machine | Slots | Beta 128 |
|---|---|---|---|
| `48K + Plus D` | `MM_SPECTRUM48` | `edge.1 = plusd` | off (the 48K's `fitted` beta128 is not fitted in this preset) |
| `128K + Plus D` | `MM_SPECTRUM128` | `edge.1 = plusd` | none |

Keys (`data/configs/<preset>/unreal.ini`, the existing slots syntax of `docs/features/slots.md`):

```ini
[SLOTS]
edge.1 = plusd
edge.1.rom = gdos-pd.rom      ; G+DOS v1a (default); unidos, gdos1 when their ROMs are added
edge.1.drives = 2             ; 1 or 2
edge.1.drive1 = 3.5-80-ds     ; geometry of the drive mechanism
edge.1.drive2 = 3.5-80-ds
```

A preset is a `machinevariants.cpp` entry (`48K-PLUSD`, `128K-PLUSD`, like `48K-BETA128`) so that the model list,
the Qt menu, `emulator/models` and the automation can name it. Any other machine with an edge connector can take the card
through `[SLOTS]` or `slots set` at run time; the fit is the slot manager's (§3).

ROMs: `data/rom/gdos-pd.rom` is already in the tree (G+DOS v1a, CRC32 `569F7E55`, unused today) and stays where it is;
its hashes are in `data/rom/README-ROMS.md`. UNI-DOS v2 and G+DOS v1 (MAME hashes in hardware-reference) are not in
the collections (scan of 2026-11-15) and are added when found. `data/rom/gdos.rom` is the DISCiPLE GDOS v3 ROM: kept for P7.

## 3. The card

Reference data (`slots/refdata/cards.cpp`):

```cpp
// MGT +D: hardware-reference.md (MAME mgt.cpp, Fuse plusd.c, G+DOS ROM)
constexpr PortClaim kPlusDClaims[] = {
    {.mask = 0x00FF, .match = 0x00E3, .dir = Dir::ReadWrite, .port = "fdc-status-command"},
    {.mask = 0x00FF, .match = 0x00EB, .dir = Dir::ReadWrite, .port = "fdc-track"},
    {.mask = 0x00FF, .match = 0x00F3, .dir = Dir::ReadWrite, .port = "fdc-sector"},
    {.mask = 0x00FF, .match = 0x00FB, .dir = Dir::ReadWrite, .port = "fdc-data"},
    {.mask = 0x00FF, .match = 0x00EF, .dir = Dir::Write,     .port = "control"},
    {.mask = 0x00FF, .match = 0x00E7, .dir = Dir::ReadWrite, .port = "page-in-out"},
    {.mask = 0x00FF, .match = 0x00F7, .dir = Dir::ReadWrite, .port = "printer"},
};
constexpr const char* kPlusDMedia[] = {"fdd.plusd1", "fdd.plusd2"};
// CardDef{.id = "plusd", .name = "MGT +D", .bus = BusKind::SinclairEdge,
//         .needs = BusSignal::RdRom | BusSignal::M1 | BusSignal::Nmi, .functions = {Function::DiskMgt}, ...}
```

- **Decode mask.** `0x00FF` (Fuse) first; MAME's `A1-A6` decode is an option `edge.1.decode = partial` once H-3 is
  settled. With the partial decode the +D answers every port whose `A1-A6` match (e.g. `0x63`, `0xE2`, `0x1E3`
  ...), including even ports the ULA also answers: the claim table reports the overlaps, nobody checks them by hand.
- **Function.** New `Function::DiskMgt` ("MGT disk interface") in `slotvocabulary.h`. Rule D1 then refuses two MGT
  cards; a +D and a Beta 128 are different functions and may coexist (their ports differ: Beta `1F/3F/5F/7F/FF`).
- **Fit.** Needs `/ROMCS` (`RdRom`): `real` on the 48K (`kEdge48`) and 128K / +2 (`kEdge128`); on the +2A / +3
  (`kEdgePlus3`, no `/ROMCS`) the manager reports `unrealistic` (H-8). Pentagon / Scorpion have a ZX-bus, not the
  edge: `adapter` at best; the manager's matrix decides, regenerated with `UNREAL_SLOTS_MATRIX_UPDATE=1`.
- **Known claim conflicts** the matrix must show: DivMMC (`E3/E7/EB`, not created by any machine today), the
  `covox-fb` card (`FB`), `zx-wifi` (`xxEF` with a full decode: no clash with `00FF/EF` only if its mask differs:
  checked by the table).

Runtime (`PlusDCard : ICard`):

```cpp
class PlusDCard final : public ICard, public HostBusOverlay, public IMachineM1Hook
{
    // ICard / PortDevice: the claims above route here
    uint8_t PortIn(uint16_t port) override;         // E3..FB -> _fdc; E7 -> PageIn(); F7 -> busy
    void PortOut(uint16_t port, uint8_t v) override; // EF -> latch; E7 -> PageOut(); F7 -> printer data

    // IMachineM1Hook: page in BEFORE the opcode read at 0008 / 003A / 0066 / 028E
    void BeforeMachineM1(uint16_t address) override;

    // HostBusOverlay (window 0x0000-0x3FFF): while paged, reads give ROM / RAM, writes 2000-3FFF go to RAM
    std::array<uint8_t, 0x2000> _rom, _ram;
    bool _paged = true;            // after reset: H-2
    uint8_t _control = 0;          // EF latch
    std::unique_ptr<WD1793> _fdc;  // variant Wd1772, not pBetaDisk
    std::array<FDD, 2> _drives;    // owned by the card
};
```

### 3.1 Paging: overlay and M1 hook

- `BeforeMachineM1` compares the address with four constants and sets `_paged`; `machineM1HookEnd = 0x0300`, so a
  fetch above `0x02FF` costs one compare. The hook pointer is exclusive (`Z80::machineM1Hook`, `DivMmcPaging::Attach`
  refuses a taken one). On 48K / 128K nobody else takes it; on machines whose decoder takes it (ATM3, Sprinter,
  TSConf, Next) the card cannot attach and the manager reports it. **P1.2 turns the single pointer into a small
  chain** (as the Next already does internally, `portdecoder_next.cpp:365`) only if a second user on a 48K / 128K
  appears; not needed for the +D presets.
- The overlay answers the window only while `_paged`; `Core::AddBusOverlay` when the card is created, removed when it
  is pulled. With no card, nothing changes on the hot path (no overlay, no hook).
- 128K: the overlay sits on top of whatever `7FFD.4` maps, so the 128K ROM switching is untouched. H-7 (128K first
  boot) is a test, not a design branch.
- The memory viewer, the disassembler and `memory_access` must show what the CPU sees: they read through the
  overlay chain already (`Memory::MemoryReadOverlay`); a test checks that a paged-in +D shows its ROM at `0x0008`
  in `memory_access` and the disassembly (P2).
- Breakpoints and TTD position use the CPU address; a "+D ROM" bank name in `inspect_state` banks
  (`ROM: +D G+DOS`, `RAM: +D`) comes from a `DeviceState` report of the card.

### 3.2 NMI (the snapshot button)

A card action `plusd.snapshot` (the manager's card-action verb, every plane) raises `Emulator::RequestNMI()`; the
`0x0066` trap pages the +D in. `RequestMNI` (the generic "magic" key) calls the action when the card is present and
the machine has no board NMI of its own. Qt: an entry in the machine menu next to Magic.

### 3.3 Printer

No Centronics device exists. P3 keeps the port honest: `F7` read returns "not busy" with bits 0-6 as on the board
(H-4; Fuse returns `0x7F` with a printer, `0xFF` without), writes are latched and the strobe edge in `EF.6` hands the
byte to an optional sink (`edge.1.printer = file:<path>` writes raw bytes to a file in the session folder). A real
printer emulation is out of scope.

## 4. The FDC: WD1772 as a variant of `WD1793`

No chip variant exists today. Add one:

```cpp
enum class WdVariant : uint8_t { Wd1793, Vg93 = Wd1793, Wd1772 };
WD1793(EmulatorContext* context, WdVariant variant, FdcPorts ports, std::span<FDD*> drives);
```

| Area | Change for `Wd1772` | Where (master) |
|---|---|---|
| Step rates | table `{6, 12, 2, 3}` ms (WD1772-00 data sheet; H-6) | `STEP_TIMINGS_MS_*` `wd1793.h:662-663`; uses at `wd1793.cpp:1272`, `1362`, `1381`, `1994` |
| `h` flag | Type I/II/III bit 3 = disable spin-up; no head load | `CMD_SEEK_HEADLOAD` `wd1793.h:400`, `wd1793.cpp:2000-2007`; `loadHead` / `unloadHead` `757/774` and their calls |
| Motor | `MO` on at a command; off after 9 index pulses idle; spin-up 6 index pulses unless `h` or motor on | `processFDDMotorState` `515`, `prolongFDDMotorRotation` `707`, `startFDDMotor` / `stopFDDMotor` `722/732` |
| READY | none: never `NOTRDY`; status bit 7 = motor on | `isReady()` `950` and the `WDS_NOTRDY` sites (listed in the code map: `531`, `867-926`, `1715-1783`, `1946`, `2036`, `2090`, `2229-2381`, `2559`, `2991`, `3159`) |
| Status bit 5 | Type I: spin-up complete | `getStatusRegister()` `788`, `WDS_HEADLOADED` `813`, `2705` |
| Side | no side compare; bit 1 of Type II = precomp disable; side from the card latch | `CMD_SIDE_CMP_FLAG` / `CMD_SIDE` `wd1793.h:405-408`, `wd1793.cpp:2804` |
| Settle `E` / `V` | 15 ms | `HEAD_SETTLE_MS_*` `wd1793.h:646-647`, `HeadSettleTStates()` |
| Force interrupt | I2, I3 only | the D0 command handler |
| Ports | not the Beta literal `1F..FF` switch: the card calls `ReadRegister(n)` / `WriteRegister(n, v)` | `portDeviceInMethod` / `OutMethod` `3646/3736`, `attachToPorts` `3842` |
| Drive / side | from the card (`SelectDrive(n)`, `SetSide(s)`), not `processBeta128` | `processBeta128` `350-383`, `selectDrive` `387` |
| Drives | owned by the caller, not created into `coreState.diskDrives` | constructor `wd1793.cpp:87-91` |

The variant is a member checked in those few places, not a subclass (the logic is 95 % shared; a subclass would
duplicate the state machine). The Beta path stays bit for bit: its tests (`core/tests/emulator/io/fdc/*`) are the
regression gate, plus a TTD round trip on a TR-DOS load.

Timing: the WD1772 runs at 8 MHz with DD at 250 kbit/s, the same byte time (32 us) as the Beta's WD1793 at 1 MHz DD.
The existing byte-timing code holds; only the delays above change.

## 5. Two controllers in one machine

`pBetaDisk` is the only FDC in: `MainLoop::OnCPUStep` stepping (`mainloop.cpp:657-676`), the idle check
(`z80.cpp:456-463`), TTD (`ttdmachineperipherals.cpp:156-162`), SZX, `DiskFastLoad`, `DiskAutostart`,
`devicestate.cpp:1327`, and `coreState.diskDrives[4]` (`corestate.h:49`).

Design:

1. **Stepping.** A context list `fdcControllers` (Beta first, then cards). `OnCPUStep` steps each that is active;
   the card registers its WD there on create and removes it on pull. The idle check asks every controller.
2. **Drives.** The card owns its two `FDD` objects. `FloppyDriveSlot` takes `(slotId, FDD*, controllerTag)`
   instead of an index into `coreState.diskDrives`; the Beta slots keep `fdd.a..d` and aliases `A..D`; the +D
   slots are `fdd.plusd1`, `fdd.plusd2` with aliases `plusd1`, `plusd2` (and `1` / `2`? no: `1` could be read as a
   Beta drive; aliases stay explicit). Tags `wd1772`, `gdos`.
3. **Slot-scoped write notes.** `NoteSlotWrite` / `BeforeTrackWrite` / `BeforeImageGrow` take the slot id, not a
   drive index (the code map lists them).
4. **Inspection.** `DeviceState` gets one report per controller (`fdc.beta`, `fdc.plusd`), registers, drive,
   side, motor, spin-up, the latch and the page state; `inspect_state` aspect `fdc` lists both.
5. **Fast disk.** `DiskFastLoad` stays Beta-only in this work; the +D runs at the real rate, turbo speeds it up.
   (A +D fast path is a later item: a G+DOS boot is ~3 s of emulated time.)

## 6. Media and formats

- `.mgt` / `.img` already load through `unrealdisk::LoaderMGT` into a `DiskImage(cyl, 2)` with
  `TrackFormatSpec::plusD()`. Nothing changes in the loader; the slots of the +D accept the same formats as the Beta
  slots (a TRD in a +D drive is a valid medium that G+DOS cannot read: allowed, like on the hardware).
- Writes: the existing session semantics (`save` / `export` / `discard`). G+DOS writes when it saves or formats.
- The drive mechanism (40 / 80 tracks, sides) is a slot option; a 40-cylinder image in an 80-track drive is read with
  double stepping off (like the hardware: G+DOS is configured for the drive).
- `load_software` / `disk insert` with an `.mgt`: [autostart.md §4](autostart.md#4-what-the-automation-sees).
  The default drive for an MGT on a machine with a +D is `fdd.plusd1`.

## 7. Snapshots, TTD, sessions

| Item | Design |
|---|---|
| `.z80` v2/v3 modes 3 / 6 | load as 48K / 128K **with** the +D card when the machine can take it (`switch_model` rules as today); `mgtROM` (byte 59: paged) and `mgtType` (byte 83: 0 Disciple+Epson, 1 Disciple+HP, 16 +D) applied; Disciple types answered with "DISCiPLE not supported" until P7. Saving: written when the card is present |
| SZX `PLSD` | read and written: flags (paged, seek direction), control latch, FDC registers, 8 KB RAM, ROM type / custom ROM; `PDSK` (disk names) read as a hint only |
| TTD | `PlusDCardTtd` via `CollectTtdDevices` (new `TTDDeviceType` >= 66, see the TTD device key rule): page state, latch, RAM, printer latch; the WD state through the existing WD serializer with the variant in its header |
| Session save | the card is part of the slot plan; media through the manager |

## 8. Automation planes

Nothing private. Through the managers the planes already have: `slots` (add / remove / options), `media`
(insert / eject / list for `fdd.plusd1/2`), card actions (`plusd.snapshot`), `DeviceState` reports. Per
[automation-planes-guidelines.md](../../guidelines/automation-planes-guidelines.md) every new verb or field is added
to CLI, Python, Lua, MCP, REST and WebSocket, and the docs chain `command-interface.md` -> module doc -> plane docs.
New fields: the autostart result (`plan`, `command`, `dos`, `steps`), the `fdc` aspect for two controllers.

## 9. Qt

Machine menu: the two presets in the model list; `Slots` dialog shows the card; drives `+D 1` / `+D 2` in the disk
menu and the status bar disk LED per controller; `Snapshot button (+D)` action. Extending existing widgets only.

## 10. Phases

| Phase | Content | Done when |
|---|---|---|
| P0 | Data: ROM folder and hashes, WD177x data sheet in `docs/WD1793/datasheeets/` | files in place, hashes in `README.md` |
| P1 | `WD1793` variant `Wd1772`; register-level ports; drives owned by the caller; controller list in the main loop | FDC unit tests for 1772 rules green; all Beta tests and a TR-DOS TTD round trip unchanged |
| P2 | `PlusDCard`: claims, overlay, M1 hook, latch, two drive slots, presets `48K-PLUSD` / `128K-PLUSD`, `DeviceState` | G+DOS boots from the clean system disk on 48K and 128K; `CAT 1` lists the disk |
| P3 | NMI button action, printer port, TTD serializer, SZX `PLSD`, `.z80` modes 3 / 6 | snapshot button saves a 48K snapshot to disk and it loads back; TTD seek round trip during a disk read |
| P4 | Autostart (+D branch), the emulator's system disk, `load_software` routing, results | the autostart plans run on the test disks and the system disk |
| P5 | Docs and recipes (list in §11), slots matrix regenerated, MCP live scripts, gcc / MSVC builds | review checklist of `new-platform-guidelines.md` §6 all "yes" |
| P7 (later, D-4) | DISCiPLE: ports `1B/5B/9B/DB`, `1F` (control, Kempston), `3B` network, `7B` boot, `BB` page, `FB` printer; traps `0001/0008/0066/028E`; inhibit; GDOS 3d ROM (`data/rom/gdos.rom`) | GDOS 3d boots |

## 11. Documentation to update (P5)

`docs/features/slots.md` (word list, card section), `docs/features/media.md` (slot table, formats),
`docs/features/automation.md`; `docs/inprogress/2026-10-03-zx-bus-slots/compatibility-matrix.md` (generated) and
`reference-data.md`; `.recipe/machines/spectrum.md`, a new `.recipe/machines/plus-d.md`, `slots.md`,
`.recipe/_common/machines.md`; `.recipe/media/insert-disk.md` (lines 6-7, 227), `use-media-slots.md`,
`load-snapshot.md`; `.recipe/run/autostart-disk.md` (line 82: MGT no longer mount-only);
`docs/emulator/design/snapshots/emulator_snapshots.md`; the control-interface docs (slots, disk sections);
`docs/emulator/design/debugger/time-travel-debug/ttd-v1-architecture-and-format.md` (device type);
`docs/emulator/design/debugger/device-history.md`; `docs/emulator/environment-variables.md` if a variable is added.

## 12. Risks

| Risk | Answer |
|---|---|
| The `WD1793` variant breaks the Beta path | variant checks only in the listed places; full FDC test set + TR-DOS TTD round trip per change |
| Second controller misses a `pBetaDisk` site | grep gate in the review (`pBetaDisk` uses listed and each one decided); a test with both Beta and +D on a 48K doing disk I/O on both |
| M1 hook taken on exotic machines | the manager refuses the card there with a reason; the presets are 48K / 128K |
| H-1 / H-2 wrong | both are one constant each; the PAL equations settle them; G+DOS boot tests catch a wrong reset state immediately |
| Hot path | no card = no overlay, no hook; with the card: one compare per M1 under `0x0300` + overlay call; A/B benchmark 48K with and without the card (performance guidelines) |
