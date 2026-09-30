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
#   → SPG v1.0 / v1.1, MegaLZ / Hrust blocks; TS-Conf only

capture_media {"type":"screenshot"}                  # 720x288 for every TS mode
```

## WebAPI

```bash
EMU=$(curl -s -X POST "$BASE/emulator/start" -H 'Content-Type: application/json' \
      -d '{"model":"TSL"}' | jq -r .id)
curl -s "$BASE/emulator/$EMU/state/tsconf" | jq '.video, .memory.pages'
curl -s -X POST "$BASE/emulator/$EMU/snapshot/load" -H 'Content-Type: application/json' \
     -d '{"path":"testdata/machines/tsconf/spg/sprites.spg"}'
# The SD card is the media manager's slot "sd.zc" (image or host folder):
curl -s -X POST "$BASE/emulator/$EMU/media/sd.zc/insert" -H 'Content-Type: application/json' \
     -d '{"path":"/path/to/sdcard-or-folder"}'
```

CLI: `state tsconf`; Lua / Python: `tsconf_state()`.

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
| SPG programs (`.spg` v1.0 / v1.1) | implemented (pager / resident fields not used) |
| TTD: all TS-Conf state in blob 16, SD card 15, CMOS 18, IDE 17 | implemented |
| VDAC colour curves, TSU render timing within the line, cache / I/O wait states at 14 MHz | not yet (implementation-plan phases 7-8) |
| Video debug mapper (`/video/*` pixel ↔ memory) for TS modes, TS-specific Qt docks | not yet |
