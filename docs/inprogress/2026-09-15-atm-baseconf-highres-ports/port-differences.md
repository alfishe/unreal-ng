# Port Differences: ATM710 vs ATM3 (BaseConf)

Port-by-port reference of everything this port touched, with the xpeccy source
(`atm2.c` = ATM710, `pentevo.c` = ATM3), the BaseConf FPGA RTL
(`z80/zports.v`, `video/video_palframe.v`) and the unreal-ng implementation
side by side.

## Overview table

| Port | ATM710 (xpeccy `atm2.c`) | ATM3 (xpeccy `pentevo.c`) | unreal-ng |
|------|--------------------------|---------------------------|-----------|
| `#FF` palette (write) | partial decode, DOS-gated | exact `#FF`, manager-gated | both, per model |
| `#FF` palette (read) | live attribute under beam (`atm2InFF`) | not readable | **gap** - not implemented (see [verification-gaps-and-tests.md](verification-gaps-and-tests.md)) |
| `#FE` border | 4-bit border, bright = NOT A3 | same | `atmBorderBright` latch, both models |
| `xx77` video/pager | mask `0x9F` quirk, DOS-gated | exact low byte, manager-gated | per model below |
| `#7FFD` | plain 128K paging | lock honored only with EFF7 lockmem | per model |
| `#EFF7` | n/a (not used for video on ATM710) | z-bits select AlCo/HWMC over ZX base | z-bit change re-inits raster |
| `#BE` readbacks | n/a | 0x0B/0x0C/0x0D/0x0F (0x0E font byte) | 0x0B/0x0C/0x0D/0x0F implemented |
| turbo | EFF7-driven | 3-state via xx77 data bit 3 + EFF7 bit 4 | `updateTurboMode` |

## #FF palette write - the active-low DAC

Every ATM color (pixels, attributes, border) resolves through a 16-cell
palette RAM written via port #FF. FPGA (`zports.v:869`):

```verilog
atm_paldata = {~din[4], ~din[7], ~din[1], ~din[6], ~din[0], ~din[5]};
//             G(2b)      g         R(2b)      r         B(2b)      b
```

Each channel is 2 bits, MSB first, **active low** (a set data bit pulls the
DAC line down). With `v = value ^ 0xFF`:

```
red   = 10*v1 + 5*v6      level in {0, 5, 10, 15}
green = 10*v4 + 5*v7      -> LEVELS ladder 0x00, 0x11, 0x22, ... 0xFF
blue  = 10*v0 + 5*v5      (5 -> 0x55, 10 -> 0xAA, 15 -> 0xFF)
```

unreal-ng (`PortDecoder_ATM710::Port_ATM_Palette_Out`, shared by ATM3) packs
the color as `0xAABBGGRR` (framebuffer order, red in the low byte) and stores
the RAW pre-inversion byte in `EmulatorState.atmPaletteRegs[]` for the
#BE.0D readback. Example verified by RTL round trip: writing `0x55` ->
`v = 0xAA` -> red 10 (`0xAA`), green 5 (`0x55`), blue 5 (`0x55`) ->
`0xFF5555AA`; writing `0x00` -> white; writing `0xFF` -> black.

### Cell pointer = the 4-bit border

FPGA (`video_palframe.v:98`): `pal_addr = atm_palwr ? {5'd0, zxcolor} : ...`
- the palette RAM cell is selected by the border color register `zxcolor`,
which is the 4-bit FE border (`zports.v:500`, `border <= {~a[3], din[2:0]}`).
xpeccy: `atm2OutFF`/`evoOutFF` use `vid->brdcol & 0x0f` the same way.
unreal-ng: cell = `(border_attr & 0x07) | (atmBorderBright << 3)`, and **every
renderer routes the border through it** (`vidDrawATM*` ->
`vid_dot_full(vid, brdcol & 0x0f)` in xpeccy).

### Decode differences

| | ATM710 | ATM3 |
|---|---|---|
| address match | `(port & 0x9F) == 0x9F` (aliases `9F/BF/DF/FF` low bytes) | exact `(port & 0xFF) == 0xFF` |

### Write gates

| Gate | ATM710 | ATM3 |
|------|--------|------|
| decode enable | `IsDosPortsEnabled()` (DOSEN or SYSEN - the xpeccy "dos" line: `{0x009f,0x00ff,1,...}` map entry) | `IsManagerEnabled()` |
| pen2 latch (`aFF77.PEN2`, bit 14, from address A14) | blocks the write when set | same |

`PortDecoder_ATM3::IsManagerEnabled() = (pBF & 1) || !(aFF77 & CPM) ||
(flags & CF_TRDOS)` - xpeccy `evoIn57` gate. The pen2 polarity follows the
RTL: `atm_pen2 <= ~a[14]`, `atm_palwr = vg_wrFF_fclk & atm_pen2`, i.e. an xx77
write with A14 high closes the palette; xpeccy's own reset performs
`evoOut77d(0x4377, 0x03)` = "pager on, TR-DOS free, palette closed".

## #FE - the fourth border bit

FPGA: `border <= {~a[3], din[2:0]}` - the bright bit of the border/palette
pointer is **NOT address bit 3**, but its inverse, and it is **re-latched by
every #FE write** (not sticky). xpeccy: `atm2OutFE`/`evoOutFE` do
`nextbrd |= (port ^ 8) & 8` - identical polarity. unreal-ng
(`portdecoder_atm710.cpp`, #FE handler): `atmBorderBright = (port & 0x0008) ? 0 : 1;`
so `OUT (#FE)` selects the dim cell, `OUT (#F6)` the bright cell of the same
color (`Border_FEAddressBit3_SelectsBrightPaletteCell`).

## xx77 - video mode + pager latch

Data byte: bits 0-2 = video mode, bit 3 = turbo select (ATM3);
address bits: A8 -> `aFF77.PEN` (ATM paging enable, active low in RTL:
`atm_pen <= ~a[8]`), A9 -> `CPM`, A14 -> `PEN2`.
xpeccy `evoOut77d`: `prt2 = a14.a9.a8.0.b3.b2.b1.b0` and
`compSetHwTurbo((val & 0x08) ? 4 : ((pEFF7 & 0x10) ? 1 : 2))` - the 3-state
turbo (3.5 MHz forced, or EFF7 bit 4 chooses 7/14 MHz), implemented as
`PortDecoder_ATM3::updateTurboMode`.

| | ATM710 | ATM3 |
|---|---|---|
| address match | `(port & 0x9F) == 0x77`-style partial decode (xpeccy map `{0x009f,0x0077}` - ports `77/37/B7/F7`) | exact `(port & 0xFF) == 0x77` |
| gate | `IsDosPortsEnabled()` | `IsManagerEnabled()` |

The ATM710 partial mask is an xpeccy quirk (a strict subset of the xx77
aliases the hardware decodes); we kept it **exact** rather than "fixing" it,
so software behavior matches the reference emulator. A mode-bit change
triggers `InitRaster` (both models) exactly like xpeccy's `evoSetVideoMode`
call from `evoOut77d`.

## #7FFD - conditional lock on ATM3

xpeccy `evoOut7FFD` (`pentevo.c:477-478`):

```c
if ((comp->pEFF7 & 4) && (comp->p7FFD & 0x20)) return;   // ignore the write
```

i.e. the classic "bit 5 locks 7FFD forever" Pentagon rule is **conditional**:
the lock is only honored while EFF7 lockmem (`EFF7 & 0x04`) is also set.
Clearing EFF7.2 re-opens #7FFD even with 7FFD.5 latched - required for the
P1024 (1 MB) paging mode. unreal-ng `PortDecoder_ATM3::Port_7FFD_Out` ports
this verbatim (`Port_7FFD_LockOnlyWithEFF7Lockmem`); ATM710 keeps the plain
128K behavior (`atm2Out7FFD`).

## #EFF7

Stored raw on ATM3 (`Port_EFF7_Out`); only a change of the video z-bits
(`EFF7 & 0x21`) re-inits the raster (mode control bits are store-only).
Readback at `#BE.0B` returns the raw register. Nuance: the FPGA
(`zports.v:688`) masks z/control bits out of the readback while `block1m`
(EFF7.2) is set (`peff7 = block1m ? {...masked...} : peff7_int`) and forces
`p7ffd` bits 7:5 to read back 0 in that mode; both xpeccy
(`case 0x0b00: res = comp->pEFF7`) and unreal-ng return the raw register -
documented divergence, harmless for known software.

## #BE readbacks (ATM3 only)

xpeccy `evoIn57` / FPGA `portbemux` (`zports.v:896-913`), implemented in
`PortDecoder_ATM3::Port_BE_In` (`port & 0xFF00` selects):

| high byte | value | notes |
|-----------|-------|-------|
| 0x0B | `pEFF7` raw | RTL 5'hB = `peff7_int` |
| 0x0C | `prt2 \| dos` : bits 0-2 mode, 3 turbo, 4 DOS, 5 ~PEN, 6 CPM_N, 7 ~PEN2 | RTL 5'hC |
| 0x0D | palette raw byte of the border cell, `(raw & 0xF3) \| 0x0C` - bits 2,3 always read 1 | RTL round trip decoded from `video_palframe.v:100/107`; xpeccy `evoIn57` carries the identical comment |
| 0x0E | font byte the text mode is currently showing (`vid->fntbyte`) | **gap** - not implemented |
| 0x0F | last written 4-bit border (`nextbrd & 0x0f`) | RTL 5'hF = `{4'bXXXX, border}` |

The 0x0D formula was proven end-to-end against the RTL: the write path stores
`atm_paldata` (the 6 inverted channel bits) into `palcolor`, and the readback
mux re-inverts exactly those six positions while forcing bits 3:2 high -
which simplifies to `(raw & 0xF3) | 0x0C` on the stored raw byte.

## Palette RAM defaults and reset state

`EmulatorContext::InitAtmPalette()` presets the 16 cells to the standard ZX
colors (identical to the ULA `clut`/`spec_colors` table, e.g. cell 1 =
`0xFFC72200` = `#0022C7` blue, cell 2 = `0xFF1628D6` = `#D62816` red), so
software that never touches #FF sees stock colors (xpeccy `vid_reset` /
`zx_set_pal` presets). Hardware reset nuance: the FPGA resets `atm_pen2 = 0`,
which **blocks** #FF writes until the first xx77 write; unreal-ng's cold
`aFF77 = 0` leaves the gate open for that window. Kept as-is for snapshot
compatibility; the window is not observable by real software (the boot ROM
writes xx77 long before touching the palette).
