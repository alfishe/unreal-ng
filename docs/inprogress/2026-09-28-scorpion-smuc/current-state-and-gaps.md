# SMUC in unreal-ng today (branch `ide-atapi`) and the gaps

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Code state** | branch `ide-atapi` (IDE rollout 1, P1-P7 done, not yet on `master`). The code below exists only on that branch, so it is named, not linked |
| **Hardware facts** | [hardware-reference.md](hardware-reference.md) (the consensus this page is compared against) |

## 1. Where the SMUC code lives

| Piece | File (`ide-atapi`) | What it does |
|---|---|---|
| Port decode and sub-devices | `core/src/emulator/ports/models/portdecoder_scorpion256.{h,cpp}`: `IsPort_SMUC` (cpp 714-746), `ReadSMUCPort` (926-978), `WriteSMUCPort` (980-1022), the IN / OUT arms (220, 437) | the whole `#xxBA` / `#xxBE` family, before the `#FE` arm |
| Presence | same header: `_smucEnabled` (default `false`), `SetSmucEnabled`, `IsSmucFitted()` = `_smucEnabled` or `[HDD] Scheme=SMUC` | "is the card on the bus" |
| IDE window | `core/src/emulator/io/ide/ideadapter.{h,cpp}`: `IdeAdapter::SmucIn` / `SmucOut` (cpp 291-323), `ResetUnits` (41-45) | the real disk core (`AtaChannel`, `AtaDisk`, `AtapiCdrom`) behind `#F8BE-#FFBE`, `#D8BE`, `#FEBE` |
| Clock + settings memory | `core/src/emulator/io/rtc/smucnvram.{h,cpp}`: `SMUCNvram` | 2 KB serial EEPROM (behavioral port of the Xpeccy LC16 model) and a 256-byte CMOS whose time registers read the host clock (or a frozen time for tests) |
| Latches | `EmulatorState::pFFBA`, `p7FBA` (`core/src/emulator/platform.h:1148`) | `#FFBA` and `#7FBA` |
| Board selection | `core/src/emulator/io/ide/idecontroller.cpp:148-154` (SMUC valid on `SCORPION` and `PROFSCORP`), `config.cpp:134` (`Scheme=SMUC`) | `[HDD] Scheme` |
| Report | `core/src/emulator/state/devicestate.cpp:1311-1337` (`state ide`, gate text "TR-DOS ports on" for SMUC) | every automation surface |
| Tests | `core/tests/emulator/ports/models/scorpionsmuc_test.cpp` | see §2 |

## 2. What works

| Feature | Evidence |
|---|---|
| The serial EEPROM protocol the ProfROM uses (START, select, ACK, shift in / out, STOP, 16-byte page write) | `ScorpionSMUC_Test.SerialEEPROMWriteAndReadback` replays the ProfROM page 7 primitives bit by bit |
| The ProfROM boot with the card fitted: the NVRAM checksum, the first-boot format path (`#0D6F` writes `#61` at EEPROM address 0), the four "not found" lines gone from the boot panel | `ScorpionSMUC_Test.ProfRomBootDrivesSmucProbes`; analysis in [profrom-smuc-not-found-and-driver-disassembly.md](../2026-09-07-scorpion-zs256-clone/profrom-smuc-not-found-and-driver-disassembly.md) §8.1 |
| The keyboard is not shadowed by the SMUC decode | `ScorpionSMUC_Test.KeyboardRowsAreNotShadowedBySmuc` (regression of 2026-09-10, same doc §8.3) |
| **IDE through the real disk core** (IDE design R4): with `Scheme=SMUC` and an image in `ide0.master`, the ProfROM sends IDENTIFY DEVICE (`#EC`) to `#FFBE`, gets the data, and goes on to READ SECTORS (`#20`) with no ABRT | `ScorpionSMUC_Test.ProfRomIdentifiesTheDiskThroughTheDiskCore` |
| The IDE slots, image formats (raw, HDF, HDI, VHD, ISO), host folders as FAT volumes, write-through, the media verbs on every surface, the HDD LED | shared with every IDE board (the IDE implementation plan §5 on `ide-atapi`; [integration-ide-cd.md](../2026-09-28-storage-manager/integration-ide-cd.md)) |
| TTD of the IDE channel: `PeripheralId::AtaChannel = 17` holds both units and the adapter latches; guest writes are replay barriers | shared (`debugger/ttd/ide/ttdatachannel.*` on `ide-atapi`) |
| The `state ide` report, with the scheme and the units | shared |

## 3. What is stubbed or missing

| Item | Today | Consensus / need |
|---|---|---|
| TR-DOS gating | not gated: the card answers in any ROM | gated by the TR-DOS ports (hardware-reference §4). The `state ide` report already *says* "TR-DOS ports on" for SMUC, but the decoder does not enforce it |
| Decode mask | whole low byte + A15 / A13 / A12 / A11 (`#B8FF`) | `#B8E7` (A4, A3 not decoded) |
| `#FFBA` D0 reset | **fixed on `ide-atapi` (2026-09-28)**: a write with D0 = 0 resets the IDE units, as MAME and the ProfROM do (`ScorpionSMUC_Test.IdeResetIsBitZeroLow`) | D0 = **0** resets (hardware-reference §5.6) |
| `#FFBA` read D7 (INTRQ) | always 1 | INTRQ of the selected unit is the likely hardware behavior (open question Q2); `IdeAdapter::AtmIntrqBit()` already computes it for ATM |
| `#7FBA` read | `latch | #3F` (D3 always 1) | `latch | #37` |
| 8259 | `#7EBE` / `#7FBE` read `#57`, writes ignored | same (consensus: absent). The ProfROM probe then reports no 8259. Only needed if the "PIC fitted" variant is wanted (phase S8) |
| Version / revision | `#3F` / `#57` | same (decodes to 1.2) |
| Virtual FDD | a plain latch | the ProfROM TR-DOS does the image redirection in software, so a latch is enough for the documented behavior; a hardware trap of the floppy controller is unknown (Q5) |
| ISA window | not decoded | not needed (no ISA cards emulated); the ProfROM ISA probe at `#7AFE` then fails, as on a card without ISA devices |
| NVRAM / CMOS persistence | none: the EEPROM and the CMOS reset to zero on every start, so the ProfROM runs its first-boot format path (~4 s) on **every** cold boot | a battery-backed file per machine (UnrealSpeccy files `CMOS` and `NVRAM`, `config.cpp:136-148`, `850-861`; ZXMAK2 satellite files; Xpeccy profile). PLAN #60(c) plans one shared CMOS chip `Ds12887` with an NVRAM file for ATM3, Profi, `SMUCNvram` and `EvoAvr` |
| RTC | the time registers read the host clock (or `SetFixedTime`); registers A-D are a minimal model (`SMUCNvram::ReadCMOS`), no periodic interrupt, no alarm | deterministic time for TTD (emulated time since a recorded base), registers A-D as a DS1685 |
| TTD of the card | `pFFBA`, `p7FBA`, the EEPROM (contents and protocol state) and the CMOS are **not** in any TTD blob. `TTDScorpionProfROM` (id 6) carries paging only | a SMUC blob (phase S5). Also the host-clock reads make a replay diverge unless the time is frozen |
| Presence switch | two flags: `_smucEnabled` (a test hook, default off) and `[HDD] Scheme=SMUC`. The shipped ini key `[MISC] SMUC=0` is **not parsed** (the `CONFIG::smuc` field exists, `platform.h:555`, unused). The PROFSCORP model decodes the family even without the card (reads `#FF`), the plain SCORPION does not (falls through to the `#FE` arm) | one rule: fitted = `[MISC] SMUC=1` or `Scheme=SMUC` (UnrealSpeccy meaning: `SMUC=1` is the clock / NVRAM / system part, `Scheme=SMUC` the IDE part), same behavior on both models |
| Old stub IDE registers | `_smucIdeRegs[8]` (status `#50`, data `#00`) still answer when the card is fitted by `_smucEnabled` but the scheme is not SMUC | remove: with the real core there is no reason for a fake drive |
| Automation / Qt for the card itself | the IDE units are covered; nothing shows `#FFBA`, `#7FBA`, the NVRAM or the RTC; no Qt switch to fit the card | a `smuc` section in the device report, a machine option, docs |

## 4. Why the card is absent by default and the shipped configs say `Scheme=NONE`

Two separate decisions, both still valid:

1. **Absent by default** (2026-09-10, [profrom-smuc-not-found-and-driver-disassembly.md](../2026-09-07-scorpion-zs256-clone/profrom-smuc-not-found-and-driver-disassembly.md) §8.2):
   with the card present the ProfROM cold boot reaches its menu at about frame 350 instead of 150.
   The extra ~200 frames (~4 s) are the bit-banged I²C: the checksum pass over 254 NVRAM bytes,
   the first-boot format and the config save. Without persistence this cost is paid on every
   boot.
2. **`Scheme=NONE` in `data/configs/scorpion` and `profscorp`** (IDE implementation plan §2.1 on
   `ide-atapi`): SMUC is an add-on card, the base ZS-256 has none (scope decision 1 of the
   [Scorpion clone project](../2026-09-07-scorpion-zs256-clone/README.md)), and the shipped boot
   and its tests are pinned without it. MAME ships the Scorpion with empty ZX-BUS slots and
   UnrealSpeccy ships `SMUC=0`, so this matches the references.

Tests that depend on the card being absent (they boot the real ProfROM and assert on its
timeline, screen or state): `scorpionturbodetect_test`, `scorpionromwindow_test`,
`scorpionrommapping_test`, `profrom_plane_notification_test`, `ttdscorpionprofrom_test`,
`ttdmodelstatecontract_test`, `core_golden_test`, the TTD fixture corpus. Keeping the default
absent keeps them unchanged.

## 5. Gap list

| # | Gap | Size | Phase |
|---|---|---|---|
| G1 | No TR-DOS gating | S | S1 |
| G2 | Decode mask narrower than the hardware (A4, A3) | S | S1 |
| G3 | ~~IDE reset polarity inverted~~ - fixed on `ide-atapi` (D0 = 0 resets); also fixed there: `#D8BE` stays the latch while `#FFBA` D7 is set (MAME, Xpeccy) | - | done |
| G4 | `#FFBA` D7 does not carry INTRQ | S | S2 |
| G5 | `#7FBA` reads D3 as constant 1 | S | S2 |
| G6 | Two presence flags, `[MISC] SMUC` not parsed, PROFSCORP and SCORPION decode differently when absent, stale `_smucIdeRegs` stub | S | S3 |
| G7 | NVRAM and CMOS lost on every start (first-boot format each time) | M | S4 (with PLAN #60(c)) |
| G8 | RTC reads the host clock: not deterministic, no DS1685 registers A-D | M | S4 |
| G9 | SMUC latches, EEPROM and CMOS not in TTD | M | S5 |
| G10 | No end-to-end test beyond IDENTIFY: partitions, mounting a TR-DOS image from the disk, booting from it | L | S6 |
| G11 | No report / Qt / docs for the card itself | M | S7 |
| G12 | 8259 (and the RTC → 8259 → IM 2 interrupt path) not emulated | L (optional) | S8 |
| G13 | Virtual FDD hardware behavior unknown (a latch only) | research | Q5 |
