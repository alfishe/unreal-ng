# Recipe: TS-Conf (ZX-Evo with the TS-Labs configuration)

TS-Conf is the ZX-Evo board with TS-Labs' FPGA configuration: 4 MB RAM, a
512 KB ROM with TS-BIOS, 3.5 / 7 / 14 MHz, the TS video modes (ZX, 16-color,
256-color, text) in four geometries up to 360x288, the TSU (two tile layers,
85 sprites), a DMA engine, a programmable interrupt controller, the Z-Controller
SD card, the Nemo IDE, Beta-128 with virtual drives and the Gluk CMOS.

Ground truth:
[portdecoder_tsconf.h](../../core/src/emulator/ports/models/portdecoder_tsconf.h)/[.cpp](../../core/src/emulator/ports/models/portdecoder_tsconf.cpp),
the engine, TSU, DMA and interrupt controller in
[core/src/emulator/platforms/tsconf/](../../core/src/emulator/platforms/tsconf/),
the design and status in
[docs/inprogress/2026-09-27-tsconf/](../../docs/inprogress/2026-09-27-tsconf/)
(`hardware-spec.md` for register semantics, `implementation-plan.md` for what
is built phase by phase).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use
> [WebAPI](#webapi) inside host-side pipelines (policy:
> [_common/transports.md](../_common/transports.md)). Shared patterns:
> [_common/machines.md](../_common/machines.md).

## MCP (preferred)

```text
emulator_manage {"action":"create","model":"TSL"}      # alias TSCONF; 4096K only
#   → boots TS-BIOS from rom/zxevo.rom (page 0). With no NVRAM file the BIOS
#     starts with "Reset to: BD boot.$c": it boots Wild Commander (boot.$C) from
#     the SD card in sd.zc ([EVO] TsBiosNvram=SDBOOT, the default). Without a
#     card it shows "Boot-Device NOT READY". [EVO] TsBiosNvram=SETUP gives the
#     blank CMOS of a new board: the Setup Utility first

inspect_state {"aspects":["tsconf"]}
inspect_state {"aspects":["tsconf_tsu"]}             # sprites (85 decoded), tile layers, CRAM
#   → memory (MEM_CONFIG decoded, window pages, LCK128, lock48, DOS / vdos,
#     cache, FM window), video (mode ZX/16C/256C/TXT, geometry, V_PAGE,
#     PAL_SEL, BORDER, TSU enables and pages, the engine's current line),
#     interrupts, DMA, CPU clock, SD card

load_software {"path":".../program.spg"}              # a TS-Conf SDK program
#   → SPG v1.0 / v1.1, MegaLZ / Hrust blocks. On another model the machine is
#     switched to TSL first (media kept): the answer's emulator_id is the NEW
#     instance - use it from then on (model_switched, previous_emulator_id)

capture_media {"action":"screenshot"}                # whole frame: 720x288 for every TS mode
capture_media {"action":"screenshot","area":"screen"} # only the graphics window (below)
```

`area=screen` returns the graphics window named by `V_CONFIG`: 512x192, 640x200,
640x240 or 720x288 pixels. These are pixel counts of the saved image: a TS frame
stores 2 pixels per raster dot, so the window is twice as wide as the dot count.
Example: a 256x192 TS-Conf mode gives a 512x192 `area=screen` image. Details:
[agent-screenshot-view.md](../media/agent-screenshot-view.md).

## WebAPI

```bash
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
      -d '{"model":"TSL"}' | jq -r .id)
curl -s "$BASE/emulator/$EMU/state/tsconf" | jq '.video, .memory.pages'
curl -s "$BASE/emulator/$EMU/state/tsconf/tsu" | jq '[.sprites[] | select(.active)]'   # the visible sprites
# Debugger board: the write-only registers as last written, DMA as programmed vs live, the line's tile pages
curl -s "$BASE/emulator/$EMU/state/tsconf" | jq '{sys_config, cache_en, fm_maps: .memory.fm_maps,
     dma: (.dma | {programmed_source, source, programmed_destination, destination, ctrl}),
     line_tiles: (.video.line | {t0_gpage, t1_gpage}), dma_regs: .regs[26:32]}'
curl -s "$BASE/emulator/$EMU/video/pixel?layer=1&x=100&y=50" | jq '.layer, .sources'  # which TSU object drew a pixel
# Palette and sprite table as memory regions "cram" / "sfile" (512 bytes each: word n at offset 2n, low byte first;
# CRAM word = bits 14-10 R, 9-5 G, 4-0 B, bit 15 VDAC; sprite d = SFILE words 3d..3d+2 - the FM window's layout)
curl -s "$BASE/emulator/$EMU/memory/region/cram?offset=0&length=32&format=hex" | jq -r .hex       # colors 0-15
curl -s -X POST "$BASE/emulator/$EMU/memory/region/cram" -H 'Content-Type: application/json' \
     -d '{"offset":2,"hex":"1F00"}'                                                            # color 1 = pure blue
# A page register as the program sets it: a port write through the decoder (RAM page #20 into window 3)
curl -s -X POST "$BASE/emulator/$EMU/ports/out" -H 'Content-Type: application/json' -d '{"port":"0x13AF","value":"0x20"}'
curl -s -X POST "$BASE/emulator/$EMU/snapshot/load" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/tsconf/spg/sprites.spg"}' | jq .emulator_id
# (from any model: switches to TSL; "switch_model":false refuses with 409)
# The SD card is the media manager's slot "sd.zc" (image or host folder):
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/sdcard-or-folder"}'
```

CLI: `state tsconf`, `state tsconf tsu`, `snapshot load x.spg [--no-switch]`; Lua / Python:
`tsconf_state()`, `tsconf_tsu()`, `snapshot_load` / `unreal.snapshot_load`. unreal-qt: Machine
menu → TS-Conf; opening or dropping an `.spg` switches to TS-Conf.

### Wild Commander (the TS-Conf shell) from the SD card

**Out of the box:** the shipped `ts-conf` config (`TSL` and `TSL-VDAC2` both use it) starts with a ready SD card in
`sd.zc`: Wild Commander 1.11i and a folder `zifi/` (`zifi.spg` + `zifi.ini`), and with a ZiFi board
(`[NETWORK] ZiFi=ZIFI-NATIVE,S3`), so a fresh instance boots to WC and `zifi.spg` runs from it
([demo](../peripherals/network.md#demo-the-zifi-browser-on-the-real-internet)). The card is `data/configs/ts-conf/wc-zifi.img.7z`
(427 KB); the build unpacks it next to the copied config (`cmake/ExtractConfigImages.cmake`), so `bin/configs/ts-conf/wc-zifi.img`
(35 MB: the smallest FAT32, 512-byte clusters, volume label `UNREAL NG`) exists only in build output and packages. `[ZC] SDWrite=session`: writes live until exit and the image stays as
shipped. The card holds WC 1.11i (`boot.$C`, `WC/`), `zifi/zifi.spg` (ZiFi client 0.733) and `zifi.ini` (SSID `UnrealNG`). To
rebuild the archive: copy the files out of the old image (`mcopy -s -m -n ::* dir/`), then
`mformat -i wc-zifi.img -F -T 69000 -h 255 -s 63 -c 1 -v "UNREAL NG" ::` (69000 sectors is just above the 65525 clusters FAT32
needs), `mcopy -s -m -n * ::/`, and `7zz a -t7z -mx=9 wc-zifi.img.7z wc-zifi.img`.
Other card or none: `[ZC] SDCARD=` or the media commands below.

```bash
# The packages and ready SD images are untracked test data:
#   testdata/machines/tsconf/wildcommander/ (README there)
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/tsconf/wildcommander/sd-images/wc-tslabs-v1.11rc7.img"}'
# Or a host folder holding WC (boot.$C and WC/ in its root) plus games:
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/sd-folder"}'
# Reset: WC (text mode) comes up with both panels on the card's root
curl -s -X POST "$BASE/emulator/$EMU/reset"
```

The CMOS has no NVRAM file in the ts-conf config: every new instance starts
with the settings above ("Reset to: BD boot.$c", the SD card as boot device).
A folder becomes a FAT32 volume from sector 0 with no MBR in front, whose boot
sector carries one partition entry over the volume, as an `mformat`-made SD
image does: TS-BIOS finds the volume only through a partition entry.

With `[EVO] TsBiosNvram=SETUP` (blank CMOS, Setup first): select "Reset to"
(3 x CAPS SHIFT+6), ENTER 3 x (ROM #00 -> ROM #04 -> RAM #F8 -> BD boot.$c),
reset. From the IDE master instead: insert the image into `ide0.master`, then
set "Boot Device" (from Setup: 7 x CAPS SHIFT+6, 1 x ENTER: SD Z-contr -> IDE
Nemo M) before the reset; WC's panels open the drive
named in `WC/wc.ini` (`DRV=1` for the IDE master). Setup options, boot devices
and IDE details: `docs/inprogress/2026-09-27-tsconf/boot-and-storage-notes.md`.

### Entering TS-BIOS Setup directly: Right Shift + F12

A running machine can be sent straight into Setup (no reboot needed) the same
way a real ZX-Evo keyboard does it: **Right Shift + F12**. Neither half is a
ZX Spectrum matrix key - F12 does not exist on a ZX keyboard at all, and the
matrix has only one "Symbol Shift" (it cannot tell left from right) - so this
combo only works through the physical-PC-key path (`PcKey`,
[pckey.h](../../core/src/emulator/io/keyboard/pckey.h)), not the ZX key names
used for everything else. Every automation surface accepts a combo mixing ZX
and PC-only names freely:

```bash
# MCP (preferred)
type_input {"action":"combo","keys":["rshift","f12"],"frames":2}

# WebAPI
curl -s -X POST "$BASE/emulator/$EMU/keyboard/combo" -H 'Content-Type: application/json' \
     -d '{"keys":["rshift","f12"],"frames":2}'

# CLI
key combo rshift f12

# Lua
key_combo({"rshift", "f12"}, 2)

# Python
emulator.key_combo(["rshift", "f12"], 2)
```

`pckey::FromName` is case-insensitive and also accepts a `"pc."` prefix
(`"pc.f12"`, `"pc.rshift"`) if a name ever collides with a ZX key name
elsewhere. `GET /api/v1/emulator/{id}/keyboard/keys` lists every known name,
ZX and PC-only alike.

**AY module playback flips the CPU clock every frame.** WC's built-in AY
player writes `SYS_CONFIG[1:0]` from the same PC once per frame (verified via
`porttrace`: alternating `0x00`/`0x02` at a fixed address, one frame apart) -
a software "turbo during the heavy part of the frame, normal speed for the
rest" trick, not a bug. NeoGS playback never does this (the hardware card
needs no CPU-side mixing help), so the CPU-frequency indicator only flickers
with AY/TurboSound tracks. `Z80::NotifyCPUFrequencyChanged` classifies this at
the source (see its comment in `core/src/emulator/cpu/z80.cpp`) so a UI shows
a stable "lo<->hi" band instead of chasing every flip.

### What works / what doesn't

| Area | State |
|:--|:--|
| Memory: `#xxAF` registers, window 0 normal / mapped mode, W0 RAM / write enable, 4 MB windows, `#7FFD` with LCK128 (512K / 128K / auto / 1024K) and lock48, FM window (CRAM / SFILE / registers), CPU cache | implemented |
| DOS trap (`#3Dxx` in mapped mode), Beta-128 gated by DOS or VG_OPEN, virtual TR-DOS (RAM page `#FF`) | implemented |
| Interrupts: frame INT at VS_INT / HS_INT (32-clock pulse), 320 line INTs, DMA INT, vectors `#FF/#FD/#FB`, masks; CPU clock 3.5 / 7 / 14 MHz | implemented |
| Video: ZX, 16C, 256C, TXT in the four geometries, X/Y offsets, line-latched registers, CRAM colors (no-VDAC curve), 720x288 framebuffer | implemented |
| TSU: tile layers with the prefetch ring, sprites (layers, LEAP, 85 cap), mixing (NOTSU / NOGFX / GFXOVR, 360-wide window) | implemented |
| DMA: RAM copy, BLT1, fill, CRAM, SFILE, SPI, IDE; the per-line DRAM budget (video, TSU, CPU reads); TSU starvation | implemented (CPU writes are not counted in the budget) |
| SD card (`#57` / `#77`, slot `sd.zc`), Nemo IDE (`[HDD] Scheme=NEMO-DIVIDE`, `IdeStall`), Gluk CMOS (a `#BFF7` access waits for the AVR like `#xxEF`, inside vdos too: [cmos-rtc.md](../peripherals/cmos-rtc.md)) | implemented |
| SPG programs (`.spg` v1.0 / v1.1) | implemented (pager / resident fields not used); opening one on another model switches to TSL on every surface |
| Sound: AY / TurboSound, one 8-bit DAC shared by Covox `#FB` and the `#FE` beeper bit | implemented |
| ZX-MultiSound card in a ZX-bus slot (the socketed YM2149 comes out; not in the shipped config): [multisound.md](../peripherals/multisound.md), [docs/features/multisound.md](../../docs/features/multisound.md) | implemented (TSFM, SAA via VGMPLAY, MIDI via GSPLAYER checked) |
| Wild Commander from SD or the Nemo IDE master (TS-BIOS "BD boot.$c", Boot Device) | works (tests BOOT-3, BOOT-4); WC's panels use the drive in `WC/wc.ini` (`DRV=0` SD, `1` IDE master) |
| PS/2 keyboard (the AVR's scan code log; Wild Commander reads only this) | implemented (host keys and automation typing) |
| TTD: all TS-Conf state in blob 16, SD card 15, CMOS 18, IDE 17; DMA writes tracked | implemented (corpus fixture `testdata/machines/tsconf/ttd/sprites.ttd`) |
| 14 MHz timing: DRAM waits on uncached reads / cache misses (zmem.v phase logic) and the DRAM arbiter (video refusing the CPU in the fetch window), 8-fclk AY / VG93 I/O stall; DMA word costs; DMA CRAM writes land at their dot | implemented (phase 8) |
| TSU timing: line L drawn from ts_start of line L - 1 with that line's latches; on a busy line the objects after line_start of L take L's latches, as the RTL (a mid-line write acts from the next line or the one after) | implemented |
| Firmware build: `[MISC] TS_VDAC` = NONE (default: STATUS VDAC_VER 0, PWM colours) or 3BIT / 4BIT / 5BIT, `TS_VDAC2=1` (VDAC curves, BLT2; the `TSL-VDAC2` variant model sets it with the card, see [tsconf-vdac2.md](tsconf-vdac2.md)); `/state/tsconf` `build{}` | implemented |
| Video debug mapper (`/video/layout`, `/video/pixel`, `/video/address`, `/video/text`; CLI `video ...`, Lua / Python `video_*`) | graphics layer (layer 0) and the TSU (layer 1 "tsu": the object, its SFILE / tilemap words, graphics byte, CRAM) in 14 MHz pixels; `/video/address?space=sprite_ram|palette` |
| TS-specific Qt docks | not yet (the model-first debugger) |
