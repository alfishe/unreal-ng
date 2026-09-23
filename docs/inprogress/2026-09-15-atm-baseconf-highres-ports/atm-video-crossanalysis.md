# ATM Video Mode Cross-Analysis

## Original Unreal Speccy Findings (from `atm.cpp`)

### Port FF77 Handling
- `set_atm_FF77()` is the main port write handler
- `pFF77` = port data byte, `aFF77` = port address bits

### Bit Assignments
Corrected against the actual source (`set_atm_FF77`, alfishe/unreal-speccy
atm.cpp):

| Bit | pFF77 | aFF77 |
|-----|-------|-------|
| 0-2 | Video mode (`val & 7`) | - |
| 3 | Turbo clock select | - |
| 5 | INT_GATE (0=masked, 1=enabled) | - |
| 8 | - | PEN (ATM paging enable) |
| 9 | - | ~CPM (TR-DOS mode) |

NOTE on MEMSWAP: the original `set_atm_FF77` calls `atm_memswap()` on pFF77
bit0 transitions, but `atm_memswap()` opens with `if (!conf.atm.mem_swap)
return;` - the physical A5-A7<->A8-A10 RAM permutation is gated behind the
unreal.ini `AtmMemSwap` option, DEFAULT 0 (OFF). Since bit0 is a plain
video-mode bit, running the permutation unconditionally scrambles all RAM on
routine video-mode changes (reproduced: 2048.scl dies at its mode 3->0
switch, $E076). unreal-ng therefore does not emulate the memswap at all
(removed from PortDecoder_ATM710/ATM3; the `atmMemSwapped` state field is
vestigial and only survives in the TTD paging blob). The earlier table entry
"bit 0 = MEMSWAP, bits 1-2,4 = video mode" was a misreading - video mode is
`val & 7`.

### Video Modes
| Mode | Name | Description |
|------|------|-------------|
| 0 | FF77_16 | EGA 16-color (4 planes at 4000h offsets) |
| 2 | FF77_MC | Hardware Multicolor |
| 3 | FF77_ZX | ZX Standard (compatible) |
| 6 | FF77_TX | Text 80x25 |
| 7 | FF77_TL | Linear Text (ATM3/ZX-Evo only) |

### Reset State
- `pFF77 = 0`, `aFF77 = 0` (no explicit initialization, defaults from memset)
- Boots in mode 0 (EGA 16-color)
- INT gate OFF (ROM enables during boot)
- PEN=0 (ATM paging disabled initially)

### Video Mode Switching Side Effects (`AtmApplySideEffectsWhenChangeVideomode`)
1. Switching TO/FROM mode 3 (ZX Standard) resets scanline state
2. Switching between text (6) and graphics (0,2) has address increment side effects:
   - Text→Graphics: reset if coming from text with odd increment
   - Graphics→Text: set odd increment

### Memory Layout (EGA mode)
- 4 planes at offsets: 0x4000, 0x8000, 0xC000, 0x10000
- Each plane = 16KB, total 64KB video RAM
- Video page from port 7FFD bit 3 only (verified 2026-09-23 against
  unreal-speccy `dxr_atm0.cpp` plane-offset structure and ZXMAK2
  `MemoryAtm710.cs` `videoPage = (CMR0 & 0x08) == 0 ? 5 : 7` - CMR0 is the
  raw #7FFD byte, `BusWritePort7FFD_128` sets `CMR0 = value` directly, no
  other bits involved. The "bits 3,6" note above was a stale error; kept
  struck through for history, not the current understanding.)

## Implementation Status in unreal-ng

### Completed
- [x] Video mode detection in `InitRaster()`
- [x] Draw callbacks for M_ATM16, M_ATMHR, M_ATMTX, M_ATMTL
- [x] ATM model config folder lookup
- [x] Frame timing defaults - now uses ZX-compatible 312-line PAL (69888 T-states, 19968us)
- [x] ROM :page suffix parsing for multi-bank ROMs
- [x] Audio timing fix using `frame_duration_us` with standard formula
- [x] Reset state matches original (pFF77=0, multiplier=1 for base clock)
- [x] Unit tests for all video modes (30 tests)
- [x] EGA 16-color bit-planar rendering (DrawATM16)
- [x] Hardware MC rendering (DrawATMHiRes)
- [x] Text 80x25 mode rendering (DrawATM2Text)
- [x] Linear text mode rendering ATM3 (DrawATM3Text)
- [x] Default EGA palette initialization (16 colors)
- [x] Sound tests updated for frame_duration_us
- [x] **Port FF77 triggers InitRaster() on video mode change**
- [x] **Fixed ATM_FF77_VMODE_MASK to 0x07 (bits 0,1,2 per original)**

### Fixed (from agent review blockers)
- [x] **Blocker 1**: Removed early returns in InitRaster() - ATM branches fall through to SetVideoMode()
- [x] **Blocker 2**: Added ATM modes to AllocateFramebuffer whitelist
- [x] **Blocker 3**: Updated rasterDescriptors with real ATM geometry (448x288 frame, 320x200 screen)
- [x] **Blocker 4**: Added DrawATMMode() to ScreenZX that writes to framebuffer (not dead vbuf)
- [x] **Blocker 5**: Tests verify _mode, raster, GetVideoMode()

### Fixed (audio/timing/aspect issues - 2026-08-21)
- [x] **Audio overflow**: Turbo multiplier now defaults to 1 (3.5MHz base clock) at reset
- [x] **Audio overflow**: Removed 14269us special case - ATM uses standard ZX-compatible timing
- [x] **Aspect ratio**: DeviceScreen::init() now calls conformToAspectRatio() after model switch
- [x] **INT gate**: Port FF77 bit 5 now controls Z80::int_gate (essential for boot sequence)

### Not Started
- [ ] Video mode switching side effects (AtmApplySideEffectsWhenChangeVideomode)
- [ ] Programmable palette via port writes
- [ ] Character ROM location verification for text modes

### Resolved (2026-09-23)
- [x] **Border rendering for ATM modes**: was never actually implemented
  correctly - the raster descriptors gave M_ATM16/ATMHR/ATMTX/ATMTL a
  nonzero `screenOffsetLeft` (64/32px) and wider `fullFrameWidth`,
  modeling a ZX-style scanned side border that doesn't exist on real ATM
  hardware. Cross-checked against 3 independent ZXMAK2 renderer classes
  (`Atm320Renderer`/`Atm640Renderer`/`AtmTxtRenderer` `CreateParams()`,
  all `c_ulaBorderLeftT = c_ulaBorderRightT = 0`): these modes are
  edge-to-edge horizontally, only top/bottom border exists. Fixed in
  `screen.h` `rasterDescriptors[]`: `screenOffsetLeft=0`,
  `fullFrameWidth == screenWidth` for all four modes. See
  `docs/inprogress/2026-09-22-atm-hires-border-and-addressing/` for the
  full investigation writeup.
- [ ] End-to-end frame test (as agent noted)
- [ ] M_ATMTX/ATMHR plane interleave fix (text mode menu rendering)
