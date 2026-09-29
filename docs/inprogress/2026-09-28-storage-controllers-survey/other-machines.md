# Other machines and interfaces: Kay, Quorum, Phoenix, GMX, LSY256, Karabas-Pro, and what was not found

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Summary** | the remaining Russian clones have **no storage of their own**: in every reference they take a board from the global IDE scheme list. Karabas-Pro is the one clone with several storage devices at once. ZXMMC, Kempston IDE, Velesoft ZX-IDE and similar interfaces appear in no local reference |

## 1. Clones without storage of their own

| Machine | unreal-ng model | Storage in the references | Source |
|---|---|---|---|
| Kay-1024 | `KAY`, not creatable | none specific; the global `[HDD] Scheme` applies | UnrealSpeccy `config.cpp:709-724` (one global scheme); zx-evo-unreal `io.cpp:677` (Kay only for `#1FFD` paging) |
| Quorum | `QUORUM`, not creatable | none | zx-evo-unreal `io.cpp:403, 1109` (paging, CP/M ports only) |
| ZXM-Phoenix | `PHOENIX`, not creatable | none specific (global scheme) | zx-evo-unreal `io.cpp:677`; Xpeccy `hardware/phoenix.c:58-69` (no storage ports) |
| Scorpion GMX | `GMX`, not creatable | none specific | zx-evo-unreal `io.cpp:534, 717` |
| LSY256 | `LSY256`, not creatable | none | zx-evo-unreal `io.cpp:176` |

(Paths in the local reference tree: `emulators/github/unreal-speccy`, `zx-evo-unreal/Unreal`,
`Xpeccy/src/libxpeccy`.)

Xpeccy does the same: its IDE is one global choice (Nemo, Nemo-A8, Nemo-Evo, SMUC, ATM, Profi;
`Xpeccy/src/xgui/options/setupwin.cpp:327-332`).

**unreal-ng consequence.** `IdeController::SchemeFits` (on master) already lets NEMO, NEMO-A8,
NEMO-DIVIDE and DIVIDE sit on any non-Profi model. When these clones become creatable (each needs a
port decoder first), their configs pick a scheme and nothing storage-specific is built. **Effort:
none** beyond the config line. Test: the 65 536-port collision sweep of `idecontroller_test` for the
new decoder.

## 2. Karabas-Pro (FPGA Profi clone)

Not a model in unreal-ng; its ROMs (ROMain, PQ-DOS) run on the `PROFI` model. It is the only clone
with four storage paths at once:

| Device | Ports and gate | Source |
|---|---|---|
| Profi IDE (a CF card) | Profi ports `#xx8B/AB/CB/EB`; answers in EXT mode **or** (DOS latch on and ROM14 = 0), unless `#028B` bit 0 turns the HDD off | karabas-pro `firmware/src/fpga/profi/rtl/karabas_pro.vhd:1357-1360, 1420-1426` |
| Nemo IDE mode (same CF card) | `#028B` bit 1 switches the board to Nemo ports (`#10`...`#F0`, `#C8`, `#11`), only while CP/M is off; the keyboard and `#7FFD` decode tighten while it is on | `karabas_pro.vhd:1387-1397, 1440-1461` |
| Z-Controller SD | `#57` / `#77` | `karabas_pro.vhd:1687-1707` |
| DivMMC | `#E3` / `#E7` / `#EB` with automap; `#EB` collides with the Profi IDE family when CP/M is off; ROM page forced to 48K while DivMMC is on | `profi/rtl/sd/divmmc.vhd:61-62, 91, 130-144`; [karabas-pro-hardware-analysis.md](../2026-09-21-profi/karabas-pro-hardware-analysis.md) §1.8, §2.6 |

Firmware in the Karabas tree: `firmware/src/fpga/profi/rom/` holds `bios_pqdos.rom`,
`profi_mainrom_*.rom`, `full_divmmc.rom`, `esxdos08x.bin`; software trees `software/profi/isdos_nemoide`,
`pq-dos`, `fatfs`.

**unreal-ng consequence.** The IDE design already decided that the Karabas gate is a whole-decoder
variant, not an IDE tweak ([IDE design](../2026-09-21-profi/2026-09-25-ide-hdd-design.md) §3.2, open
question Q2). A "Karabas-Pro personality" of the Profi decoder would combine: the wider EXT gate,
`#028B` (HDD off, Profi / Nemo switch), the Z-Controller add-on and DivMMC. **Effort: M**, after
DivMMC exists. Value: the Karabas BIOS HDD self-test, esxDOS on a Profi, ISDOS on Nemo IDE.

## 3. Interfaces searched for and not found in any local reference

| Interface | Result |
|---|---|
| ZXMMC / ZXMMC+ (`#1F` / `#3F` / `#7F` SPI) | not implemented anywhere in the local tree |
| Kempston IDE, Velesoft ZX-IDE, Pera Putnik 8-bit IDE, Spectrum SE, Timex | not implemented (Velesoft appears only as a documentation credit in UnrealSpeccy's news file) |
| MB-02+ | implemented by pico-spec (`src/MB02.cpp`): WD2797 floppy, Z80-DMA, 512 KB SRAM, BS-DOS; **no IDE**, so outside this survey |
| Fuse (ZXATASP, ZXCF, simple 8-bit IDE, DivIDE, DivMMC, ZXMMC) | not in the local tree; see [plus3e-cf.md](plus3e-cf.md) §2 for what it would verify |

None of these has software in `testdata/` or a user request behind it. They stay out of the plan
until one of them does.
