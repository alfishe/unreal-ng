# FPGA Verification, Test Coverage, and Known Gaps

## FPGA cross-verification log

Every port behavior in this port was checked against the BaseConf RTL at
`/Volumes/TB4-4Tb/Projects/emulators/github/pentevo/fpga/baseconf/trunk/`
(the ZX Evolution hardware project itself). Findings:

| Behavior | RTL evidence | Result |
|----------|--------------|--------|
| FE border bright = NOT A3, latched per write | `z80/zports.v:500` `border <= {~a[3], din[2:0]}` | matches implementation bit-for-bit |
| xx77 latches: mode = data[2:0], PEN = ~A8, CPM_N = A9, PEN2 = ~A14 | `zports.v:840-858` | matches (`aFF77` bits 8/9/14) |
| palette write blocked when pen2 inactive | `zports.v:857/867` `atm_palwr = vg_wrFF_fclk & atm_pen2` | matches |
| palette data = 6 inverted channel bits, GgRrBb | `zports.v:869` `atm_paldata = {~din[4],~din[7],~din[1],~din[6],~din[0],~din[5]}` | matches the DAC formula |
| palette cell pointer = 4-bit border | `video/video_palframe.v:98` `pal_addr = atm_palwr ? {5'd0, zxcolor} : ...` | matches |
| BE.0B = raw EFF7 | `zports.v:896-913` `portbemux` 5'hB = `peff7_int` | matches |
| BE.0C layout (mode/turbo/dos/pen/cpm/pen2) | `portbemux` 5'hC = `{~atm_pen2, atm_cpm_n, ~atm_pen, dos, atm_turbo, atm_scr_mode}` | matches |
| BE.0D = `(raw & 0xF3) \| 0x0C` | full write (`video_palframe.v:100`) -> store -> readback (`:107`, 5'hD mux) round trip decoded | matches exactly |
| BE.0F = 4-bit border | `portbemux` 5'hF = `{4'bXXXX, border}` | matches |
| hierarchical video decode, z-bits only over ZX base | `video/video_modedecode.v` + `top.v` `.pent_vmode({peff7[0],peff7[5]})`, `.atm_vmode(atm_scr_mode)` | implemented instead of xpeccy's buggy flat compose |
| hardware reset: atm_pen2 = 0 (palette blocked until first xx77) | zports.v reset defaults | documented divergence (unreal-ng cold gate open) |
| EFF7 readback masking while block1m set | `zports.v:688` | documented divergence (raw readback, same as xpeccy) |

Additional emulator cross-checks:

- The `<<1` mode-compose bug is byte-identical in xpeccy-plus and upstream
  samstyle/Xpeccy `pentevo.c` (upstream bug; its own comment contradicts the
  code, and its `case 0x13` is unreachable dead code).
- Original Unreal 0.39.0 (`tools/unreal_fix/0.39.0/original/` in the pentevo
  repo) uses EFF7 only for Pentagon 384/HWMC attribute tables - never for
  ATM3 mode detection, so it offers no third opinion; the FPGA settles it.

## Test coverage matrix

All tests in `core/tests/`; build config `-DTESTS=ON`. New/updated tests added
by this port:

### `emulator/video/atm_video_modes_suite_test.cpp` (ATMVideoModesSuite_Test)

| Test | Verifies |
|------|----------|
| `ModeMatrix_ATM3_EFF7ZBits_HierarchicalDecode` | 10-case matrix: z-bits only act over FF77 mode 3; extended modes ignore them; z0+z5 and undefined FF77 modes fall back |
| `PortEFF7_ControlBitsStored_ZBitsTriggerRedetection` | control-only EFF7 writes store without raster re-init; z-bit changes re-init |
| `Render_ATM16_PalettePortProgramsColorsAndBorder` | `OUT (#9F),0x55` reprograms cell 0 to `0xFF5555AA`; EGA pixels and the border render through it |
| `Border_FEAddressBit3_SelectsBrightPaletteCell` | `#FE` -> dim cell, `#F6` -> bright cell of the same color (A3 inverted) |
| `Render_ATMHR_AttributeBit7IsPaperBright_NoFlash` | HWM bit 7 brightens PAPER; the flash phase does not swap anything |
| `Render_ATM3Alco_FourPlanes_PagePair` | AlCo planes at `{vid^1, vid} x {+0,+0x2000}` and nibble packing |
| `Render_ATM3Hwmc_AttrFromPixelAddress_FlashBit` | HWMC attr from the pixel byte, bit 6 brights both, bit 7 inverts on flash phase |
| `Timing_ATM3ZModes_KeepAtm312LineFrame` | beam mapping of z-modes stays inside the 312-line/69888T ATM3 frame |
| (updated) renderer geometry / batch-equivalence tests | cover the palette routing and paper-bright decode changes in EGA/HWM/TX/TL |

### `emulator/ports/models/portdecoder_atm710_test.cpp` (PortDecoder_ATM710_Test)

| Test | Verifies |
|------|----------|
| `IsPort_ATM_Palette_0x9FGroupPartialDecode` | `(port & 0x9F) == 0x9F` aliases `9F/BF/DF/FF`; `FE/9E/B7...` rejected |
| `PaletteFF_WriteFormula_ActiveLowDAC` | `0x00` -> white, `0xFF` -> black, `0x55` -> `0xFF5555AA`; raw byte kept in `atmPaletteRegs` |
| `PaletteFF_CellIsTheFourBitBorder` | write lands in the cell pointed at by border + bright |
| `PaletteFF_WriteGates_Pen2AndDosLine` | DOS-gate and `aFF77.PEN2` block the write |

### `emulator/ports/models/portdecoder_atm3_test.cpp` (PortDecoder_ATM3_Test)

| Test | Verifies |
|------|----------|
| `IsPort_ATM_Palette_ExactFFDecode` | exact `#FF` only (`9F/BF/DF/FE` rejected - unlike ATM710) |
| `PaletteFF_ManagerGate` | manager gate: open after reset, closed by `CPM\|PEN2`, reopened by `BF.0 = 1` |
| `Port_7FFD_LockOnlyWithEFF7Lockmem` | 7FFD.5 alone does not lock; `EFF7.2 && 7FFD.5` blocks; clearing EFF7.2 re-opens |
| `PortBE_ReadbackRegisters` | BE.0B (raw EFF7), BE.0D (`(raw & 0xF3) \| 0x0C` of the border cell), BE.0F (4-bit border) |

## Known gaps and future work

Ordered by likely impact:

1. **ATM710 #FF readback** (`atm2InFF`, `atm2.c:242`): hardware returns the
   live attribute byte under the beam (0xFF outside the screen area). Not
   implemented - reads fall through to the bus default. A few demos poll it;
   would need a beam-position -> attribute lookup in the decoder.
2. **Font RAM upload** (ATM3): `regBF & 4` redirects CPU memory writes into
   the 2 KB font RAM (`evoMWr` -> `vid_fnt_wr(adr & 0x7ff)`) and `#BE.0E`
   reads back the font byte text mode is showing. Neither implemented; the
   built-in `ATM_FONT` table is always used. Affects ATM3 software with
   custom fonts (rare).
3. **Plain ZX mode palette routing**: on hardware ALL video (including plain
   ZX mode 3 with z-bits clear) goes through the #FF palette RAM. unreal-ng's
   generic ULA path still renders with the fixed `spec_colors` table on ATM
   machines; only the extended modes and z-modes route through `atmPalette`.
   Observable only by software that reprograms #FF and then switches back to
   plain ZX mode.
4. **EFF7/#7FFD block1m readback masking** (`zports.v:688`): raw readbacks
   (same as xpeccy); see [port-differences.md](port-differences.md).
5. **xx77 decode width on ATM710**: xpeccy's mask `0x9F` (ports
   `77/37/B7/F7` only) kept faithfully; the real hardware decodes more
   aliases. Kept intentionally to match the reference emulator.
6. **Reset-time pen2 state**: unreal-ng leaves the #FF gate open until the
   first xx77 write (FPGA blocks it); see
   [port-differences.md](port-differences.md).

Unrelated pre-existing issues observed during verification (NOT caused by this
port, confirmed via `git stash`):

- `tools/poc/011-ttd-v2-capture-analysis` breaks the full `ninja` build
  (predates this branch work; `core-tests` target unaffected).
- `ATM710TrdosBoot_Test.MenuTRDOSBootsClassicTRDOS` fails on the branch.
- `EmulatorManager_Test.CreateEmulatorWithNonCreatableModelReportsReason`
  fails on the branch (master-oriented: ATM710 is creatable here by design).

## Reproducing the verification

```bash
# build + full ATM-focused suites
ninja -C cmake-build-release core-tests
./cmake-build-release/bin/core-tests --gtest_filter=\
"*ATM*:*Atm*:*Alco*:*Hwmc*:*Palette*:*Border_FE*:*Timing_ATM3*"

# full suite (parallel)
cmake --build cmake-build-release --target test-parallel
```
