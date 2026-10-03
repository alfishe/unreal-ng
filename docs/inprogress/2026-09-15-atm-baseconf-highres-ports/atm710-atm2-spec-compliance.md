# ATM710 — compliance review against the ATM Turbo 2+ architecture book (`atm2_arch.pdf`)

| | |
|---|---|
| **Date** | 2026-10-01 |
| **Status** | Review complete. Four gaps found (**S7-1 … S7-4** below), each with a proposed fix; none implemented yet. Three spec subsystems documented as absent (§4). |
| **Machine** | `MM_ATM710`, short name `ATM710` (ATM Turbo 2+ v7.10) — the sibling of this folder's `ATM3` work; the two share `PortDecoder_ATM710` (ATM3 derives from it) |
| **Baseline** | `master` @ `e02d8f4d` |
| **Spec** | `atm2_arch.pdf` — «Персональный компьютер Turbo 2+. Внутренняя архитектура и внешние устройства», 2nd corrected edition, Moscow 2005, 60 pp. (local copy: `emulators/github/zx-evo/pentevo/docs/ATM/atm2_arch.pdf`) |
| **Cross-evidence** | Unreal Speccy 0.37.9 (`memory.cpp` `set_banks()`, `io.cpp`, `dxr_atm0/2/6.cpp`), Xpeccy (`src/libxpeccy/hardware/atm2.c`, `atm2PortMap` / `atm2OutFF` / `atm2InFF`) — consulted at the exact disputed points only |

## 0. Summary / verdict

The core of the machine — memory dispatcher, port map, all four documented video
modes, the `#FF` palette, Beta-Disk gating and the IDE 16-bit protocol — **matches
the book** (and, where the book is ambiguous, matches the reference emulators we
already port from). Section-by-section table in §2.

Four deviations found:

| ID | Gap | Spec § | Sev | Size |
|---|---|---|---|---|
| **S7-1** | Covox / LPT port `#FB` dead (writes never dispatched) | II-1.4, II-3 | **H** (user-visible: no Covox sound anywhere) | S |
| **S7-2** | ZX base mode bypasses the `#FF` palette RAM and the 16-color border | I-5.1, 5.5, 5.6 | M | M |
| **S7-3** | `IN #FF` attribute port (open) not implemented — always `#FF` | I-5.7 | M | S |
| **S7-4** | `#EFF7` swallowed ahead of the memory-manager window decode; per spec any `#xxF7` (incl. `#EFF7`) writes window 3 | I-4.1 | L (no known ATM710 software writes `#EFF7`) | S |

Plus one cosmetic stale comment (S7-5) and three subsystems that exist in the spec
but are deliberately not emulated (§4: i8031 keyboard/RS-232/RTC, ADC, external
`#FA` bus).

## 1. How the verdicts were reached

- **The book wins.** `atm2_arch.pdf` is the vendor architecture description for
  exactly this board (v7.10 — the D4/D5 bit notes say "для 7.10").
- Where the book is self-contradictory or describes unbuilt generalizations (the
  6-bit palette DAC inputs, the "technological" video modes, the 64-page ROM),
  the **reference emulators** decide, same policy as [gap-analysis.md](gap-analysis.md) §2.
- Severity/size use the folder scale: **H** = mainstream software fails or shows
  wrong results, **M** = documented feature missing, common software copes,
  **L** = rare software / cosmetic; **S** < 1 day, **M** = days.

## 2. What matches (spec → implementation)

| Spec § | Requirement | Implementation (verified) |
|---|---|---|
| I-3.1 | System port `#FF77`: D0-D2 = RG0-2 video modes, D3 turbo, D5 Z_I int gate; address A8 = PEN, A9 = CPM, A14 = PEN2; recommended `#4177`/`#0177`/`#FF77` values | `Port_FF77_Out` (`core/src/emulator/ports/models/portdecoder_atm710.cpp:569`); modes 3/2/0/6 → ZX/HWMC/EGA/Text, others ("технологические") → `M_NUL` (`core/src/emulator/video/screen.cpp:357`); PEN/CPM/PEN2 latched in `aFF77` and consumed by mapping / shadow gate / palette gate |
| I-3 | Two ways to open shadow ports: DOSEN (TR-DOS session via `#3Dxx`, ROM2) or clearing A9 (CPM signal = permanent DOSEN) | `IsDosPortsEnabled()` = `CF_DOSPORTS \|\| ~CPM` (`portdecoder_atm710.cpp:453`), gating `xx77`/`xFF7`/Beta128/IDE on read and write |
| I-4 / 4.1 | 16 KiB pages, 64 RAM pages, 4 windows, dispatcher `#xxF7`: inverted D0-D5 page, D6 RAM/ROM, D7 mux with `#7FFD` (RAM: D0-D2 ← non-inverted 7FFD bits; ROM: D0 ← DOSEN), window by A14-A15, two register sets by 7FFD.4 | `Port_FFF7_Out` + `updateMemoryBanks` (`portdecoder_atm710.cpp:625`, `:723`), types `0x000/0x100/0x200/0x300`; covered by tests `FFF7_RegisterEncoding_*`, `FFF7_MemoryBanks_*` |
| I-3.1 (A8) | PEN = 0: dispatcher off, every quarter shows the last ROM page (CP/M BIOS; the four system pages are always the four **highest**) — the power-on state | `portdecoder_atm710.cpp:750` (all four windows → `romMask`); identical to Unreal Speccy `set_banks()` `pen=0` branch |
| I-4.2 | `#7FFD` with the NedoPC fixes (no A9 in the decode), D5 lock, D3 screen page, D4 ROM2 | `IsPort_7FFD` mask `0x8006`/match `0x0004`; `IsPagingLocked`; `SetActiveScreen` |
| I-1.5.1 / II-6.1 | `IN #7FFD` flags: D6 = WIRQ (IDE), D7 = ADCS, D0-D5 = 1, decode needs A9=1, open port | `IdeAdapter::In` `IDE_ATM` `(port & 0x8202) == 0x0200` branch, ungated (`core/src/emulator/io/ide/ideadapter.cpp:94`) |
| I-5.5 / II-1.1 | Border 16 colors: D0-D2 + inverted A3 (BRD3), re-latched by every `#FE` write | `atmBorderBright` latch (`portdecoder_atm710.cpp:238`) — effective in the extended modes (see S7-2 for ZX mode) |
| I-5.6 | Palette: 16 positions × 64 colors, active-low, cell = the currently displayed color (practically the 4-bit border), gated by PEN2 + shadow | `Port_ATM_Palette_Out` (`portdecoder_atm710.cpp:671`) — the xpeccy `atm2OutFF` DAC model with the inverted-data-in-address-high-byte substitution; defaults = standard ZX 16 (`InitAtmPalette`, `core/src/emulator/platform.h:1149`); tests `PaletteFF_*` |
| I-5.1 | ZX screen: page 5/7 by 7FFD D3 | `DetectModeATM2` mode 3 → ZX renderer, `VideoPage()` |
| I-5.2 | Text 80x25: codes even `#01C0+64r` / odd `#21C0+64r` in page 5(7); attributes **swapped** (even → `#21C0`, odd → `#01C0`); attr D0-D2+D6 = INK, D3-D5+D7 = PAPER, no flash; 64-byte rows, 40 displayed; font from a dedicated 2 KiB generator | `ScreenAtm::Draw` M_ATMTX (`core/src/emulator/video/atm/screenatm.cpp:175`), geometry in `core/src/emulator/video/atm/atmgeometry.h`; the odd-attr `+1` intra-row offset is the reference renderer's hardware quirk (`dxr_atm6.cpp` `text2_ofs = -4*PAGE+1`), the spec only fixes the areas |
| I-5.3 | HW multicolor 640x200: 1bpp + per-byte attrs (8x1), even/odd columns at +0/+0x2000, attribute parity **not** swapped, linear 40-byte rows | `ScreenAtm::Draw` M_ATMHR (`screenatm.cpp:113`); test `Render_ATMHR_Linear40ByteStride_*` |
| I-5.4 | EGA 320x200x16: pixel pairs interleaved across four planes exactly per fig. 9 (page5 low = pairs ≡1 mod 4, page5 high ≡3, page1 low ≡0, page1 high ≡2); pair bits D0-D2+D6 left / D3-D5+D7 right | `ScreenAtm::Draw` M_ATM16 (`screenatm.cpp:90`) + `PairColourIndex`; tests `Render_ATM16_*` |
| I-5 (fig. 4) | NedoPC fix: the A5-A7 ↔ A8-A10 address permutation on RG0 toggling removed | `atm_memswap` deliberately not emulated (`AtmMemSwap=0`), comment at `portdecoder_atm710.cpp:573` |
| II-5 | Beta-Disk 128, all ports shadow: `#1F/#3F/#5F/#7F/#FF`, sysreg D0 drive / D2 reset / D3 halt / D4 side, readback D6 DRQ / D7 INTRQ | gated Beta128 dispatch with dirty-high-byte canonicalization (`portdecoder_atm710.cpp:346`); bit 6 (DDEN) masked — the ATM2+ board does not wire it, matching the book's bit table |
| II-6 | IDE: `#xx0F` (A0-A3=1, A4=0), A5-A7 register select, A8 = low/high half; 16-bit transfer only at A8=0; read latches the high byte, write is high-byte-first; ports shadow; WIRQ on `#7FFD` D6 | `IdeAdapter::AtmIn/AtmOut` (`ideadapter.cpp:285-314`), gate `IdeGate().dosPorts = IsDosPortsEnabled()`; tested by `IdeAdapter_Test.AtmDecodeGateAndIntrq` |
| II-1.3 | AY `#FFFD`/`#BFFD`, open, NedoPC decode (no A9) | decoder arms + tests |
| I-3.1 (D3) | Turbo = 7.0 MHz | `updateTurboMode` → `hw_turbo_ratio = 2` (`portdecoder_atm710.cpp:800`) |
| II-7 | `#FFE7`/`#FEE7` read as `#FF` | undecoded + `FloatBus=0` in the shipped config → `#FF` |

The `xx77`/`xFF7` write gate and the missing `xxF7` readback both match the
references (Unreal Speccy wraps the whole ATM710 block in `CF_DOSPORTS`; our
`CF_DOSPORTS || ~CPM` superset is *more* spec-faithful — the book says A9=0 opens
all shadow ports permanently).

## 3. Gaps and proposed fixes

### S7-1 — Covox / LPT `#FB` never receives data (Sev H, Size S)

**Spec** (II-1.4, II-3, port table): `out #nnFB` (A2=0, A0=A1=1) is the 8-bit DAC
(Covox) and the LPT data port simultaneously; `#nn7B` (A7=0) is the strobed form;
`in #nnFB` reads printer busy on D7.

**Today**: the Covox is a *self-decoding* `PortDevice` registered for every model
by `SoundManager::attachToPorts` (`core/src/emulator/sound/soundmanager.cpp:1677`),
but `PortDecoder_ATM710::DecodePortOut/In` never calls
`DispatchSelfDecodingOut/In` (`core/src/emulator/ports/portdecoder.cpp:1732`), so
on ATM710 `OUT #FB` reaches nothing. This is the exact bug that was fixed for
ATM3 in phase E0 (`portdecoder_atm3.cpp:497`, test
`core/tests/emulator/ports/models/portdecoder_atm3_test.cpp:1074`
`Covox_FbReachesSelfDecodingDevice`, whose comment says "the ATM decoders did not
dispatch self-decoding devices at all") — the fix was never back-ported to ATM710.

**Proposed fix** (mirror of the ATM3 phase-E0 shape, restricted to the addresses
the ATM2+ board actually wires — the mono `#FB`/`#7B` group, not the full
SoundDrive set which would collide with FDC `#xx1F` in the quad fitment):

- In `PortDecoder_ATM710::DecodePortOut`, final fall-through, before/after the
  palette arm: add

  ```cpp
  const uint8_t low = port & 0x00FF;
  if (low == 0x00FB || low == 0x007B)   // Covox DAC + LPT data/strobe (spec II-1.4/II-3)
      DispatchSelfDecodingOut((port & 0xFF00) | 0x00FB, value);  // canonicalize #7B -> #FB
  ```

  with a `disp` attribution (`decodedPort = 0x00FB`, device `PortDeviceId::Covox`).
  The canonicalization is needed because `Covox::MatchesFitment` (Mono) only
  claims the exact `#FB` low byte, while `#7B` drives the same latch.
- In `DecodePortIn`: `#FB` returns `0xFF` (D7 = busy, "printer not connected" —
  matches the book's D0-D6 = 1).
- Test (add to `core/tests/emulator/ports/models/portdecoder_atm710_test.cpp`,
  same mock as the ATM3 one): `DecodePortOut(0x00FB, …)` and `DecodePortOut(0x007B, …)`
  reach the registered device; `#F1/#F3/#F9` and the SoundDrive mode-1 low bytes do
  **not** (they are not on this board).

### S7-2 — ZX base mode bypasses the palette RAM and the 16-color border (Sev M, Size M)

*Consolidates [verification-gaps-and-tests.md](verification-gaps-and-tests.md)
item 3 (PLAN #53) and adds the border half of the gap.*

**Spec** (I-5.1, 5.5, 5.6): the 16 palette positions cover the whole machine —
in ZX mode they default to the standard colors but remain programmable via `#FF`,
and the border is 16-color (BRD3 via A3) in every mode.

**Today**: the extended modes render through `EmulatorState.atmPalette`
(`core/src/emulator/video/atm/screenatm.cpp:57`), but the ZX renderer uses fixed
tables (`_rgbaColors`/`_rgbaFlashColors`, `core/src/emulator/video/zx/screenzx.cpp:87`)
and `ScreenZX::SetBorderColor` truncates to 3 bits (`screenzx.cpp:713`
`_borderColor = color & 0b0000'0111`), so `atmBorderBright` and palette writes have
no effect in the machine's primary mode. The shipped config's `UsePalette=1` and
`[COLORS] color=ATM` are not parsed anywhere (dead options).

**Proposed fix**:

1. Give `ScreenZX` an optional palette indirection: a `const uint32_t*
   _atmPaletteOverride` consulted by the ink/paper/border lookups when non-null
   (the flash variant applies the same cell with paper/ink swapped — flash still
   swaps attribute bits, the palette cell itself is per-color-position).
   `Screen::SetVideoMode`/model init sets it for `MM_ATM710`/`MM_ATM3` ZX-family
   modes. Palette writes are rare, but the lookup is per-cell — keep it as one
   extra indirection, no branching beyond the null check (per the hot-path rule in
   `docs/guidelines/performance-guidelines.md`; the pointer is what the ATM16/HR/TX
   path already does).
2. Route the border through the 4-bit index: on ATM models
   `SetBorderColor(value & 0x07)` becomes
   `SetBorderIndex((value & 0x07) | (state.atmBorderBright << 3))` and the border
   fill reads `atmPalette[idx]` (exactly what `ScreenAtm` does at
   `screenatm.cpp:57`).
3. Either parse `UsePalette`/`[COLORS] color=ATM` or delete them from
   `data/configs/atm710/unreal.ini` so the config stops promising a feature.

Tests: extend `atm_video_modes_suite_test.cpp` — palette `#FF` write + bright
border visible in mode 3 (framebuffer border cell), attribute color routed through
the reprogrammed cell.

### S7-3 — `IN #FF` attribute port not implemented (Sev M, Size S)

*Consolidates [verification-gaps-and-tests.md](verification-gaps-and-tests.md)
item 1 (PLAN #53); the spec review confirms the requirement (§5.7) and adds the
proposed implementation route below.*

**Spec** (I-5.7, port table, open port): reads the attribute byte currently being
displayed.

**Today**: `DecodePortIn` has no `#FF` arm; the read falls to the floating bus,
and the shipped config disables that (`FloatBus=0` in
`data/configs/atm710/unreal.ini`), so `IN #FF` is always `#FF`. The original
UnrealSpeccy "PortFF" option (which the config still carries as `PortFF=1`) was
deliberately not ported (`core/src/emulator/config.cpp:335` — "UlaContention
implements the full architecture-aware floating bus instead"), which is true only
when `FloatBus=1`.

**Evidence**: Xpeccy implements the port for ATM2 unconditionally —
`atm2InFF` (`atm2.c:227`): `return (vbrd || hbrd) ? 0xff : vid->atrbyte` — as the
catch-all handler of `atm2PortMap`.

**Proposed fix** (reuse the existing Scorpion machinery —
`UlaContention::GetFloatingBusAttribute()`, `core/src/emulator/video/ulacontention.h:255`,
which already returns the attribute of the cell under the beam and is *not* gated
by the `FloatBus` config toggle):

- In `PortDecoder_ATM710::DecodePortIn`, add an arm for the exact `#FF` low byte
  (`(port & 0x00FF) == 0x00FF`), placed with the other open-port arms, returning
  `pUlaContention->GetFloatingBusAttribute()` (this yields `#FF` in border/blanking,
  matching `atm2InFF`'s `vbrd/hbrd` case).
- Keep the read outside the shadow gate (the spec marks the port open) and keep it
  additive with the shadow `IN #FF` (Beta128 DRQ/INTRQ) — that arm is already
  gated, so ordering: gated Beta128 first, then the open attribute read only when
  the Beta arm did not claim it (i.e. in shadow the FDC wins, as on hardware both
  answer and the book documents both meanings for `in #FF`).
- Test: during a known paper position, `IN #FF` returns the attribute byte of that
  cell; in the border region it returns `#FF` (mirror the Scorpion tests).

### S7-4 — `#EFF7` preempts the memory-manager window decode (Sev L, Size S)

**Spec** (I-4.1): the dispatcher is *any* `#xxF7` (low byte `F7`: A0=A2=A4=A7=1,
A3=0) — `#EFF7` is window 3 like any other mirror. The book has no `#EFF7` entry
at all (the port table lists it for no function on this machine).

**Today**: `IsPort_EFF7` (exact `0xEFF7`) is tested **before** `IsPort_FFF7` in
`DecodePortOut` (`portdecoder_atm710.cpp:285`), so `OUT #EFF7` lands in the inert
`pEFF7` latch (`Port_EFF7_Out`, self-described as "Inert on ATM 7.10") and a
legitimate window-3 write is lost. Both references disagree with us here:
Unreal Speccy decodes `(port & 0x00FF) == 0xF7` for ATM710 (`io.cpp`, the `EFF7`
handler is Pentagon-only) and Xpeccy's `atm2PortMap` has `{0x009f, 0x00f7}` with
no EFF7 special case. The in-code justification cites pentevo's `evoPortMap` —
that is the ATM3/BaseConf map, not this machine.

**Proposed fix**:

- Delete the `IsPort_EFF7` arm from `PortDecoder_ATM710::DecodePortOut` so `#EFF7`
  falls into `IsPort_FFF7` (window 3). `PortDecoder_ATM3` overrides
  `DecodePortOut` entirely, so ATM3 keeps its BaseConf `#EFF7` semantics.
- For `DecodePortIn`: either drop the `pEFF7` readback too (reference behavior —
  floating) or keep it as a harmless debug extension; dropping is cleaner.
- Update the now-wrong tests `IsPort_EFF7` (decode) and the
  `PortDecoder_ATM710_Trace_Test.EveryPortIsAttributed` expectations;
  add `FFF7_EFF7_IsWindowThree` (OUT `#EFF7` writes `pFFF7[regSet+3]`).
- Fix the stale header comment `ATM_FF77_TURBO = 0x08; // Bit 3: 14MHz turbo`
  (`core/src/emulator/ports/models/portdecoder_atm710.h:46`) → 7 MHz (S7-5,
  cosmetic, fold into the same change).

## 4. Present in the spec, deliberately absent from the emulator

Documented as non-implemented; no fix proposed now (each is a self-contained
subproject, and none is exercised by the shipped software set):

| Spec § | Subsystem | Status today |
|---|---|---|
| II-2 + App. 1 | i8031 XT/AT keyboard controller: `0x55`→`0xAA` protocol, modes 0-3, CP/M key codes, flags registers; RS-232; RTC (clock/calendar registers) | Not emulated. `#FE` reads are the plain ZX matrix (`Keyboard::HandlePortIn`, `core/src/emulator/io/keyboard/keyboard.cpp:329`) — equivalent to mode 0 with VE1=1. FF77 D4 (Z_1), D6 (VE1), D7 (VE0) are stored but drive nothing; `#FE` D5 (Z-signal) reads constant 1, which the book itself calls obsolete. `ATMKBD=1` in the config is a host-input mapping option, not the 8031 protocol |
| II-1.5 | 8-channel ADC: `IN #7DFD` data, channel selected by border color D0-D2 | Not emulated; `#7DFD` undecoded (floating `#FF`). The `ADCS` flag on `IN #7FFD` D7 permanently reports "ready" — internally consistent |
| II-4 | External device port `#FA` (+ `#FB` as its address bus) | Not decoded; no emulated peripherals attach to it |

## 5. Deliberate deviations that are correct (keep as is)

- **`xxF7` has no readback** — matches the references and is load-bearing for the
  stock TR-DOS 5.04T `#3D38` probe (comment at `portdecoder_atm710.cpp:161`).
- **Beta128 `#FF` bit 6 masked** — the board does not wire DDEN; the monitor ROM
  itself writes `5C` before MFM reads.
- **NedoPC-corrected decodes** (`#7FFD`, AY) omit the A9 "board error" bit, as the
  book's own footnotes instruct.
- **GS host ports `#B3/#BB/#33`** — an external ZX-Bus card, outside the book's
  port map; an extension, not a deviation.

## 6. Proposed verification for the fixes

1. S7-1: decoder unit test (mock self-decoding device) + machine-level: a Covox
   demo / CP/M digital-audio playback produces non-flat Covox samples
   (`SoundManager::getCovox()->hadSoundLastFrame()`).
2. S7-2: video suite tests (border cell + attribute cell after a `#FF` palette
   write, mode 3); re-run the EGA/HWM/TX suite to prove the shared palette didn't
   shift; golden-path boot re-check (ATM BIOS menu colors unchanged with the
   default palette).
3. S7-3: unit test `IN #FF` in paper vs border; the Scorpion attr-port tests as
   the template.
4. S7-4: decoder test `OUT #EFF7` → `pFFF7[3]` (both register sets); the existing
   ATM710 trace/port-map tests updated.

All four need the standard pre-commit gate: full
`ninja -C cmake-build-agent-release` (zero warnings) + `test-parallel` green.
