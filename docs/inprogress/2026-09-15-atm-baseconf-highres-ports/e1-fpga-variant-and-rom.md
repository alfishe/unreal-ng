# Phase E1 — FPGA variant switch, `#xxBD` register block, official ROM image

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done. Plan: [implementation-plan.md](implementation-plan.md) phase E1 |
| **Gaps closed** | C-1, C-9, R-1 of [gap-analysis.md](gap-analysis.md); C-2 plumbing (the `#xxBE` exit strobe still only latches until NMI lands in E3); `[EVO]` part of R-2 |

## 1. What changed

| Area | Change | Evidence |
|---|---|---|
| Config | `[EVO] Fpga=trdemu\|legacy` (default `trdemu`, unknown values warn and fall back to `trdemu`) → `CONFIG::atm.evo_legacy_fpga` | design D1 in [tdd-evo-control-and-avr.md](tdd-evo-control-and-avr.md) |
| Register block | One table, `PortDecoder_ATM3::ReadEvoRegister(index)`, index = A12..A8 (A15..A13 ignored): `00-07` pages as written to `#x7F7`, `08` RAM bits, `09` dos7ffd bits, `0A` `#7FFD`, `0B` `#EFF7`, `0C` `#xx77` state with the **live DOS bit** in bit 4 (was `pFF77` bit 4), `0D` palette, `0F` border, `10/11` breakpoint, `12` write-protect bits (0 until E8), `13` virtual-drive mask (current tree only), `0E` and undefined → `#FF` | `fpga/base_trdemu/trunk/z80/zports.v` portbdmux :955-994; legacy `fpga/baseconf/trunk/z80/zports.v` portbemux :893-929 |
| Ports, current tree | `#xxBD` reads the table; `#10BD/#11BD` write the breakpoint (A12..A9 = 8, byte by A8); `#13BD` writes the 4-bit mask (reset 0); `#xxBE` has no read path (`#FF`) | zports.v :470-472, :504-525, git `663b8cf2` "removed completely xxBE read ports" |
| Ports, legacy tree | `#xxBE` reads the table (indices `00-12`); any `#xxBD` write sets the breakpoint byte selected by A8; `#xxBD` reads `#FF` | baseconf zports.v :473-487 |
| `#xxBF` read | only the defined bits: `& #3F` current, `& #1F` legacy | zports.v :466-468 |
| State / TTD | `EmulatorState::evoFddMask`; `AtmPagingState.evoFddMask` takes one reserved byte (blob stays 128 bytes) | — |
| ROM | `data/rom/zxevo-fe.rom` = official pentevo `rom/zxevo_fe.rom` (md5 `6e290020…`, ERS 0.60.05 FE, NEO-DOS page 29); `configs/atm3/unreal.ini` → `ATM3=rom/zxevo-fe.rom`, `[EVO] Fpga=trdemu`. `rom/zxevo.rom` stays for `Fpga=legacy` and TS-Conf | `data/rom/README-ROMS.md` |
| Port map | `#xxBD` / `#xxBE` rows describe the selected variant | — |
| Recipe | `.recipe/machines/atm.md` ATM3 section rewritten (shadow rules, CMOS gate, `#xxBD`, ROM) | — |

## 2. How the official image behaves in the emulator (measured)

- The EVO Reset Service boots to its main menu: the menu is unpacked to `#6000` and idles in a HALT
  loop at **PC `#6117`**, window 0 on BASIC48 ROM page 28, about 60 frames after reset.
- At frame 52 the ERS runs its FPGA suitability probe (`rst8service.a80` `VERSION_`):
  `IN (#13BD)` → `#00`, `OUT (#13BD),#0A`, `IN (#13BD)` → **`#0A`**, `OUT (#13BD),#00`. The read-back
  matches, so the ERS does not show "Incorrect FPGA zxevo_fw.bin".
- After that, every RST8/NMI service entry saves and zeroes `#13BD` and every exit writes it back
  (PC `#0246/#024C/#02F2`). With the current all-zero NVRAM the ERS sets the mask to `01`: drive A is
  its RAM disk (CMOS `#EB` bits 1..0 = 0). This is exactly the source-level description in
  [baseconf-hardware-reference.md](baseconf-hardware-reference.md) §C 3.1.
- The same image on `Fpga=legacy` stops before the probe in this emulator. Whether a real board with
  the legacy firmware gets further is not verified, so no test asserts it.

## 3. Tests

| Test | What it pins |
|---|---|
| `ConfigEvoFpga_Test.ParsesVariantNamesCaseInsensitive`, `.MissingOrUnknownMeansCurrentTrdemu`, `Config_Test.EvoFpgaKeyReachesConfig` | config parsing, default, no prefix match |
| `PortDecoder_ATM3_Test.PortBD_ReadbackRegisters_TrdemuFpga` | `#xxBD` reads, `#xxBE` does not, A15..A13 ignored |
| `PortDecoder_ATM3_Test.PortBE_ReadbackRegisters_LegacyFpga` | legacy readback on `#xxBE`, `#xxBD` write-only, no index `13` |
| `PortDecoder_ATM3_Test.EvoRegisterTable_AllIndices` | every index, incl. the live DOS bit in `0C` |
| `PortDecoder_ATM3_Test.BreakpointAddressWrites_BothTrees` | both write decodes |
| `PortDecoder_ATM3_Test.FddMask13BD_ReadWriteResetAndLegacyAbsent` | 4-bit mask, reset, absent on legacy |
| `PortDecoder_ATM3_Test.PortBF_ReadbackMasksUndefinedBits` | `#xxBF` read masks |
| `TtdAtmPaging_*` (updated) | `evoFddMask` in layout, round trip, hash |
| `ZXEvoErs_Test.BootsToMainMenu` (new file `zxevo_ers_test.cpp`) | real ROM: menu idle loop reached |
| `ZXEvoErs_Test.FpgaSuitabilityProbePassesOnTrdemu` | real ROM: the `#13BD` probe reads back `%1010` (port trace) |
| `EmulatorManager_Test.CreateZXEvo_BootsBaseConfRomSet` (updated) | shipped config: official image, NEO-DOS slot, ERS pages 22/24 |
| `ZXEvoBoot_Test.*` (updated) | the older `rom/zxevo.rom` still boots on `Fpga=legacy` |

Tests changed because the default image changed (the facts they check moved, nothing was relaxed):

| Test | Change |
|---|---|
| `ROMControlPoints_Test.Atm3` | checks the pages ATM3 actually boots (28 BASIC48, 29 NEO-DOS = TR-DOS family, 30 BASIC128, 31 ERS) instead of pages 0-3, which BaseConf never boots and which are empty in the official image. All control points match on the official image |
| `TTD_Page255_Test` (fixture) | banks ROM page 28 instead of page 0 into window 0 to find a `RET` |

## 4. Verification run (2026-09-28)

- Isolated worktree = HEAD `e4d4b2fb` + only the E1 files: `core-tests` **3924 passed, 0 failed**, zero
  compiler warnings.
- Full-tree build (all targets incl. `unreal-qt`): zero compiler warnings.
- In the shared working tree `TtdAtmPaging_Test.SeekRestoresMemoryMapAndResultingBanks` fails; it passes
  in the isolated worktree, so the cause is another session's uncommitted TTD restore work, not E1.
