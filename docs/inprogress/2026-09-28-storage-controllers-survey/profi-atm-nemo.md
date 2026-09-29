# Profi, ATM Turbo 2+, Pentagon + Nemo / Nemo-A8 / Nemo-DivIDE (and SMUC in one row)

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Designs** | [IDE design](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) §3 (Profi), §4 (all boards side by side), §9 (Profi pseudocode); implementation plan `docs/inprogress/2026-09-28-ide-atapi/implementation-plan.md` (D1-D9, §5 as built) |
| **State** | all five boards are decoded by one `IdeAdapter`, on master since 2026-09-29 (`f5fc5f05`) |
| **Effort left** | **S** each: merge, the tests below that do not exist yet, config polish |

These boards are all "8-bit Z80 bus to a 16-bit IDE drive" adapters. They differ in only three
things: which ports reach which ATA register, how the 16-bit data word is split into two bytes (the
**latch pattern**), and when the board answers (the **gate**). Everything behind that - the drive,
its commands, the images, the slots - is one shared core.

## 1. Consensus port tables

Sources: UnrealSpeccy `io.cpp` / `hdd.cpp` (pentevo SVN `tools/unreal_fix/0.39.0/nedopc/`), ZXMAK2
`Hardware/*/Ide*.cs`, Xpeccy `libxpeccy/hdd.c`, MAME `bus/spectrum/zxbus/{nemoide,smuc}.cpp`,
pico-spec `Ports.cpp` / `IDE.cpp`, Karabas-Pro RTL, ZX-Evo RTL. The per-source comparison with line
numbers is in the IDE design §3.7 and §4; this table is the consensus the code implements.

| Board | Ports (mask) | Register from | CS1 (control block) | High byte | Gate | INTRQ |
|---|---|---|---|---|---|---|
| **Profi** | `(p & #9F) = #8B`: `#xxCB` / `#xxEB` | A10..A8 | write `#06AB` only | two latches, **mirror roles**: read `#CB` register + `#EB` latch; write `#CB` latch + `#EB` register | Profi EXT mode (`#DFFD.5` and `#7FFD.4`) | not wired |
| **Nemo** (Pentagon) | A2 = A1 = 0, CS0 when A4 A3 = 10: `#10 #30 ... #F0` | A7..A5 | `#C8` = register 6 | latch port `#11` (A0 = 1): read `#10` then `#11`; write `#11` then `#10` | TR-DOS ports **off** | not wired |
| **Nemo-A8** | as Nemo | A7..A5 | `#C8` | latch at A8 = 1 (`#110`) | TR-DOS ports off | not wired |
| **Nemo-DivIDE** (and ZX-Evo) | as Nemo + RTL aliases | A7..A5 | `#C8` | Nemo latch **or** two `#10` accesses (low, then high) | always (Evo) | not wired |
| **ATM Turbo 2+** | `(p & #1F) = #0F`: `#xx0F ... #xxEF` | A7..A5 | none | latch at A8 = 1 (`#FF0F`), Nemo order | TR-DOS ports **on** | **yes**: bit 6 of the `#7FFD`-class read `(p & #8202) = #0200` |
| SMUC (Scorpion) | `#F8BE-#FFBE`, latch `#D8BE` | A10..A8 | `#FFBA` bit 7 turns `#FEBE` into the control block | latch, Nemo order | TR-DOS ports on | ZXMAK2 only |

SMUC is covered in depth by the sibling survey `docs/inprogress/2026-09-28-scorpion-smuc/` (being
written in parallel).

**Worked example: the Profi mirror latch (the trap).** To read one word `#1234`:
`IN A,(#00CB)` returns `#34` and latches `#12`; `IN A,(#00EB)` returns `#12`. To write `#ABCD`:
`OUT (#00CB),#AB` only fills the write latch; `OUT (#00EB),#CD` sends `#ABCD`. A decoder that treats
"A5 = high byte" the same in both directions gets exactly one direction wrong (Xpeccy fixes it with
`if (wr) port ^= 0x20`, `Xpeccy/src/libxpeccy/hdd.c:791`).

**Worked example: the ATM INTRQ bit.** After `OUT (#FFEF),#20` (READ SECTORS on the command
register, A7..A5 = 7) the drive raises INTRQ when the sector is ready. `IN A,(#7FFD)` then has bit 6 =
0; bits 5..0 are `#3F`. The ROM polls this read instead of the status register
(`IdeAdapter::AtmIntrqBit`, UnrealSpeccy `read_intrq`).

### 1.1 Where the references disagree

| Point | References | unreal-ng |
|---|---|---|
| Nemo latch port | UnrealSpeccy: any A0 = 1 with A1 = A2 = 0 (`nedopc/io.cpp:467-484, 1114-1125`); MAME `zxbus/nemoide.cpp:41-66` and the Evo FPGA: exactly `#11` | the UnrealSpeccy decode |
| Nemo `#C8` | CS1 register 6 everywhere except Xpeccy, which maps it to the task-file head register (a bug, `hdd.c:697-715`) | CS1 |
| Nemo read of a non-latch port | UnrealSpeccy sets the latch to `#FF` first (`io.cpp:1119`) | same |
| Profi gate | UnrealSpeccy: EXT only; Karabas-Pro RTL and pico-spec (`Ports.cpp:863-873`): EXT **or** (DOS and not ROM14), which the SYS-ROM HDD probe of the Karabas BIOS needs | EXT only (IDE design §3.2, open question Q2) |
| Profi latch port | any `#xxEB` / `#xxCB` (UnrealSpeccy, RTL); Xpeccy only `#00EB`, which misses the ROM's `#FFEB` | any |
| ATM latch | UnrealSpeccy: any A8 = 1 `#x0F`; Xpeccy only `#010F`; MAME `atm.cpp:398-402` mirrors `#010F` | any A8 = 1 |
| ATM control block | not reachable in UnrealSpeccy or MAME (no CS1): no SRST on ATM | none |
| INTRQ clear | UnrealSpeccy clears INTRQ on an alternate-status read too (`hdd.cpp:264-267`), against the ATA standard; Xpeccy only on a status read (`hdd.c:398-405`) | status read only |
| `read_intrq` with no drive | UnrealSpeccy returns "asserted" when no unit / nIEN (`hdd.cpp:247-248`) | same (`AtmIntrqBit`) |

## 2. Reset and gating details that matter

| Board | Machine reset | Soft reset (SRST) | Notes |
|---|---|---|---|
| Profi | clears both latches, hard-resets the drives (UnrealSpeccy `z80.cpp:174-177`; RTL `ide_controller.vhd:172`) | `#06AB` bit 2 | ZXMAK2 does not reset (a bug) |
| Nemo family | hard reset, latches cleared | `#C8` bit 2 | the Nemo gate is "TR-DOS ports off": TR-DOS code cannot reach the disk, so DOS drivers page TR-DOS out first |
| ATM | hard reset | none (no CS1) | the ATM gate is the opposite of Nemo: the ports answer only with TR-DOS ports on (TR-DOS active or ATM3 shadow) |
| SMUC | hard reset; `#FFBA` bit 0 resets the drives | via `#FEBE` with `#FFBA.7` | see sibling survey |

## 3. unreal-ng now vs gap

| Piece | On master (IDE rollout 1) | Gap |
|---|---|---|
| Decode and latches | `IdeAdapter` (`core/src/emulator/io/ide/ideadapter.{h,cpp}`): `ProfiIn/Out`, `NemoIn/Out` (A0 or A8 latch), `EvoIn/Out`, `AtmIn/Out` + `AtmIntrqBit`, `SmucIn/Out`; POD `IdeAdapterState` | none |
| Gate | `IdeAdapter::Gate { dosPorts, profiExt }` from the base `PortDecoder` (`IdeGate()`), IDE decoded before the model's rules (UnrealSpeccy `io.cpp` order) | none |
| Board per model | `[HDD] Scheme`, validated by `IdeController::SchemeFits`: Profi `PROFI`, ATM710 `ATM`, ZX-Evo `NEMO-DIVIDE`, Pentagon 128/512 `NEMO`, Scorpion `NONE` (SMUC is an add-on) | Pentagon 1024 has a decoder (`portdecoder_pentagon1024`) but no config folder of its own: **to check** which `[HDD] Scheme` it ends up with |
| Disk, CD, formats, slots | shared core (`ata/`), `HddImageFormats`, `IdeUnitSlot` | none |
| TTD | `AtaChannel = 17` | none |
| Tests | `ideadapter_test` (every scheme), `profi_hdd_test` (SYS ROM loader, skipped without the ROM), `zxevo_ers_test`, `scorpionsmuc_test` | real-firmware test for **ATM** (no ATM HDD boot test yet) and for **Pentagon Nemo** (NedoOS or a Nemo-driver program) |

## 4. Software to test with

| Board | Software | Where |
|---|---|---|
| Profi | SYS ROM HDD loader (`#28CE`), Karabas-Pro BIOS HDD self-test | `testdata/machines/profi/rom/profi_mainrom_standart.rom`; ZXMAK2 `ROMS/PROFI/profi-hddboot.rom` (reference) |
| Profi | PQ-DOS on a disk image | karabas-pro `software/profi/pq-dos/pqdos1.img` (reference tree) |
| ATM Turbo 2+ | ATM CP/M HDD driver (`hddrv.a80`, image `cpm.img`), NedoOS `osatm2hd.trd` | `pentevo/rom/atm_cpm/source/` and `NedoOS/release/` (reference tree); `data/rom/atm2.rom`, `glukatm.rom` |
| Nemo (Pentagon) | NedoOS (its FatFS driver probes Nemo master / slave, `NedoOS/src/fatfs4os/savelij.asm:22-50, 239-305, 347-394`), raw image `NedoOS/tools/vhd/hdd_nedo.vhd.xz`, Wild Commander | reference tree; `data/rom/glukpen.rom` |
| ZX-Evo | ERS HDD boot, CD boot, NedoOS | done (`zxevo_ers_test`) |

## 5. Acceptance tests still to add

1. **ATM-HDD-1**: `glukatm.rom` (or the ATM CP/M boot) finds a drive; the `#7FFD` bit 6 poll ends.
2. **NEMO-HDD-1**: NedoOS Pentagon build boots from a FAT image in `ide0.master` on Pentagon 128 with
   `Scheme=NEMO`; the same image under `NEMO-A8` fails at the first word read (proves the latch
   choice matters).
3. **Collision sweep** per machine: all 65 536 ports, IDE on and off, nothing else shadowed (exists
   in `idecontroller_test`; extend to Pentagon 1024).

## 6. Order

1. ~~Land `ide-atapi` on master (closes #13a IDE, #55 E6/E7, #58 M6).~~ Done: `f5fc5f05`, merged in `c69486ab`.
2. Add ATM-HDD-1 and NEMO-HDD-1 (fixtures from NedoOS / Gluk ROMs).
3. SMUC per the sibling survey.
