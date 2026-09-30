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
#   → boots TS-BIOS from rom/zxevo.rom (page 0). With a blank CMOS the BIOS
#     opens its Setup Utility (text mode); ENTER changes an option and saves
#     the NVRAM, after a reset it boots the default (TR-DOS)

inspect_state {"aspects":["tsconf"]}
#   → memory (MEM_CONFIG decoded, window pages, LCK128, lock48, DOS / vdos,
#     cache, FM window), video (mode ZX/16C/256C/TXT, geometry, V_PAGE,
#     PAL_SEL, BORDER, TSU enables and pages, the engine's current line),
#     interrupts, DMA, CPU clock, SD card

load_software {"path":".../program.spg"}              # a TS-Conf SDK program
#   → SPG v1.0 / v1.1, MegaLZ / Hrust blocks. On another model the machine is
#     switched to TSL first (media kept): the answer's emulator_id is the NEW
#     instance - use it from then on (model_switched, previous_emulator_id)

capture_media {"type":"screenshot"}                  # 720x288 for every TS mode
```

## WebAPI

```bash
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
      -d '{"model":"TSL"}' | jq -r .id)
curl -s "$BASE/emulator/$EMU/state/tsconf" | jq '.video, .memory.pages'
curl -s -X POST "$BASE/emulator/$EMU/snapshot/load" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/tsconf/spg/sprites.spg"}' | jq .emulator_id
# (from any model: switches to TSL; "switch_model":false refuses with 409)
# The SD card is the media manager's slot "sd.zc" (image or host folder):
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/sdcard-or-folder"}'
```

CLI: `state tsconf`, `snapshot load x.spg [--no-switch]`; Lua / Python:
`tsconf_state()`, `snapshot_load` / `unreal.snapshot_load`. unreal-qt: Machine
menu → TS-Conf; opening or dropping an `.spg` switches to TS-Conf.

### Wild Commander (the TS-Conf shell) from the SD card

```bash
# The packages and ready SD images are untracked test data:
#   testdata/machines/tsconf/wildcommander/ (README there)
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/tsconf/wildcommander/sd-images/wc-tslabs-v1.11rc7.img"}'
# Blank CMOS -> TS-BIOS Setup. Select "Reset to" (3 x CAPS SHIFT+6) and press
# ENTER 3 x (ROM #00 -> ROM #04 -> RAM #F8 -> BD boot.$c); ENTER saves NVRAM.
# Then reset: WC (text mode) comes up with both panels on the card's root.
curl -s -X POST "$BASE/emulator/$EMU/reset"
```

The CMOS has no NVRAM file in the ts-conf config: every new instance starts
with blank NVRAM (Setup first). From the IDE master instead: insert the image
into `ide0.master`, then also set "Boot Device" (4 x CAPS SHIFT+6 further, 1 x
ENTER: SD Z-contr -> IDE Nemo M) before the reset; WC's panels open the drive
named in `WC/wc.ini` (`DRV=1` for the IDE master). Setup options, boot devices
and IDE details: `docs/inprogress/2026-09-27-tsconf/boot-and-storage-notes.md`.

### What works / what doesn't

| Area | State |
|:--|:--|
| Memory: `#xxAF` registers, window 0 normal / mapped mode, W0 RAM / write enable, 4 MB windows, `#7FFD` with LCK128 (512K / 128K / auto / 1024K) and lock48, FM window (CRAM / SFILE / registers), CPU cache | implemented |
| DOS trap (`#3Dxx` in mapped mode), Beta-128 gated by DOS or VG_OPEN, virtual TR-DOS (RAM page `#FF`) | implemented |
| Interrupts: frame INT at VS_INT / HS_INT (32-clock pulse), 320 line INTs, DMA INT, vectors `#FF/#FD/#FB`, masks; CPU clock 3.5 / 7 / 14 MHz | implemented |
| Video: ZX, 16C, 256C, TXT in the four geometries, X/Y offsets, line-latched registers, CRAM colors (no-VDAC curve), 720x288 framebuffer | implemented |
| TSU: tile layers with the prefetch ring, sprites (layers, LEAP, 85 cap), mixing (NOTSU / NOGFX / GFXOVR, 360-wide window) | implemented |
| DMA: RAM copy, BLT1, fill, CRAM, SFILE, SPI, IDE; the per-line DRAM budget (video, TSU, CPU reads); TSU starvation | implemented (CPU writes are not counted in the budget) |
| SD card (`#57` / `#77`, slot `sd.zc`), Nemo IDE (`[HDD] Scheme=NEMO-DIVIDE`, `IdeStall`), Gluk CMOS | implemented |
| SPG programs (`.spg` v1.0 / v1.1) | implemented (pager / resident fields not used); opening one on another model switches to TSL on every surface |
| Sound: AY / TurboSound, one 8-bit DAC shared by Covox `#FB` and the `#FE` beeper bit | implemented |
| Wild Commander from SD or the Nemo IDE master (TS-BIOS "BD boot.$c", Boot Device) | works (tests BOOT-3, BOOT-4); WC's panels use the drive in `WC/wc.ini` (`DRV=0` SD, `1` IDE master) |
| PS/2 keyboard (the AVR's scan code log; Wild Commander reads only this) | implemented (host keys and automation typing) |
| TTD: all TS-Conf state in blob 16, SD card 15, CMOS 18, IDE 17; DMA writes tracked | implemented (corpus fixture `testdata/machines/tsconf/ttd/sprites.ttd`) |
| 14 MHz timing: DRAM waits on uncached reads / cache misses (zmem.v phase logic) and the DRAM arbiter (video refusing the CPU in the fetch window), 8-fclk AY / VG93 I/O stall; DMA word costs | implemented (phase 8) |
| VDAC color curves, TSU render timing within the line | not yet |
| Video debug mapper (`/video/layout`, `/video/pixel`, `/video/address`, `/video/text`; CLI `video ...`, Lua / Python `video_*`) | graphics layer (layer 0) and the TSU (layer 1 "tsu": the object, its SFILE / tilemap words, graphics byte, CRAM) in 14 MHz pixels; `/video/address?space=sprite_ram|palette` |
| TS-specific Qt docks | not yet (the model-first debugger) |
