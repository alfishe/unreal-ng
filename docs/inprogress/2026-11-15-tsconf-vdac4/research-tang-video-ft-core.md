# 03. Tang Nano 20K video / FT812 core: what an emulator must model

Research notes for the unreal-ng VDAC4 design. Spec-like: every fact carries a `path:line` reference.
Facts marked **UNKNOWN** were not settled by the sources.

Repositories: `VDAC4:` = github.com/andrewinsidelazarev/VDAC4, `DOOM-VDAC4:`, `R-Type-VDAC4:`, `HMM2-VDAC4:` = the same owner's repositories (default branch, 2026-11-15); `core/` paths are unreal-ng.

Path prefixes used below:

| Prefix | Path |
|--------|-------------------------------------------------------------|
| `T/`   | `VDAC4:Tang Nano 20k/` |
| `RTL/` | `T/Firmware/rtl/` |
| `BL/`  | `T/BL616/` |
| `ESP/` | `VDAC4:ESP32 S3 Zero N8R8/` |
| `HW/`  | `VDAC4:Hardware/` |
| `P/`   | the third-party patch archive `Unreal_VDAC4_pal8_color_test_patch.zip` |
| `E/`   | `core/src/3rdparty/eve-emu/` |
| `ARCH` | `docs/inprogress/2026-10-01-tsconf-vdac2/eve-emu-architecture.md` |

Sources are Russian; quotes are translated.

## 0. Architecture in one paragraph

The Tang FPGA is split into multiboot slots of 1 MB in its own 8 MB flash (`T/Docs/a3-fpga-framebuffer.md:337-376`,
`T/Docs/video-core.md:111-122`). Slot 0 is the TS-Config framebuffer core (Evo video capture -> 720p50 HDMI,
LINK personality 01). Slot 1 is the FT812 core (personality 02), which has no Evo capture. Slot 2 holds the ROM
font image VD4FONT1. Slot 3 is a video (MJPEG) core (personality 05). Slots 4-6 are free
(`RTL/ts_fb_core.sv:246-432` VERSION field, `T/Docs/video-core.md:111-122`). In the FT core the host-visible FT812
front-end (registers, RAM_DL, RAM_CMD, SPI, timing, INT_N) is RTL (`RTL/ft_core.sv`). It is a port of eve-emu:
"behaviour repeats eve-emu (alfishe/unreal-ng, core/src/3rdparty/eve-emu, MIT): register table, SPI transactions,
scan-out and DL swap" (`RTL/ft_core.sv:3-7`). RAM_G sits in SDRAM (`RTL/ft_core.sv:14-17`). The display-list
renderer is a tiled rasterizer in RTL (`RTL/ft_render.sv:1-27`, `RTL/ft_rd_walk.sv:1-45`). The coprocessor
(RAM_CMD consumer) is C firmware on the module's BL616 RISC-V MCU, linked over a private SPI word port
(`RTL/ft_core.sv:6-7`, `T/Docs/ft812-emulation.md:43`, `:224-241`). The ESP32-S3 drives the Tang over
LINK (QSPI) for overlays, settings, core switching and an optional "ESP host" mode (`T/Docs/a3-fpga-framebuffer.md:237-290`).

---

## 1. Differences from a real FT812 and from eve-emu; VDAC4 profile hooks

### 1.1 Host-visible front-end (RTL `ft_core.sv`)

| # | Behaviour | Real FT812 | eve-emu today | VDAC4 Tang | Ref |
|---|-----------|-----------|---------------|------------|-----|
| F1 | Power state after load | SLEEP after POR | SLEEP after POR | Slot-1 core starts **ACTIVE**, ext clock x8 = 64 MHz (`START_ACTIVE = !HAS_EVO`) | `RTL/ft_core.sv:33-37`, `RTL/ts_fb_core.sv:891` |
| F2 | RAM_DL / RAM_CMD on core reset (RST_PULSE, CLKEXT/CLKINT, PWRDOWN) | (not cleared, per Tang author) | cleared | **not cleared**; registers restored in 128 clocks (~2 us) | `RTL/ft_core.sv:21-23`, `:247-248`, `:651-656`, `E/src/eve-chip.cpp:34,43` |
| F3 | Host access during the 128-clock reset sweep | n/a | n/a | reads are ignored (MISO 0), writes dropped (`!clearing` guards) | `RTL/ft_core.sv:674-681`, `:691` |
| F4 | Register commit moment | on high byte / other reg / end of transfer | same (Tang copies eve-emu) | same | `RTL/ft_core.sv:25-28`, `:921-945` |
| F5 | REG_CMDB_WRITE partial word | dropped (TO VERIFY in eve-emu) | `kCmdbDropPartialWord = true` | dropped | `RTL/ft_core.sv:26-28`, `E/src/eve-tunables.h:25` |
| F6 | CLKEXT / CLKINT frequency | crystal / 12 MHz | `kInternalOscillatorHz` 12 MHz | CLKEXT 8 MHz (VDAC2 oscillator), CLKINT 12 MHz, CLKSEL 0 = x5 | `RTL/ft_core.sv:30-31`, `E/src/eve-tunables.h:36-39` |
| F7 | Host commands | full set | full set | 41 STANDBY, 42 SLEEP, 43/50 PWRDOWN, 44 CLKEXT, 48 CLKINT, 61/62 CLKSEL (only in SLEEP, 0 or >= 2), 68 CORERST | `RTL/ft_core.sv:955-967` |
| F8 | ACTIVE wake-up | read at address 0 | same | "H_IGNORE + h_zero" in non-running state -> ACTIVE | `RTL/ft_core.sv:718`, `E/src/eve-spi.cpp:111` |
| F9 | REG_ID ready delay | up to 300 ms | `kIdReadyDelayClocks = 0` | `R_ID` is a constant 0x7C, no delay | `RTL/ft_core.sv:356`, `E/src/eve-tunables.h:30-33` |
| F10 | ROM_CHIPID | `08 12 01 00` at 0x0C0000 | written into RAM_G at reset | written into RAM_G at reset (`chipid_pend`) as 0x00011208; "until the first host write" | `RTL/ft_core.sv:118`, `:569-572`, `:995`, `T/Docs/ft812-emulation.md:98-107`, `E/src/eve-chip.cpp:38` |
| F11 | RAM_G size | 1 MB | `kRamGSize` = 1 MB global const | **2 MB** (0x000000-0x1FFFFF); FT_CFG bit 0 -> **3 MB** (0x2FFFFF) | `T/Docs/ft812-emulation.md:41-42`, `RTL/ft_core.sv:14`, `E/src/eve-internal.h:23-29` |
| F12 | ROM fonts / ROM_FONTROOT | ROM at 0x1E0000..0x2FFFFF, root 0x2FFFFC = 0x00201EE0 | `kRomFontBase` 0x1E0000, served by `RomByte` | font image from flash slot 2 copied by ESP into SDRAM after every FT core load, ROM_FONTROOT written last; visible (read-only) at 0x200000-0x2FFFFF **only in 2 MB mode**; ESP writes it via FT_CFG bit 3 | `T/Docs/ft812-emulation.md:98-131`, `RTL/ft_core.sv:392-399` |
| F13 | Unmapped / undocumented regs | various | eve-emu table | REG_UND558 = 0x100, REG_UND560 = 0x800; other non-existing regs and indexes > 127 read 0 | `RTL/ft_core.sv:375-379` |
| F14 | Swap | at frame end, after renderer | `eve-timing.cpp` RequestSwap/ApplySwap at frame event | DETACHED_SWAP (slot 1, `SD_FAST && !HAS_EVO`): swap and INT_SWAP on the frame event, no wait for the rasterizer; DLSWAP=0 keeps a pending swap, 3 stays without effect | `RTL/ft_core.sv:38-40`, `:634-646`, `:934-939`, `RTL/ts_fb_core.sv:891`, `E/src/eve-timing.cpp:147-177`, `T/Docs/ft812-emulation.md:29-32` |
| F15 | INT_FLAGS clear on read | cleared | cleared | cleared, except a swap that landed after the read is kept (`swap_after_read`) | `RTL/ft_core.sv:747-749` |
| F16 | INT_N | open-drain | `int_n` computed | `int_n = !(int_en && |(flags & mask))`; forced inactive when ESP is host; reaches Evo only when M_SEL = 1 (buffer U11, OE = M_SEL) | `RTL/ft_core.sv:1001`, `RTL/ts_fb_core.sv:887-888`, `RTL/ts_fb_hdmi_top.sv:63`, `HW/A5/KiCad/CHANGES_A5.md:36-58` |
| F17 | Timing / REG_FRAMES | per HCYCLE, VCYCLE, PCLK | per registers | from HCYCLE * PCLK and VCYCLE; event line = voffset + vsize - 1 (clamped); REG_FRAMES ticks on that line | `RTL/ft_core.sv:302-334` |
| F18 | Supported output mode | anything up to 800x600 class | anything | doc: only 1024x768@59 (HCYCLE 1344, VCYCLE 806, PCLK 1) tested; 640x480@57 "not supported"; renderer clamps HSIZE/VSIZE to 1024/768 | `T/Docs/ft812-emulation.md:22-28`, `:36-40`, `:162-177`, `RTL/ft_render.sv:246-247` |
| F19 | 2 MB mode writes >= 0x200000 | n/a | ROM writes ignored | read-only (ignored) | `RTL/ft_core.sv:398-399`, `E/src/eve-memory.cpp` (RomByte) |

### 1.2 SPI electrical behaviour (Evo side) (`ft_spi.sv`)

- Mode 0, MSB first; SCK-clocked, reset by CS (`RTL/ft_spi.sv:1-20`, `T/Docs/ft812-emulation.md:146-158`).
- MISO changes on the rising edge, half a clock early. Outside read data MISO = 0, as in bt8xxemu (`RTL/ft_spi.sv:1-20`).
- Evo SCK: 14 MHz in stock TS-Config firmware, 28 MHz in `zxevo_fw_vdac2-spi28` (`T/Docs/ft812-emulation.md:146-158`).
- First read byte comes 8 SCK after the address bytes. At 28 MHz a RAM_G read that misses the cache can be late:
  the first byte is wrong and FT_STAT bit 26 is set. At 14 MHz it is always in time (`RTL/ft_core.sv:18-19`,
  `T/Docs/ft812-emulation.md:146-158`, `RTL/ts_fb_core.sv:246-432` FT_STAT). **This is visible to software.**
- Host SPI request queue depth is 8 (4 before 1.28) (`RTL/ft_core.sv:14-17`).

### 1.3 Rasterizer (RTL `ft_render.sv`, `ft_rd_walk.sv`, `ft_rd_exec.sv`)

This affects only pixels, never values the host reads back.

- Tiles: 128x8 per the walker header (`RTL/ft_rd_walk.sv:3-4`); `ft_render.sv` header says 64x16, three buffers at 2 MB
  and two at 3 MB (`RTL/ft_render.sv:1-27`). The two headers disagree; **UNKNOWN** which one the current build uses.
- Not drawn (sticky `unsup` flags): sloped LINES / LINE_STRIP, EDGE_STRIP_*, MACRO; CALL deeper than 4; more than 16384 steps
  (`RTL/ft_rd_walk.sv:26-28`, `:70`, `:543-591`; `T/Docs/ft812-emulation.md:294-296`). TEXT8X8 / TEXTVGA / BARGRAPH:
  the exec header lists them as not done (`RTL/ft_rd_exec.sv:44-45`). Formats outside 0-7, 14-17 set `unsup[2]`
  (`RTL/ft_rd_exec.sv:1207`). BILINEAR since 1.25/1.26 (`T/Docs/ft812-emulation.md:294-296`).
- PALETTED is implemented; ARGB4444 alpha 15 expands to 254; REPEAT uses a mask (`T/Docs/ft812-emulation.md:303-307`).
- **PALETTED8 differs** (the one renderer difference third-party software depends on):
  - Tang: one pass takes A, R, G and B from the full ARGB8888 palette word (`RTL/ft_rd_exec.sv:1501-1503`). The palette word
    address is `PALETTE_SOURCE + idx*4`, word-aligned via `pa[21:2]`, so the low 2 bits of PALETTE_SOURCE are ignored
    (`RTL/ft_rd_exec.sv:1785-1807`).
  - Real FT812 / Bridgetek DLL: one pass draws grey; the 4-pass program (one per channel, PALETTE_SOURCE + 0..3 with a
    colour mask) draws colour. On Tang the 4-pass program also draws the right colour, because each pass masks one
    channel of the same full-colour word (`P/README.txt`).
  - eve-emu: `Rgba{v,v,v,v}` (`E/src/eve-bitmap.cpp:192-197` Texel, `:397-410` LinePalette); the patch adds an
    `EVE_VDAC4_PALETTED8` branch in both places and recommends a VDAC4-only profile (`P/README.txt`).
- Frame drops / cancels: heavy frames are skipped; from 1.45 a host write to RAM_G (`stale_write_abort`) cancels the
  picture at most once in a row (`RTL/ft_render.sv:144-158`, `:304-316`, `T/Docs/ft812-emulation.md:29-32`). Cosmetic.
- Dithering is on by default; FT_CFG bit 1 disables it (`T/Docs/ft812-emulation.md:133-144`). Cosmetic.

### 1.4 Coprocessor (BL616 `copro/ft_copro.c`)

Supported commands (`BL/copro/ft_copro.c:5-17`, `:53-83`):
DLSTART, SWAP, INTERRUPT, BG/FG/GRADCOLOR, GRADIENT, PROGRESS, TEXT, SETFONT, NUMBER, CALIBRATE (no effect),
MEMCRC, REGREAD, MEMWRITE, MEMSET, MEMZERO, MEMCPY, APPEND, TOUCH_TRANSFORM (13 words, no effect), INFLATE,
GETPTR, LOADIMAGE, GETPROPS, LOADIDENTITY, TRANSLATE, SCALE, ROTATE, SETMATRIX, TRACK, COLDSTART, GETMATRIX,
SETROTATE, SETBASE, SETSCRATCH, SETBITMAP, MEDIAFIFO, VIDEOSTART, VIDEOFRAME, plus VDAC4-only B0, B1 and A0-AF.

| Area | Real FT812 | eve-emu (`E/src/eve-copro.cpp` BuildCommandTable `:47-108`) | VDAC4 BL616 | Ref |
|------|-----------|-------------------------------------------|-------------|-----|
| Widgets BUTTON, KEYS, SLIDER, SCROLLBAR, TOGGLE, GAUGE, CLOCK, DIAL, SPINNER, STOP, SCREENSAVER, SKETCH, LOGO, SNAPSHOT, SNAPSHOT2 | present | absent | **absent -> fault** | `BL/copro/ft_copro.c:5-17`, `:53-83` |
| PROGRESS | present | absent | present | same |
| SETFONT2, ROMFONT, PLAYVIDEO | present | present | **absent -> fault** | same; `BL/docs/images.md:66-70` |
| codes 0x04, 0x07 | undocumented | hang (07 hangs in place) | fault (not in table) | `E/src/eve-copro.cpp` table, `BL/copro/ft_copro.c:53-83` |
| no-op undocumented 03, 35, 3D, 42, 44, 47-4D, 4F, 60, FF | various | (per table) | consume params, no effect | `BL/copro/ft_copro.c:53-83` |
| Unknown code | fault | fault, mark 0x04F2 at RAM_CMD[0] | fault, mark 0x04F2 at RAM_CMD[0], INT_CMDEMPTY, REG_CMD_READ = 0xFFF | `BL/copro/ft_copro.c:319-334`, `E/src/eve-copro.cpp:537-539`, `T/Docs/ft812-emulation.md:199-211` |
| Inflate error | fault | fault | mark 0x2074 | `BL/copro/ft_copro.c:319-334` |
| Other faults | - | - | DL_FULL (> 2048 words), 3D, IMAGE | `BL/copro/ft_copro.h` fault enum |
| Defaults after COLDSTART | bg 0x002040, fg 0x003870, grad 0xFFFFFF | same | same; scratch 15, base 10 | `BL/copro/ft_copro.c:346-` |
| CMD_TEXT with ROM font before ROM_FONTROOT valid | ROM always present | ROM always present | waits up to 6 s (`FTC_ROM_WAIT_MS` 6000), then draws without fonts; fonts 16-31 preset; matches bt8xxemu on 312 cases | `T/Docs/ft812-emulation.md:109-131`, `BL/copro/ft_copro.h` |
| Text length | - | - | `FTC_TEXT_MAX` 256 | `BL/copro/ft_copro.h` |
| CP_INFO | none | none | BL616 writes 0x3FFFFC on the link -> LINK 0x30 (`C0`/version/error/state/counter); firmware 41 = `C0290201` | `T/Docs/ft812-emulation.md:224-241`, `BL/README.md:22-42` |
| 3D diag | - | - | `FTC3D_DIAG_ADDR` 0x1DFE00 (inside RAM_G) | `BL/copro/ft_copro.h` |

#### 1.4.1 3D extension A0-AF (VDAC4-only)

Spec: `BL/docs/3d-api-v3.md`. Opcodes: `BL/copro/vdac3d.h:6-21`. Parameter counts: `BL/copro/ft_3d.c:138-156`.

| Code | Name | Params | Ref |
|------|------|--------|-----|
| A0 | TARGET (addr, w, h/format; fmt 0 ARGB1555, 3 L8, 4 RGB332, 5 ARGB2 dithered, 7 RGB565, `|0x100` SSAA) | 3 | `3d-api-v3.md:69-83`, `vdac3d.h:29-32` |
| A1 | PROJECT: x' = w/2 + f*x/z, y' = h/2 - f*y/z | 2 | `:85-89` |
| A2-A7 | IDENTITY 0, TRANSLATE 3, ROTATE 3, SCALE 3, PUSH 0, POP 0; M = M*Rz*Ry*Rx | | `:91-103` |
| A8 | LIGHT | 2 | `:105-109` |
| A9 | CLEAR (ARGB) | 1 | `:111-114` |
| AA | MESH (int16 xyz + pad; faces a,b,c,RGB565; flags 1 cull, 2 flat, 4 Gouraud, 8 static cache) | 4 | `:116-132`, `vdac3d.h:28,34-35` |
| AB | RENDER; REG_CMD_READ advances only after the last strip is written | 0 | `:134-138` |
| AC | INFO: writes info + stats (triangles, ms<<16) into the ring like GETPROPS | 2 | `:140-146` |
| AD | TEXTURE (formats 0-3) | 4 | `:148-169`, `vdac3d.h:22-25` |
| AE | TEXMESH (16-byte vertex: XYZ Q8.8, UV Q12.4, RGB Q4.12) | 4 | `:171-191`, `vdac3d.h:36-40` |
| AF | MATERIAL (fog, quantisation, alpha threshold default 128) | 5 | `:193-211` |

- Version probe: CMD_3D_INFO returns 0x3D010400 (v1), 0x3D020400 (v2), 0x3D030500 (v3, fw 17+; board fw 24). On a real
  FT812 it faults and the program resets the coprocessor via REG_CPURESET (`3d-api-v3.md:16-26`). An emulator without the
  extension must therefore fault exactly like a real chip.
- Limits: 16.16 fixed point, angle unit 1/65536 turn, addresses multiple of 4 and < 0x300000, FTC_FAULT_3D -> 0xFFF;
  1280 triangles, 512 vertices, 1024 faces, stack 8, cache 6144 words / 16 meshes, texture 16384 bytes, target width
  4-1024 (x4), height 1-2048 (`3d-api-v3.md:28-46`). Raster rules: top-left, 24-bit z, sort (`:213-238`).
- Bit-exactness hazard: `rotate()` uses `sinf/cosf`, normalisation uses `sqrtf` (`BL/copro/ft_3d.c:61`, `:171`,
  `:225-239`, `:415`). A host port must not depend on libm for bit-exact output (project rule: no libm transcendentals
  in tables).

#### 1.4.2 Images (`BL/docs/images.md`)

| Item | Real FT812 | eve-emu | VDAC4 | Ref |
|------|-----------|---------|-------|-----|
| JPEG | baseline | baseline only | + progressive, 4:2:0 with smooth chroma upsampling (pixels differ) | `images.md:74-93`, `E/src/eve-copro-media.cpp:304-305` |
| PNG | 8-bit, no Adam7 | 8-bit only, no Adam7, no grey+alpha | + 1/2/4/16-bit, Adam7; tRNS ignored in LOADIMAGE, as on FT812 | `images.md:95-109`, `E/src/eve-copro-media.cpp:288-290` |
| WebP | no | no | yes (fw 40) | `images.md:111-122`, `T/Docs/ft812-emulation.md:675-690` |
| OPT_FULLSCREEN (8) | scale to HSIZE/VSIZE | scale by HSIZE/VSIZE, tunable | **VDAC4 meaning**: fit into a 1024x768 RGB565 canvas (1.5 MB) | `images.md:25-40`, `E/src/eve-copro-media.cpp:384-390`, `E/src/eve-tunables.h:166` |
| OPT_MEDIAFIFO with LOADIMAGE | yes | (yes) | unsupported | `images.md:25-40` |
| GETPROPS after LOADIMAGE | end ptr | - | JPEG: start of output; PNG/WebP: end | `images.md:25-40` |
| CMD_LOADIMAGE_BOX 0xFFFFFFB0 (ptr, options, box, room) | no | no | yes (fw 39) | `images.md:42-53` |
| CMD_IMAGEINFO 0xFFFFFFB1 (returns 4 words) | no | no | yes | `images.md:55-64` |
| Max side | - | - | 2047 | `images.md:138-145` |
| Error report | fault | fault | RAM_CMD word `0x00474D49 | code<<24`, a number, then an ASCII string; codes 1-5 | `images.md:213-233` |

### 1.5 Where a VDAC4 profile hooks into eve-emu

eve-emu is a single-model library: `EveModel` has only `EVE_MODEL_FT812` (`E/include/eve/eve.h`). `EveConfig` has
`structSize` "for later extension", `model`, `externalClockHz`, `romImage`, `romImageSize`, `decoders` (same file).
The architecture doc already plans "one chip table ... RAM_G size, ROM layout, available commands" as table flags
(`ARCH:626-636`). The `ChipTable` struct is `{model, name, ramGSize, bases, chipId, registers}`
(`E/src/eve-internal.h:127-135`), chosen in `GetChipTable` (`E/src/eve-registers.cpp:191`, table `:139-141`) and
stored in `chip->table` (`E/src/eve-chip.cpp:130`).

| Hook | Change for VDAC4 | Location |
|------|------------------|----------|
| H1 model | add `EVE_MODEL_VDAC4` (or a flags word in `EveConfig`, size-guarded by `structSize`) and a `ChipTable` row | `E/include/eve/eve.h`, `E/src/eve-registers.cpp:139-141,191` |
| H2 RAM_G size | replace the global `kRamGSize` (1 MB) by `table.ramGSize`; 2 MB, runtime switch to 3 MB (FT_CFG bit 0 equivalent) | `E/src/eve-internal.h:23`, `E/src/eve-memory.cpp` (BusWrite/BusPeek) |
| H3 ROM window | ROM fonts at 0x200000-0x2FFFFF (read-only) in 2 MB mode; absent (RAM) in 3 MB mode; ROM_FONTROOT may be zero until loaded (fonts appear later) | `E/src/eve-internal.h:24-27` (`kRomFontBase` 0x1E0000), `RomByte` |
| H4 reset | do not clear RAM_DL / RAM_CMD on core reset; ignore host traffic for 128 clocks | `E/src/eve-chip.cpp:34-43` |
| H5 power-on | start ACTIVE with 64 MHz system clock (when emulating the "already switched to slot 1" state) | power state init |
| H6 PALETTED8 | per-channel colour from ARGB8888; low 2 bits of PALETTE_SOURCE ignored | `E/src/eve-bitmap.cpp:192-197`, `:397-410`, `P/README.txt` |
| H7 command table | add PROGRESS, A0-AF, B0, B1; remove widgets (already absent), SETFONT2, ROMFONT, PLAYVIDEO; 04 / 07 fault instead of hanging | `E/src/eve-copro.cpp:47-108` |
| H8 media | OPT_FULLSCREEN = 1024x768 RGB565 canvas; extended JPEG/PNG/WebP; image error words | `E/src/eve-copro-media.cpp:288-305,384-390` |
| H9 renderer omissions (optional) | to match board pixels: skip sloped lines, EDGE_STRIP, MACRO, TEXT8X8/VGA | renderer |
| H10 swap | DETACHED swap is already the eve-timing model; INT_FLAGS read keeps a late swap | `E/src/eve-timing.cpp:147-177`, `RTL/ft_core.sv:747-749` |

---

## 2. LINK register map seen by the ESP

Bus: ESP QSPI master, 40 MHz; frame = command 8 bits, address 32 bits, 8 turnaround clocks; 0x01 write, 0x02 read
(`T/Docs/a3-fpga-framebuffer.md:237-290`). Pins: SCK 77, IO 71/72/55/31, CS 18, IRQ_N 85
(`T/Docs/a3-interface-pinmap.md:24-52`). Authoritative map: comment block `RTL/ts_fb_core.sv:246-432`.

| Addr | Name | Meaning | Personality |
|------|------|---------|-------------|
| 0x00 | ID | "VDC4" = 0x56444334 | all |
| 0x04 | VERSION | personality 01 TS-Config / 02 FT / 05 video; protocol 01; core version | all |
| 0x08 | STATUS | bit 12 = M_SEL | all |
| 0x28 | FT_RSTAT | renderer status | FT |
| 0x2C | FT_RTIME | renderer time | FT |
| 0x30 | CP_INFO | `C0` / version / error / state / counter from BL616 | FT |
| 0x34 | MB_PAGE | mailbox page | |
| 0x38 | FT_FREE | Gray-coded free entries, 512-deep FT_BURST FIFO (from 1.29) | FT |
| 0x3C | SCRATCH | | all |
| 0x40 | CTRL | bit 14 ASPECT, 16:15 SIZE, 17 BLEND, ... | all |
| 0x44 | ADJUST | window adjust | TS-Config |
| 0x48 / 0x4C | flash bridge cmd / status | 64-byte buffer, cmd [6:0] tx, [14:8] rx, 16 keep CS | all |
| 0x50 | FT_SEEN | Evo FT traffic seen (after reset reads 00680000 = CORERST only) | |
| 0x54 | FT_ADDR (W) / FT_COUNT (R) | ESP window into FT address space | FT |
| 0x58 | FT_DATA | | FT |
| 0x5C | FT_STAT (R) / FT_CFG (W) | STAT: 31 busy, 30 INT_N, 29 DL bank, 28 copro reset, 27 3 MB, 26 late, 25 drop, 24 BL616 queue overflow, 23:16 state, 14 lost, 13 no dither, 12 ESP host, 11:0 CMD_WRITE. CFG: 0 = 3 MB, 1 = no dither, 2 = ESP host, 3 = ROM write | FT |
| 0x60-0x7C | ADJ_TAB | | TS-Config |
| 0x80-0xBC | FLASH_BUF | 16 words | all |
| 0xC0 / 0xD0 | OVL CTRL (overlay 0 / 1) | | all |
| 0xC4 / 0xC8 / 0xCC | OVL POS / SIZE / BASE | | all |
| 0xE0 / 0xE4 | OVL_PAL / OVL_STAT | | all |
| 0xE8 | FT_HOST | ESP host command: 00 ACTIVE, 44, 61/62, 68; parameter in 15:8 | FT |
| 0xEC | SD_TRAIN | | |
| 0xF0 | BOOT | R: [7:0] own slot, [15:8] next slot, 16 Evo, 17 FT, 18 armed, 19 reboot requested, 31:20 = 0xB00. W: "BOOT" 0x424F4F54, "ARM1" 0x41524D31, "ARM0" 0x41524D30 | all |
| 0x100.. | SD bridge | | TS-Config only |
| 0x200-0x3FC | MBOX | | |
| 0x40000-0x7FFFF | FT_BURST | streaming writes into FT space (~13 MB/s total window) | FT |
| 0x80000-0xFFFFF | OVL_MEM | overlay canvas, SDRAM words 0x108000-0x127FFF, RGB565 | all |

Refs: `RTL/ts_fb_core.sv:246-432`; FT window speed `T/Docs/ft812-emulation.md:60-67`; FT_BURST/FT_FREE
`T/Docs/ft812-emulation.md:660-668`; FT_HOST implementation `RTL/ft_core.sv:725-732`.

### 2.1 Overlays

- Canvas: SDRAM words 0x108000-0x127FFF, RGB565, written via LINK 0x80000-0xFFFFF; transparency 0-15
  (`T/Docs/a3-fpga-framebuffer.md:141-190`).
- Overlay 0: settings menu 336x136, x2. Overlay 1: player 640x360, x2. UI_KEY codes 1-6 (same ref).
- Overlays are composited after scaling and are invisible to Evo software. **Cosmetic** for the Evo; an emulator models them
  only to show the ESP menu.

### 2.2 Core load / multiboot

- Slots: 1 MB each in the XT25F64B (8 MB) (`T/Docs/a3-fpga-framebuffer.md:302-324`, `:337-376`).
- The ESP writes "ARM1" then "BOOT" to 0xF0. The core pulls RECONFIG_N low; from 1.13 the BL616 does the reboot through JTAG
  Reconfig (`RTL/ts_fb_hdmi_top.sv:47-49`, `T/Docs/a3-fpga-framebuffer.md:337-376`).
- Load time: about 2.9 s at 2.5 MHz, about 174 ms at 41.667 MHz. Slot 1 loads fast from 1.18, slot 0 from 1.22 (same ref).
- After the FT core loads, the ESP copies VD4FONT1 (286 KB) from slot 2 into SDRAM, writing ROM_FONTROOT last
  (`T/Docs/ft812-emulation.md:109-131`).
- After a load the output stays black until the ESP writes CTRL, at most 256 frames (~4.3 s) (`FMT_WAIT_LOG2 = 8`,
  `RTL/ts_fb_core.sv:74-94`).

### 2.3 Settings in flash

- Sector 0x7FF000 (4 KB): 128 cells x 32 bytes, eight big-endian words: 0 MAGIC "VDST"; 1 MUX CLOCK/DELAY/PHASE/AUTO
  ALIGN, format, bit 16 ASPECT, 18:17 SIZE, 19 BLEND; 2-5 window adjust per raster; 6 [1:0] FT SIZE (ESP 0.1.104; core only
  checksums it); 7 = ~sum(0..6). Last valid cell wins; torn cells fail the checksum (`RTL/fb_flash.sv:4-15`,
  `T/Docs/a3-fpga-framebuffer.md:302-324`).
- The core loads settings itself 1 ms after reset; the ESP writes cells through the bridge (0x48/0x4C, 0x80-0xBC)
  (`RTL/fb_flash.sv:17-21`, `:26-30`).
- The Evo cannot see any of this. An emulator can keep these settings as plain host config.

---

## 3. Video switching

### 3.1 M_SEL

- M_SEL is TS-Config VCONFIG (#00AF) value #04: "picture from FT812" (`ESP/Network/Weather screensaver VDAC2/src/video.asm:4-12`,
  `ESP/Network/ESP App SPG/README.md:19,27`). Pin FPGA 73 (`T/Docs/a3-interface-pinmap.md:24-52`). LINK STATUS bit 12
  mirrors it (`RTL/ts_fb_core.sv:246-432`).
- In the TS-Config core (slot 0), M_SEL=1 blanks the Evo picture (`blank` from `RTL/ts_fb_hdmi_top.sv:251`) and holds the
  idle timer, so the FT core is never left (`RTL/ts_fb_core.sv:561-590`).
- In the FT core, M_SEL=0 shows black (`m_ft` / `sc_blank`, `RTL/ts_fb_core.sv:756`, `:1544-1546`;
  `T/Docs/a3-fpga-framebuffer.md:378-408`).
- Hardware: FT_INT_N reaches the Evo (IDE15) only when M_SEL=1 (U11 OE = M_SEL) (`HW/A5/KiCad/CHANGES_A5.md:36-58`).
  MISO goes through U8 with OE = MISO_EN (`HW/A5/KiCad/CHANGES_A5.md:36-58`, `RTL/ws_dark.sv:31`).

### 3.2 TS-Config -> FT core (auto-switch)

- Trigger, in the slot-0 core: an FT812 ACTIVE command or a memory access of at least 16 SCK while armed ("ARM1"). Plain host
  commands (CORERST, CLKEXT, CLKSEL) do not trigger it from 1.16 (`T/Docs/a3-fpga-framebuffer.md:378-408`). RTL:
  `go = (24 bits all zero) || (>= 32 bits && fs_sr[30] == 0)`; `evo_rst = 68 00 00` (`RTL/ts_fb_core.sv:495-554`).
- In slot 0, MISO reads 0 (no FT812 there) (`T/Docs/a3-fpga-framebuffer.md:378-408`).
- The slot-1 core loads in about 174 ms and starts ACTIVE at 64 MHz, because CORERST/CLKEXT/CLKSEL/ACTIVE went into a core
  without an FT812 (`RTL/ft_core.sv:33-37`). Until the load ends, reads return 0. Software that polls REG_ID until it reads
  0x7C (as all VDAC2 software does, `E/src/eve-tunables.h:30-33`) works; RAM_G written before the switch is lost.
- **Software-visible**: REG_ID reads 0 for about 0.2-0.3 s after the first ACTIVE or memory access; register values are the
  reset values of a core that is already ACTIVE.

### 3.3 FT core -> TS-Config (fallback)

- Condition: M_SEL=0 and no Evo FT SPI traffic for `SWITCH_IDLE_MS = 500` ms (3000 before 1.22), unless the ESP is host
  (`RTL/ts_fb_core.sv:74-94`, `:561-590`; `T/Docs/ft812-emulation.md:600-602`; `T/Docs/a3-fpga-framebuffer.md:368-371`).
- Stale docs still say 3 s: `T/Docs/ft812-emulation.md:58`, `T/Docs/a3-fpga-framebuffer.md:396`,
  `ESP/Network/ESP App SPG/README.md:27-33`, `DOOM-VDAC4:include/zm_app.h:150`. The RTL value is 500 ms.
- The video core (slot 3) returns to slot 0 by the same rule (`T/Docs/video-core.md:104-109`).
- **Software-visible**: after the return, RAM_G, RAM_DL and the registers are gone; the next FT access reloads slot 1 (3.2).

### 3.4 Evo reset

- An Evo reset brings back the TS-Config picture. The marker is TS-Config clearing `vdac2_msel` (M_SEL -> 0); the FT core then
  falls back after the idle time (`T/Docs/ft812-emulation.md:60-67`).
- FT_SEEN after a reset reads 00680000 (CORERST only) (`T/Docs/a3-fpga-framebuffer.md:465-490`).

### 3.5 ESP host mode

- FT_CFG bit 2 (core 1.12). The Evo SPI is disconnected (`spi_cs_n | ft_cfg_esp`), Evo MISO reads 0, INT_N is held
  inactive, the idle timer is frozen (`RTL/ts_fb_core.sv:618-622`, `:887-891`; `T/Docs/ft812-emulation.md:52-59`).
- **Software-visible**: while an ESP app runs (ESP App SPG), every Evo read from the FT returns 0.

### 3.6 Output, scaling, ASPECT/SIZE, genlock (all cosmetic)

- FT core output: 1366x768 reduced blanking (VESA DMT, 72 MHz, 60.00 Hz) from 1.41, 720p60 before. The 1024x768 frame is
  shown pixel for pixel at 4:3/100 %; 16:9 is 1360x768. FT SIZE is its own menu item (`RTL/ts_fb_hdmi_top.sv:120-132`,
  `:220-232`; `T/Docs/ft812-emulation.md:22-28`).
- Frames smaller than 640 px wide are upscaled; other sizes use the nearest class (`RTL/fb_scanout.sv:24-56`, OUT768
  `:46-50`). So the RTL does scale 640x480 even though the doc calls it "not supported".
- TS-Config core output: 1280x720p50 (VIC 19) from the 27 MHz crystal. A frame repeats about every 0.85 s (48.83 -> 50 Hz).
  ASPECT/SIZE gives 960x720 or 1280x720 at 100/95/90 %, area-weighted; CTRL bits 14 and 16:15. FRAME BLEND is CTRL 17.
  AVI InfoFrame, DRIVE=4 (`T/Docs/a3-fpga-framebuffer.md:9-16`, `:40-139`).
- Genlock (`T/Docs/a3-fpga-video.md`, `RTL/ts_hdmi_top.sv`) is the older single-core design: 896x640 at 48.83 Hz from PCLK.
  With M_SEL=1 it outputs a black field and keeps sync, and FT_INT_N is held inactive (`T/Docs/a3-fpga-video.md:47-50`).
  The multiboot design does not use it.
- None of this reaches Evo software. The FT812 timing that software sees (REG_FRAMES, INT_SWAP) comes from the FT812 registers
  (1024x768@59 = 1344*806/64 MHz), not from the HDMI output (`RTL/ft_core.sv:302-334`, `T/Docs/ft812-emulation.md:162-177`).

### 3.7 Visibility summary

| Visible to Evo software | Cosmetic only |
|-------------------------|---------------|
| Load delay on the first FT access (~0.2-0.3 s, MISO 0, REG_ID 0) | HDMI mode 1366x768 / 720p50, VIC |
| RAM_G/DL/registers lost after M_SEL=0 + 500 ms silence | ASPECT, SIZE, FT SIZE, BLEND, DRIVE |
| ESP host mode: MISO 0, INT_N inactive | Overlays (menu, player) |
| INT_N only with M_SEL=1 | Frame drops, RAM_G-write cancels, dither |
| 28 MHz RAM_G read may be late (wrong first byte) | Renderer omissions (sloped lines, EDGE_STRIP, MACRO) unless pixels are compared |
| RAM_G 2/3 MB, ROM fonts at 0x200000, ROM_FONTROOT appears after load | Settings flash |
| Coprocessor command set, 3D, images, fault marks | Rasterizer tile size, SDRAM layout |
| RAM_DL/RAM_CMD not cleared on core reset | |
| PALETTED8 1-pass colour (pixels only) | |

---

## 4. Evo-side SPI access to the FT core

- Port: Z-controller SPI; FT812 chip select is port #77 bit 2 (`RTL/ts_fb_hdmi_top.sv:62-67`). SCK is shared with SD and the
  ESP (same ref). Pins: FT_CS_N 17, SCK 20, MOSI 19, MISO 79 (also WS2812 DIN), FT_INT_N 74 (`T/Docs/a3-interface-pinmap.md:24-52`).
- The protocol is standard FT812: 3-byte address, a dummy byte on reads, host commands as 3 bytes (`RTL/ft_core.sv:9-12`,
  `:674-681`, `RTL/ts_fb_core.sv:495-554`).
- Event rate: at most once per 8 SCK (286 ns at 28 MHz, 17 core clocks); a read answer is ready in about 6 clocks
  (`RTL/ft_core.sv:9-12`).
- Address map seen by the Evo: RAM_G 0x000000 (2 or 3 MB); ROM_CHIPID 0x0C0000; ROM fonts 0x200000-0x2FFFFF (2 MB mode);
  RAM_DL 0x300000; registers 0x302000; REG_TRACKER / MEDIAFIFO 0x309000; RAM_CMD 0x308000
  (`T/Docs/ft812-emulation.md:98-107`, `RTL/ft_core.sv:123`, `:384-390`).
- Step-1 bring-up used by VDAC2 software (Weather 2): read REG_ID = 0x7C, set 1024x768@59 (HCYCLE 1344, VCYCLE 806,
  PCLK 1) (`T/Docs/ft812-emulation.md:162-177`).
- The video core (slot 3) also answers the Evo FT SPI: REG_ID 0x7C, a media FIFO, personality 05 (`T/Docs/video-core.md:147-172`).

---

## 5. Unknowns

1. **Coprocessor timing**: how long each BL616 command takes (REG_CMD_READ progress rate). `images.md:235-261` and
   `3d-api-v3.md:240-248` give only model or speed figures, not a per-command cost table.
2. **640x480 at the host level**: the doc says "not supported" (`T/Docs/ft812-emulation.md:36-40`), yet `fb_scanout.sv:24-56`
   scales frames up to 640 px wide. Whether register timing and REG_FRAMES are right for such modes is not tested.
3. **Unaligned PALETTE_SOURCE**: Tang ignores the low 2 bits (`RTL/ft_rd_exec.sv:1785-1807`). The real chip reads the byte
   at the offset (one channel per pass). Only the 4-pass and 1-pass cases are documented (`P/README.txt`); other offsets
   are untested.
4. **Firmware source vs board**: CP_INFO C0290201 = firmware 41 (`BL/README.md:22-42`), but the v3 3D code was built in a
   separate (UNDERRUN) project. Whether `BL/copro` is exactly firmware 41 is **UNKNOWN**.
5. **REG_ID delay**: constant 0x7C in RTL, so there is no delay after the core loads; the load delay itself (~174 ms plus font
   copy) has no exact board measurement as seen from the Evo.
6. **Rasterizer tile size**: 128x8 (`ft_rd_walk.sv:3-4`) vs 64x16 (`ft_render.sv:1-27`).
7. **Font copy vs first CMD_TEXT**: whether a program that issues CMD_TEXT right after the switch sees a 6 s wait on the board
   (`FTC_ROM_WAIT_MS`) or whether the ESP copy always finishes first.
8. **INT_FLAGS / power edge cases** (STANDBY wake, CLKSEL outside 6-10): taken from eve-emu tunables marked "TO VERIFY"
   (`E/src/eve-tunables.h:25-40`).
9. **The real FT812 on RST_PULSE**: the Tang author says RAM_DL/RAM_CMD are not cleared (`RTL/ft_core.sv:21-23`); eve-emu
   clears them. Neither side cites a hardware measurement.
