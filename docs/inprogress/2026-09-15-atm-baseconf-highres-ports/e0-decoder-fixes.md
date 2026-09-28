# Phase E0 — ZX-Evo BaseConf decoder fixes: what changed and how it was verified

| | |
|---|---|
| **Date** | 2026-09-28 |
| **Status** | Done. Plan: [implementation-plan.md](implementation-plan.md) phase E0 |
| **Gaps closed** | P-1, P-2, P-3, P-4, P-6, P-7 (confirmed already correct), P-9, P-10 of [gap-analysis.md](gap-analysis.md) |
| **Moved** | P-5 (`#xBF7` write protect) → phase E8: it needs a write-protect path in `Memory` that keeps the TTD write journal honest, the same path flash writes need |

## 1. The change in one paragraph

`PortDecoder_ATM3` no longer inherits the ATM Turbo 2+ decode. A new `ClassifyPort()` assigns every
I/O cycle to exactly one board function (`PortArm`), following the released BaseConf FPGA
(`pentevo/fpga/base_trdemu/trunk/z80/zports.v`, porthit list :331-359). `DecodePortIn/Out` dispatch
on that one value. Every mainboard port now decodes the full low byte, and the shadow line (TR-DOS
active or `#BF` bit 0) switches the FDC / ATM group in and the joystick / Z-Controller / `#EFF7`
group out, exactly as on the board. The window mapping moved into `PortDecoder_ATM3::updateMemoryBanks()`
with the BaseConf pager rules.

## 2. Per fix: hardware, other emulators, test

Emulator abbreviations as in [emulator-feature-matrix.md](emulator-feature-matrix.md): **U**
zx-evo-unreal, **N** nedopc / Unreal_NS, **X+** xpeccy-plus, **Z** ZXMAK2 / kozynax, **M** MAME.

| Fix | Hardware (RTL) | Other emulators | Verdict | Test |
|---|---|---|---|---|
| `#FE` exact low byte; `#F6` = border 8-15 without beeper; `#FC` = border (+ `#7FFD` when A15 = 0) | `zports.v:331-334, 533-538, 944` | U, N, X+, M decode `#FE` on the low byte; none models `#F6`'s missing beeper or `#FC` | RTL | `Sweep_EveryPortMatchesFpgaPortHit`, `BorderPortsF6AndFC` |
| NemoIDE ports no longer reach the border | same sweep: `#10…#F0/#11/#C8/#x8` are board ports | all emulators with IDE | fixed (IDE itself is E6) | `BoardPortsReservedForLaterPhases` |
| FDC only in shadow, exact low bytes `#1F/#3F/#5F/#7F/#FF` | `zports.v:342, 797-799` | U, N, X+, M: shadow-only; Z: `DOSEN‖SYSEN` with mask `0x9F` | RTL (consensus) | `Fdc_OnlyInShadow_JoystickOutside` |
| Kempston joystick `#1F` outside shadow | `zports.v:344, 444-445` | U: any A5 = 0 port; M: returns 0 (TODO); Z: none | RTL; no joystick model yet, so it reads `#00` (same stub as Scorpion) | same test |
| Kempston mouse `#xxDF`, A8/A10 sub-decode, not TR-DOS gated | `zports.v:446-447`, `zkbdmus.v:118-120` | U, N, X+, M decode it; Z hides it in DOS (differs) | RTL | `KempstonMouse_Decoded` |
| Covox `#FB` reaches the Covox device | `zports.v:945` | U, N, X+, M, Z | fixed (was dead config on ATM3) | `Covox_FbReachesSelfDecodingDevice` |
| `#7FFD` = low byte `#FD`/`#FC` with A15 = 0 (`#1FFD` is an alias; no +3 port) | `zports.v:484, 694-710` | U: A15/A1 partial; M, X+: `#FD` low byte | RTL | `Port7FFD_FullLowByteDecode` |
| AY = low byte `#FD` with A15:A14 = 11 / 10 | `zports.v:663-684` | all | RTL | same |
| `#EFF7` written only outside shadow, on any `#F7` with A8 = 1 and A12 = 0; write-only | `zports.v:490-491, 714-720, 424-482` (no read mux entry) | U: exact port, outside shadow; Z: exact, not gated (differs); M: write-only | RTL. The ERS writes it with shadow off (`services.a80` `RAM_CODE`) | `Eff7_WrittenOnlyOutsideShadow_WriteOnly` |
| Gluk clock `#DFF7/#BFF7` need `#EFF7` bit 7 outside shadow; `#DEF7/#BEF7` in shadow always | `zports.v:455-460, 739-750` | U: always on (differs); Z, M: honor bit 7 | RTL | `Gluk_GatedByEff7Bit7OutsideShadow` |
| Pager ports decode A8 + A11:A10 only (`#xFF7` 11, `#x7F7` 01); A13:A12 free | `atm_pager.v:180-214` | U: mask `0x0FFF`; M: `0x3FFF`; Z: `0x37FF` | RTL (emulators disagree) | `IsPort_FFF7_PagerDecode`, `IsPort_37F7` |
| 1 MB mode page = `{reg[7:6], 7FFD[7:5], 7FFD[2:0]}`; 128K mode `{reg[7:3], 7FFD[2:0]}` | `atm_pager.v:147-156` | U, M, N agree; Z permutes the bits (differs) | RTL (consensus) | `Mapping_7FFDPageBits_1MegVs128KMode` |
| `#EFF7` bit 3 = RAM page 0 at `#0000`; pager-off still wins | `atm_pager.v:114-137` | U, Z correct; M maps ROM 0 (bug) | RTL (consensus) | `Mapping_Eff7Bit3_Ram0AtWindow0` |
| `#77` read outside shadow = `#00` | `zports.v:449-450` | U, N, M | RTL | `ZController_ConfigReadsZero_DataIdle` |
| Reset at 7 MHz | `top.v:401`, `zclock.v` | M 7 MHz; U 3.5 MHz | already correct in our code; now pinned by a test | `Reset_Runs7MHz` |
| TTD: palette, palette registers, border-bright bit, the live CMOS address latch | — | no reference emulator has time travel | fixed; `AtmPagingState` 44 → 128 bytes (no ATM recordings in the `.ttd` corpus) | `Atm3RoundTripCarriesPaletteBorderAndLiveCmosLatch`, hash test |
| Memory API/CLI ROM page count; port-trace model name; ATM3 port-map rows | — | — | fixed | existing API tests + port-map test |

## 3. Tests changed because the hardware says so

| Test | Old expectation | Why it changed |
|---|---|---|
| `PortDecoder_ATM3_Test.IsPort_37F7` | `#17F7` is not `#x7F7` | the board decodes A11:A10 only |
| `PortDecoder_ATM3_Test.IsPort_FFF7_NarrowerDecode` → `IsPort_FFF7_PagerDecode` | Unreal's `0x3FFF` mask | same |
| `PortDecoder_ATM3_Test.InheritsPort_7FFD` → `Port7FFD_FullLowByteDecode` | `#7FF5` pages memory | BaseConf needs low byte `#FD`/`#FC` |
| `PortDecoder_ATM3_Test.Turbo_FF77Bit3_EFF7Bit4_MultiplierSelect` | `#EFF7` written in shadow | `#EFF7` is ignored in shadow |
| `ATMVideoModesSuite_Test.PortEFF7_ControlBitsStored_ZBitsTriggerRedetection` | same | same |

## 4. Verification run (2026-09-28)

- `ninja -C cmake-build-agent-release`: builds; **zero compiler warnings** (the only warnings are the
  pre-existing Homebrew "built for newer macOS" linker notices).
- `core-tests`, 4 shards: **3881 passed, 0 failed** (116 skipped: fixture-absent tests, unchanged).
- Real-ROM tests on the current image still pass: `ZXEvoBoot_Test.BootsToInteractiveServiceShell`,
  `ZXEvoBoot_Test.MenuKeyUBoots128KBasic`, `EmulatorManager_Test.CreateZXEvo_BootsBaseConfRomSet`.

## 5. Noticed, not changed

- **No floating bus on the Evo:** the FPGA drives `#FF` for every IN nobody answers (`zbus.v:50`),
  so a port-read of an unclaimed address is always `#FF`. Our Z80 applies the ULA floating-bus value
  to undecoded ports, but `data/configs/atm3/unreal.ini` ships `FloatBus=0` (also the parser default),
  so ATM3 already reads `#FF`. Only a user config with `FloatBus=1` would differ. Recorded as gap
  P-12 (Low).
