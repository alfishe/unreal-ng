# ATM710 / ATM3 (ZX-Evo BaseConf) Highres Video Modes and Port Differences

Date: 2026-09-15
Branch: `atm`
Status: implemented and covered by tests (see [verification-gaps-and-tests.md](verification-gaps-and-tests.md))

This folder documents the port of the ATM Turbo 2+ v7.10 ("ATM710") and ZX
Evolution BaseConf ("ATM3"/PentEvo) highres screen modes and their port-level
differences from xpeccy into the unreal-ng core, including every deviation we
found between the emulators and the actual hardware.

## Contents

| File | Topic |
|------|-------|
| [video-mode-decode.md](video-mode-decode.md) | FF77/EFF7 video mode decode (hierarchical, FPGA-verified), the xpeccy `<<1` bug, per-mode renderer details |
| [port-differences.md](port-differences.md) | Port-by-port comparison: #FF palette, #FE border, xx77 latches, #7FFD lock, #EFF7, #BE readbacks, turbo |
| [atm-video-crossanalysis.md](atm-video-crossanalysis.md) | Original Unreal Speccy atm.cpp cross-analysis: corrected FF77 bit table (video mode = `val & 7`), the default-OFF AtmMemSwap ini gate behind the physical A5-A7<->A8-A10 RAM permutation (removed from our port decoders after 2048.scl died on its mode 3->0 switch) |
| [verification-gaps-and-tests.md](verification-gaps-and-tests.md) | FPGA cross-verification log, test coverage matrix, known gaps and future work |

## Sources analyzed

| Source | Path | Role |
|--------|------|------|
| xpeccy-plus (fork) | `/Volumes/TB4-4Tb/Projects/emulators/github/xpeccy-plus/src/libxpeccy/` - `hardware/atm2.c` (ATM710), `hardware/pentevo.c` (ATM3), `video.c` (renderers) | primary emulator reference for this port |
| samstyle/Xpeccy (upstream master) | fetched raw `pentevo.c` | confirmed the mode-decode bug is upstream, not a fork regression |
| pentevo hardware project | `/Volumes/TB4-4Tb/Projects/emulators/github/pentevo/fpga/baseconf/trunk/` - `z80/zports.v`, `video/video_modedecode.v`, `video/video_palframe.v`, `top.v` | **ground truth** (the actual BaseConf FPGA RTL) |
| original Unreal 0.39.0 | `/Volumes/TB4-4Tb/Projects/emulators/github/pentevo/tools/unreal_fix/0.39.0/original/` | historical check: EFF7 was never wired into ATM3 mode detection in original Unreal |
| unrealspeccy / ZXMAK2 renderers | `other/unrealspeccy` `dxr_atm0/2/6.cpp`, `draw.cpp`; ZXMAK2 `EvoTxtRenderer`, `Atm640Renderer`, `UlaAtm450` | renderer geometry for EGA/HWM/TX/TL |
| prior raw analysis | [atm-video-crossanalysis.md](atm-video-crossanalysis.md), [../2026-09-10-atm-debugging/](../2026-09-10-atm-debugging/) | earlier investigation notes |

## Executive summary of findings

1. **xpeccy's ATM3 mode decode is buggy** - `evoSetVideoMode` composes
   `z5.z0` into bits 1/5 of a flat mode byte, but `z0` collides with an FF77
   mode bit, making its ALCO case (`0x13`) unreachable dead code. The bug is
   present in upstream samstyle/Xpeccy too. The FPGA does a **hierarchical**
   decode instead; unreal-ng implements the FPGA behavior
   ([video-mode-decode.md](video-mode-decode.md)).
2. **The #FF palette port is an active-low 2-bit-per-channel DAC** whose exact
   formula, cell pointer (the 4-bit FE border), write gates (pen2 / dos /
   manager) and readback all match the BaseConf RTL bit-for-bit
   ([port-differences.md](port-differences.md)).
3. **ATM attribute decode has paper-bright, not flash** in HWM/TX/TL
   (bit 6 = ink bright, bit 7 = PAPER bright); only the EFF7 z-mode HWMC has a
   flash bit. Our previous decode misread bit 7 as flash and dropped bright
   bits - fixed.
4. **ATM3 keeps the 312-line / 69888T frame in every video mode**; the ALCO /
   HWMC mode ids carry Pentagon-class 320-line timing descriptors, so three
   timing sites in `ScreenZX` override them on MM_ATM3.
5. **The #7FFD lock is conditional on EFF7 lockmem** on ATM3 (unlike plain
   Pentagon where bit 5 locks forever): writes are ignored only while
   `EFF7.2 && 7FFD.5` are both set.

## What changed in the codebase

| File | Change |
|------|--------|
| `core/src/emulator/emulatorcontext.cpp` | `EmulatorContext::InitAtmPalette()` - 16-cell palette RAM defaulting to the ZX colors |
| `core/src/emulator/platform.h` | `EmulatorState.atmPalette[16]`, `atmPaletteRegs[16]` (raw bytes for readback), `atmBorderBright`; removed the interim `FF77_ZX_ALCO`/`FF77_ZX_HWMC` flat-compose defines |
| `core/src/emulator/ports/models/portdecoder_atm710.h/.cpp` | `Port_ATM_Palette_Out` (DAC formula, cell = 4-bit border), partial `(port & 0x9F) == 0x9F` decode, pen2/dos gates, `atmBorderBright` latch in the #FE handler |
| `core/src/emulator/ports/models/portdecoder_atm3.h/.cpp` | exact `#FF` decode + manager gate, conditional #7FFD lock, EFF7 z-bit raster trigger, xx77 mode-change raster trigger, #BE.0B/0C/0D/0F readbacks |
| `core/src/emulator/video/screen.cpp` | `Screen::DetectModeATM3` rewritten as the hierarchical FPGA decode; `SetVideoMode` ATM3 ALCO/HWMC timing override; clut comment refreshed |
| `core/src/emulator/video/zx/screenzx.cpp` | `DrawATMMode`: palette routing (border + all pixels), paper-bright attr decode; new `DrawAlcoMode` (M_P16/M_PMC); `CreateTstateLUT` + `TransformTstateToFramebufferCoords` timing overrides; `RenderFrameBatch` dispatch for the new modes |
| `core/src/emulator/video/zx/screenzx.h` | declarations |
| `core/tests/emulator/video/atm_video_modes_suite_test.cpp` | 2 stale tests replaced, 6 added (palette port, FE bright cell, paper-bright, ALCO planes, HWMC, timing) |
| `core/tests/emulator/ports/models/portdecoder_atm710_test.cpp` | +4 palette port tests |
| `core/tests/emulator/ports/models/portdecoder_atm3_test.cpp` | +4 exact-decode / gate / lock / readback tests |

## Verification results (2026-09-15)

- Build (`ninja -C cmake-build-release core-tests`): clean, **zero warnings**.
- ATM-focused suites (`ATMVideoMode_Test`, `ATMVideoModesSuite_Test`,
  `PortDecoder_ATM710_Test`, `PortDecoder_ATM3_Test`, screen/timing tests):
  **73/73 pass**.
- Full `core-tests` run: exactly two failures, both verified pre-existing on
  the branch via `git stash` (i.e. they fail without these changes too):
  - `ATM710TrdosBoot_Test.MenuTRDOSBootsClassicTRDOS` (known)
  - `EmulatorManager_Test.CreateEmulatorWithNonCreatableModelReportsReason`
    (master-oriented test; ATM710 is creatable on the `atm` branch)

No commit has been made (per project policy: commit only on explicit request).
