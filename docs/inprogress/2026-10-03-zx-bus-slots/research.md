# ZX-bus slots: research (phase SL-0)

| | |
|---|---|
| **Date** | 2026-10-03 |
| **Parts** | [research-machines.md](research-machines.md) (expansion connectors, IORQGE arbitration, built-ins, read conflicts, Sinclair edge variants), [research-cards.md](research-cards.md) (decode, IORQGE, functions, options and known conflicts of about 30 cards), this page (summary and the code inventory) |
| **Method** | primary sources first (schematics, netlists, CPLD / FPGA sources, service manuals), emulators only as a cross-check, disagreements tabulated; every fact carries its source |

## 1. Summary of findings

| # | Finding | Design consequence |
|---|---|---|
| 1 | IORQGE means four different things: card wins (NemoBus / Kay, Pentagon-1024SL, Scorpion), board wins (ZX-Evo, TS-Conf, Profi), ULA only (48K), none (128K, +2A, +3) | per-bus `arbitration` ([architecture.md](architecture.md) §4) |
| 2 | Cards detect I/O cycles differently; the ZX-MultiSound ignores /IORQ ("RD or WR without MREQ and M1") and therefore sees the cycles the ZX-Evo hides from the slots | per-card `detection` (`Iorq` / `RdWr`) |
| 3 | The ZX-Evo AY is a socketed YM2149, not an FPGA TurboSound; with a MultiSound both chips drive `#FFFD` reads | socketed built-ins; owner decision [Q7](open-questions.md) |
| 4 | No machine documents who wins when two devices drive one read; NemoBus convention: a readable card must drive IORQGE | `readRule` stays a labeled modeling choice; every such port is reported as a bus fight |
| 5 | The 1991 Pentagon 128 has no CPU bus connector; ATM Turbo 2 has only a 2x12 I/O bus (ZX-bus through a third-party CPU-socket adapter); Profi has its own 64-pin bus; the Sprinter reaches ZX-bus cards only through its ISA adapter | bus kinds `atm-iobus`, `profi-bus`; adapters as slot entities |
| 6 | The 128K and +3 edges have no IORQGE; the +3 has /ROM1OE + /ROM2OE instead of /ROMCS; no published ZX-bus-to-edge adapter for the 128K (Velesoft ZX Bus Protector, BDI 2.0 pass-through exist) | `zxbus` cards on Sinclair machines are `adapter` or `unrealistic` |
| 7 | The classic GS has no `#33` (NeoGS added it; ZXM-GS has its own `#33`); SounDrive 1.05 modes 1 / 2 are a switch, not both; TurboSound chip select is `#FC-#FF` on the real board; MoonSound decodes the low byte | per-card claims; emulator deviations listed as follow-ups (not slot work) |
| 8 | Nemo IDE drives IORQGE on the whole `#06/#00` group outside DOS (hides `#FE` / `#7FFD` decodes) | a later card's declared claim |
| 9 | Known real conflicts: ZX-WiFi vs ZX-Evo / TS-Conf `#EF` (use the `#EE` build), MoonSound vs Profi `#7E`, DivIDE vs GS, DivIDE / DivMMC vs Beta 128 (`#3Dxx` traps), Multiface 1 vs Kempston and Beta, SMUC v1 on NemoBus without a mod | the matrix and the exceptions table (`refdata/exceptions.cpp`) |

## 2. Code inventory (master `8588b7e77`, ttd-engine `744a7fffb`)

### 2.1 Where cards are decided today

- **Config keys** (`core/src/emulator/config.cpp`): `[ISA]` 390-439, `[Beta128]` 494-507, `[INPUT]` 509-582, `[HDD]`
  584-620, `[SOUND] CovoxFB / CovoxDD / SD` 623-625, `MoonSound` 648-657, `[MOONSOUND]` 659-692, `TurboSound` 694-726,
  `GSType` 754-791, `GSRamSize` 801-829, `[NGS]` 832+, `[NETWORK]` 992-1080; `[ZC]` parsed by `MediaConfig`.
- **Creation:** `Core::Init` (`core/src/emulator/cpu/core.cpp:49`): sound 321, IDE 364, decoder 438-460, sound / Beta
  `attachToPorts` 470-479, `NetworkManager` 485. `SoundManager` ctor 78-187; `attachToPorts` 1807-1865 (GS deleted
  when `!ZxBusPresent()`).
- **Per-machine gates:** `ZxBusPresent` (only consumer `soundmanager.cpp:1827`), `HasKempstonJoystick`,
  `ReservesLowByte` (TS-Conf `#EF`), `DescribeNetwork`, `IsGsPort` (Pentagon gate; others rely on "no handler"),
  `CF_TRDOS` / `CF_DOSPORTS` gates, `OverrideDecodeForFullDecodeClaim` (`portdecoder.cpp:1518-1549`, exceptions for
  reads without `portDeviceClaimsRead` and for Beta-128 ports).
- **Runtime switches:** `SoundManager::requestGeneralSoundCardSwitch` (:1780, refused while recording) and
  `NetworkManager::RequestChange` (:630); five surfaces call each. Both become slot replaces applied by a restart (Q6).
- **Create-time options:** WebAPI `createEmulator` (`lifecycle_api.cpp:463-551`: `sprinter{isa_slot1/2,...}`,
  `profi{...}`), MCP `emulator_manage create`, CLI `--isa-slot1/2`; all through `configOverride`.
- **Shipped configs:** every `data/configs/*/unreal.ini` sets `GSType=NGS` and `Mouse=KEMPSTON`; TurboSound, Covox, SD,
  MoonSound and HDD differ per model (table in the SL-0 code report, reproduced in tdd.md when SL-4 converts them).

### 2.2 Port hot path

- Every IN / OUT runs `NotifyFullDecodeIn/Out`: a `std::map::find` on `_fullDecodeDevices`, which **no production code
  ever fills**, plus an index into the 256-entry low-byte array.
- `PeripheralPortIn/Out` do two RB-tree lookups (`key_exists` then `.at`) on about ten entries.
- Self-decoding dispatch runs only on Pentagon, Scorpion and ATM3.
- Benchmarks for A/B: `BM_PortIn` (`core/benchmarks/emulator/portin_benchmark.cpp`, 48K / 128K / Pentagon), the
  contention mix, `ScorpionPagingStorm`; **none covers OUT alone or a machine with a card fitted** - SL-2 adds them.

### 2.3 TTD

- Master registry: `std::unordered_map<uint8_t, TTDSerializable*>` keyed by `PeripheralId`; a duplicate **silently
  overwrites** (`ttdperipheralregistry.cpp:16`); file info carries a 64-bit peripheral mask.
- ttd-engine: device table keyed by `TTDDeviceKey{type, instance}`, duplicates refused; the per-id map still collapses
  duplicates before it. Configuration fingerprint (`CaptureConfigFingerprint`, `uint64` fields) excludes the device
  set by design; slots add `slots.<id>` hashes.
- Session guards on master: TurboSound slot, GS slot, `PortDecoder::TtdSessionMatches` (Sprinter only); ttd-engine
  predates the Sprinter ISA guard.
- Fixture corpus: Pentagon fixtures in `testdata/ttd/`, per-machine fixtures (TS-Conf, Sprinter recorded with the
  classic GS swapped in), port-journal fixtures; `TTD_Corpus_Test.EveryFixtureLoadsRestoresAndReplaysExactly` needs
  byte-identical blobs.

### 2.4 Restart path (Q6)

`ModelSwitch::Run` (`core/src/emulator/media/modelswitch.cpp:73-157`) re-creates the instance under the same id and
carries media (`TakeMediaSet` / `AdoptMediaSet`), but passes only `RamPowerOnOverride`: the old instance's create-time
override (Sprinter ISA, Profi options) and runtime changes are lost today. Extension: `ModelSwitchRequest` gains a
`configOverride`, same-model switches are allowed, and the instance's override is carried.
`MachineStateTransfer::TransferToNewInstance` (`machinestatetransfer.cpp:1086`) is the existing pattern for copying
device settings into a new instance.

### 2.5 Data files

`data/` subfolders are copied one by one in seven build / packaging places (root, unreal-qt, unreal-videowall,
testclient, Linux packaging, core tests, core benchmarks). Not needed for slots: the owner chose a reference data
collection in the code ([reference-data.md](reference-data.md) §1), no data files.

### 2.6 Tests a migration must keep green

Config (`config_test.cpp`: TurboSound kind, shipped configs fit NeoGS / IDE), port routing
(`fulldecodeclaim_test.cpp`, `portdecoder_portmap_test.cpp`, per-model decoder tests incl. GS-not-fitted fall-through
to the FDC), sound (`covox_test.cpp`, `soundmanager_test.cpp`, `tsfm_soundmanager_test.cpp`, MoonSound), device
state, IDE, network, Sprinter network (`ZxBusCardsAreRefusedWithTheReason`), joystick / mouse, snapshot transfer,
TTD (`ttdgeneralsoundswitch_test.cpp`, corpus), `core_golden_test.cpp` (`EveryModelRunsExactlyAsRecordedInFastAndDebugMode`).

## 3. Open items carried forward

From research-machines.md §18 and research-cards.md §10, the ones that matter for the first migration:

- the grey +2's IORQGE scope; whether the 128K AY sits on the CPU side of the ULA resistors;
- Profi: which ports `/OUTIORQ` masks, and the palette port (`#xx7E` vs "0FEH");
- Scorpion: +12 V on early boards, IORQGE resistor on early boards;
- the SounDrive 1.05 schematic (mode decode width);
- the real result of two drivers on one read (no source; modeling choice, flagged).

None of them blocks SL-1 / SL-2: each is a data value with a source field, updated when found.
