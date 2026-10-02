# Sprinter automation audit (2026-10-02)

> **Status (2026-10-02, branch `sprinter-automation`):** every P1 and P2 gap is implemented on all five
> surfaces from one core source each (the Status column of §4); G16 / G17 are partly done (owner priority);
> G18-G21 stay P3. Verified against a running unreal-qt (BIOS 3.04 text screen, DSS 1.71 / Flex Navigator
> 1.15 from the MAME HDD on BIOS 3.07, WAVPLAY on BIOS 3.06): recipes
> [sprinter.md](../../../.recipe/machines/sprinter.md) steps 1 and 6,
> [sprinter-sound.md](../../../.recipe/machines/sprinter-sound.md), [sprinter-accelerator.md](../../../.recipe/machines/sprinter-accelerator.md).

Read-only audit: which Sprinter features that work in the core are not yet reachable from the
automation surfaces (WebAPI + OpenAPI, MCP, CLI, Lua, Python), the command reference
([command-interface.md](../../emulator/design/control-interfaces/command-interface.md)), the recipes
(`.recipe/`) and the MCP resource `unreal://machine/sprinter`. Scope: `master` at `db38bb9f3`, plus the
uncommitted working tree of branch `sprinter-s6` (sound, `scratch/wt-sprinter-s6`).

The parity rule: the core builds each report once (`DeviceState::*`, a `StateNode` tree); every
surface only converts it. A field added to `DeviceState::Sprinter` reaches all five surfaces at once.
Most of the gaps below are therefore **one core change + docs**, not five.

Legend: ✔ = there and correct; **partial** = some of it, or only through a generic facility / a
workaround; ✘ = missing. "gen" = the gap is in a facility every machine uses; "SP" = Sprinter only.

Column abbreviations: WA = WebAPI, OA = OpenAPI, MCP, CLI, Lua, Py = Python (off in the default
build: `ENABLE_PYTHON_AUTOMATION=ON`), CI = command-interface.md, Rc = `.recipe/`.

## 1. Where the Sprinter is on the surfaces today (evidence)

| Surface | Sprinter entry points | Evidence |
|:--|:--|:--|
| WebAPI | `GET /state/sprinter`, `/emulator/state/sprinter`, `/state/sprinter/ports`, `/ports/lookup`, `/state/sprinter/text` | `core/automation/webapi/src/emulator_api.h:287-291`, `api/state_device_api.cpp:300-375` |
| OpenAPI | the same five paths | `webapi/src/openapi/openapi_state.inc:202-305` |
| MCP | `inspect_state` aspects `sprinter`, `sprinter_ports`, `sprinter_text`; resource `unreal://machine/sprinter` | `mcp/src/mcp-tools.cpp:1063, 1097-1102, 1462-1467, 1819-1853`; `mcp/src/mcp-resources.cpp:311-345, 363` |
| CLI | `state sprinter [ports\|port\|text]` | `cli/src/commands/cli-processor-state.cpp:64-67, 221-223`; `cli-sprinter-format.h` |
| Lua | `sprinter_state()`, `sprinter_text()`, `sprinter_ports{}`, `sprinter_port()` | `lua/src/emulator/lua_emulator.h:2450-2508` |
| Python | the same four | `python/src/emulator/python_emulator.h:1953-2000` |
| Paging / ROM / ports overview | `sprinter` block in `/state/paging`, `/state/memory[/ram]`, `/state/memory/rom` (16 pages + roles), `/ports` `live.sprinter_port_table` | `webapi/src/api/state_memory_api.cpp:146-149, 301-304, 386, 1498-1512`; `api/ports_api.cpp:114-119`; Lua `lua_emulator.h:4224-4368`; Py `python_emulator.h:4093-4232`; CLI `cli-processor-state.cpp:425-447, 523, 582, 627` |
| command-interface.md | four rows in §3.3 | `command-interface.md:738-741` |
| Recipes | `machines/sprinter.md`, `machines/sprinter-accelerator.md`, `media/sprinter-hdd.md`, `analysis/sprinter-ttd.md`, `analysis/sprinter-mame-compare.md`, `analysis/port-trace.md` (codes) | `.recipe/README.md:96, 122-123, 135, 138` |

What `DeviceState::Sprinter` holds (`core/src/emulator/ports/models/sprinter/sprinterdevicestate.cpp:575-733`):
`pld` (state, module, bitstream hashes, fast_start, DCP open frame/PC), `decoder` (map, CNF, DOS,
PN5, `port_7ffd`/`port_1ffd`), `windows[4]`, `registers` (ROM_RG, SYS_PG, ALL_MODE decoded, PORT_Y,
RGMOD + mode page, HOLD, SCALE, CBL control), `cells` (#C0-#FF), `clock` (turbo, ratio, MHz), `frame`
(lines requested / in force, T-states), `video` (summary, below), `z84c15`, `fdc`, `cmos`, `ide`, `bios`.
**Not in it on master:** the accelerator, the wait-state rule, palettes, the per-square mode map, the
sound devices (the S6 branch adds `sound`).

## 2. Feature matrix

| # | Feature (core status) | WA | OA | MCP | CLI | Lua | Py | CI | Rc | Notes / evidence |
|:--|:--|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:-:|:--|
| 1 | Model create / list (`SPRINTER` creatable) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | generic `/models`; `mcp-tools.cpp:139`; `sprinter.md:39-44` |
| 2 | BIOS selection (3 images) | partial | partial | partial | partial | partial | partial | ✘ | ✔ | read-only list in `bios` (`sprinterdevicestate.cpp:413-463`); selection only by `[ROM] SPRINTER=` in the INI; no create parameter or variant (`core/src/emulator/machinevariants.cpp:39-43` has only TSL-VDAC2) |
| 3 | Full / fast start (`FastStart`), `AccelIntSuspend` | partial | partial | partial | partial | partial | partial | ✘ | ✔ | `pld.bitstream.fast_start` shown; neither is a setting (`/settings` names: `settings_api.cpp`, no Sprinter keys) |
| 4 | PLD configuration, modules, bitstream loader | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | `sprinterdevicestate.cpp:586-611` |
| 5 | Port table: state, ports, lookup, codes in the port trace | ✔ | ✔ | ✔ (lookup via `invoke_api`) | ✔ | ✔ | ✔ | ✔ | ✔ | `port-trace.md:77-119` |
| 6 | Paging / windows / fast RAM / ROM roles | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | ✔ | CI's `paging` row (`command-interface.md:757`) does not mention the Sprinter block; fast RAM bytes readable as page type `cache` (`SprinterMemory::FastRam() = CacheBase()`, `sprintermemory.h:86`; `debug_api.cpp:1648-1651`) but nowhere documented as such |
| 7 | Z84C15: WCR / MWBR / CSBR / MCR, WDT, CTC, SIO, PIO, IRQ priority | ✔ | ✔ | partial | ✔ | ✔ | ✔ | partial | partial | `sprinterdevicestate.cpp:319-410`; MCP text summary is one line; not in the report: daisy-chain order / IEI-IEO per device, WDT count, the wait generator (power-on M1 counter, after-ED flag - all in the TTD blob 29) |
| 8 | INT source (mode-table frame INT, keyboard INT) | partial | partial | partial | partial | partial | partial | ✘ | ✘ | first 8 positions + count + `keyboard_int_latched` (`sprinterdevicestate.cpp:308-315`); no "last INT at frame/T", no INT length |
| 9 | Wait states (21 MHz rule, `SprinterWaits`) | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | only `clock.ratio` (`sprinterdevicestate.cpp:671-679`) |
| 10 | Video: mode summary (dominant kind, counts, low-res, INT-armed squares) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | partial | `VideoSummary` `sprinterdevicestate.cpp:256-317`; a second, different classifier in `ScreenSprinter::DescribeScreenState` (`screensprinter.cpp:263-297`: blank counted as border) feeds `/state/screen` |
| 11 | Video: per-square mode (kind, palette 0-3/text palette, source column/row, low-res quarter, Line2) | partial | partial | partial | partial | partial | partial | ✘ | ✘ | only per pixel through the video map: `/video/pixel` (sources = mode bytes, pixel byte, pen; `sprintervideomapper.h:9-29`); no 40 x 32 grid anywhere |
| 12 | Video: palettes (8 x 256 pens, RGB) | partial | partial | partial | partial | partial | partial | ✘ | ✘ | one pen per `/video/pixel` call; no palette dump (TS-Conf has one: `state tsconf tsu`, `command-interface.md:737`) |
| 13 | Video: HOLD, border, PORT_Y, RGMOD, frame length 320/312 | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | partial | `registers`, `video.hold`, `frame` |
| 14 | Video: Spectrum-compatible screen (ALL_MODE shadow) | partial | partial | partial | partial | partial | partial | ✘ | partial | only `registers.all_mode.zx_screen_shadow` (`sprinterdevicestate.cpp:648`); no "the picture is a Spectrum screen" flag |
| 15 | Video: mode / palette / frame-length switch TRACE | partial | partial | partial | partial | partial | partial | ✘ | ✘ | see §3 Q1 |
| 16 | Video RAM 256 KB: read / write / dump | partial | ✘ | partial | partial | partial | partial | ✘ | partial | no `vram` space; CPU copy in RAM pages #50-#5F only (§3 Q2) |
| 17 | Video map: layout / pixel / address / beam | ✔ | ✔ | partial | ✔ | ✔ | ✔ | ✘ | ✘ | `video_map_api.cpp`; MCP has `video_layout` aspect, pixel/address only via `invoke_api`; `video_address_in` refuses `space=vram` (`devicestatevideo.cpp:347-362`); CI has no `video` rows (generic) |
| 18 | Screen text (`sprinter_text`) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | generic `/video/text` answers "no text layer" on the Sprinter (`devicestatevideo.cpp:388-396`) |
| 19 | Screenshot (736 x 288 / screen area) | ✔ | ✔ | ✔ | ✔ | ✘ | ✔ | ✔ | ✔ | Lua has no `capture_screen` (generic) |
| 20 | Screen digest | partial | partial | partial | partial | partial | partial | partial | ✘ | hashes RAM pages 5/7 (`screendigest.h:10-20`); `mode=active` uses `GetDisplayedRAMPages`, which knows nothing of the Sprinter (`ScreenSprinter` clears `activeRamPages`, `screensprinter.cpp:270`) - a native screen change is invisible unless `banks=80..95` is passed |
| 21 | OCR | partial | partial | partial | partial | partial | partial | partial | ✔ | `screen_ocr` is ZX-only; the recipe says use `sprinter_text` (`sprinter.md` Pitfalls) |
| 22 | `/state/screen/mode` geometry | partial | ✔ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | `framebuffer` / `raster` / `sprinter_modes` are added by the WebAPI handler only (`state_screen_api.cpp:124-141`), not by `DeviceState::ScreenMode` - a parity breach |
| 23 | Accelerator (S5): mode, length, function, blocked, ops counter | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | partial | the S5 outcome specified the JSON (`s5-accelerator-outcome.md:85-96`), never added (no `accel` in `sprinterdevicestate.cpp`, also not on `sprinter-s6`); the recipe still says "comes with the sprinter-automation work" (`sprinter-accelerator.md:76-83`) |
| 24 | Floppy (WD1793 latched, #BD density, drive, rate) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | `fdc` block + `/state/fdc`; the rate-retry search is not shown |
| 25 | IDE (2 channels, empty-channel pull-down) | ✔ | partial | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | `/state/ide` (`devicestate.cpp:1763-1799`), `sprinter-hdd.md`; OpenAPI text still says "ide (a placeholder until ... S3b)" (`openapi_state.inc:220-221`) |
| 26 | CHD media | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | ✔ | generic media slots (`use-media-slots.md:25-28`); CI's `media export` row (`command-interface.md:3326`) lacks `--compression` / `--parent` and never says CHD |
| 27 | Keyboard (PS/2 stream, typematic, overruns) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | ✔ | PC keys by name through `DebugKeyboardManager`; state: `z84c15.keyboard` (int, bytes on the wire, overruns, SIO A FIFO). Missing: raw scan-code injection, typematic state |
| 28 | Mouse (Kempston view #58 + MS serial on SIO B) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | ✔ | generic mouse API drives both (`sprinter.md:163-171`); the serial packet in flight is not reported |
| 29 | CMOS / RTC | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | `/state/rtc`, `/rtc/cells` |
| 30 | TTD (blobs 25, 28-31, 35) | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | ✔ | generic TTD; `sprinter-ttd.md`. Stale: MCP resource says "TTD refuses to record this machine" (`mcp-resources.cpp:343-344`), recipe `sprinter.md` row "refuses ... until phase S7" |
| 31 | Video recording with audio | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | ✔ | partial | generic `/video/record`; `video-recording.md:115` "Sprinter ... silent until that card exists" becomes stale with S6 |
| 32 | Sound S6 (branch): AY 1.75 MHz ABC, Covox, Covox-Blaster ring / rate / INT / DAC | ✔ (branch) | partial (branch) | partial | ✔ (branch) | ✔ (branch) | ✔ (branch) | ✘ | ✘ | branch adds `sound` to `DeviceState::Sprinter` and makes `DeviceState::Covox` report the machine DAC (`devicestate.cpp` diff +11), so all surfaces get it; OpenAPI text updated; MCP text summary does not print it; no CI row, no recipe |
| 33 | Audio mixer per device (levels, mute), capture per source | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | ✘ | generic: only a master `muted` flag (`state_audio_api.cpp:1221`); `/audio/capture` takes `seconds` only (`analyzers_api.cpp:1723-1741`); `/state/audio/channels` is built in the WebAPI, not in the core (beeper "unknown", no Covox row: `state_audio_api.cpp:1098-1180`) |
| 34 | MAME compare | ✔ | - | ✔ | - | - | - | - | ✔ | `sprinter-mame-compare.md` (inline Python), `tools/machines/sprinter/mame-capture/` |
| 35 | GUI indication (status bar / debugger) | - | - | - | - | - | - | - | - | not automation; no Sprinter video mode in the status bar (`statusbarmanager.cpp` only labels the IDE channels, line 639); no Sprinter debugger panel |

Counts over the 34 automation rows (row 35 excluded): complete on every surface and in the docs: 6
(rows 1, 4, 5, 18, 24, 29); complete on the surfaces but not in the docs: 4 (rows 26, 30, 31, 34); partial: 21;
missing on every surface: 3 (row 9 wait states, row 23 accelerator, row 33 per-device mixer / capture).

## 3. The owner's four questions

### Q1. Video modes: exposure, indication, parameters, tracing

- **Current mode per square:** not exposed as a grid. `video` gives counts per kind and the dominant
  kind (`sprinterdevicestate.cpp:256-317`); one square at a time is reachable through `/video/pixel`
  (its sources are the mode bytes). **Gap G3.**
- **Mode-table summary:** ✔ (`video.squares`, `mode_page`, `int_positions`).
- **Parameters:** HOLD ✔ (value + x/y shift), frame length ✔ (`frame.lines_requested` / `lines`),
  RGMOD ✔ (`registers.rgmod.mode_page`), PORT_Y ✔, border ✔. Palette index per square ✘ (G3),
  palette contents ✘ (G4).
- **Indication:** the GUI status bar shows nothing Sprinter-specific; `ScreenSprinter::DescribeScreenState`
  already builds a one-line description ("Sprinter 320 lines, mode page 1: text ..."), which only
  `/state/screen` shows (G12). There is no debugger panel.
- **Tracing (any machine):** there is **no "video mode change log" endpoint for any machine**. What exists:
  1. **Port trace with internal codes** (`profiler/porttrace/*`, every surface): each event has
     `frame`, `timestamp`, `pc`, `code`, `code_name` (`porttrace_api.cpp:107-125`). On the Sprinter the
     mode-relevant codes have names: `RgMod` #C5, `Hold` #CB, `Frame320` #2C, `Frame312` #2D, `Border`
     #C2, `PortY` #C4, `AllMode` #C3, `Scale` #C7 (`portdecoder_sprinter.cpp:993-999`); a filter
     `{"include":[{"code":"RgMod"},{"code":"Frame312"},...]}` gives a switch log today. Limits: the
     position is frame + timestamp, not line / T in frame; and the value is the byte written, not the
     state in force.
  2. **`VideoWriteLog`** (`core/src/emulator/video/map/videowritelog.h`): per-frame list of
     (frame T, latches) - exactly a "video mode change log", but internal (it serves `/video/pixel`'s
     `state_at`), fed only from the ZX screen (`screenzx.cpp:715`), and its `VideoLatches` have no
     Sprinter fields.
  3. **Mode-table and palette changes are video RAM writes**, not port writes: nothing traces them.
     Memory watchpoints on RAM pages #50-#5F catch the graphics-page writes, but not the Spectrum-shadow
     writes (they go to video RAM only, `sprintermemory.h:30, 96-100`).
  4. Beam tools (`/video/beam`, `run_to_scanline`, `/video/pixel?t=`): position only, no history.
  **Gap G6** (generic) proposes the log.

### Q2. Framebuffer access

- **Raw video RAM (256 KB, separate from CPU memory):** no automation space. `SprinterVideoRam` is a
  separate array (`sprintervideoram.h:36-60`); the memory API knows `ram` / `rom` / `cache` / `misc`
  only (`debug_api.cpp:1648-1651`). Workaround: RAM pages #50-#5F hold the CPU copy (graphics writes go to
  both, `sprintervideomapper.h:24-25`), readable with `/memory/page/ram/80..95` - but Spectrum-shadow
  writes are missing there, and a write there does not reach video RAM (no palette refresh, no INT
  update). TTD carries it (blob 28) but there is no extract. **Gap G5.**
- **Rendered framebuffer raw:** still PLAN #26 (`docs/inprogress/PLAN.md:124`, deferred: "Digest/OCR/
  screenshot cover demand"). For the Sprinter the deferral reason is weaker: digest and OCR do not see
  native screens. `capture/planeb` is ZX DLSS only. **Gap G14.**
- **Screenshot:** ✔ PNG/GIF 736 x 288 (`sprinter.md:318`). **Digest:** blind to native screens (G7).
  **OCR:** `sprinter_text` for text squares ✔; glyphs drawn as graphics (Flex Navigator panels) have no
  text path; generic `video_text` / `screen_ocr` do not dispatch to `SprinterText` (G8).

### Q3. Sound (branch `sprinter-s6`)

On the branch: `state sprinter` → `sound.ay` (chip, `clock_hz` 1 750 000, stereo ABC, chip count,
ports) and `sound.covox_blaster` (control, mode, stereo, bits, `int_enabled`, rate / divider /
`rate_hz` / `tick_tstates`, `play_index`, `write_index`, `int_pending`, `half_needs_data`,
`next_tick_tstate`, `dac_left/right`, counters `ticks`, `ring_writes`, `covox_writes`, `int_requests`);
`/state/audio/covox` reports the machine DAC (`fitment: machine`) on all surfaces; AY registers through
the generic `/state/audio/ay`. Still to add (G9, G10): the ring bytes themselves, the MCP one-line
summary, CI rows, a recipe (WAVPLAY / Covox-Blaster check with audio capture), the stale
`video-recording.md:115` line, per-device mixer levels / mute and per-source capture (generic G13).
Note: `sound.ay.stereo` is a constant string; it should come from the config the AY uses.

### Q4. Other gaps

Accelerator on no surface (G1); stale MCP resource / recipe / OpenAPI text (G2); BIOS and start options
only in the INI (G11); wait states (G16); Z84C15 detail (G17); raw PS/2 injection and typematic state
(G18); serial mouse packet (G19); accelerator operation trace (G20); MAME compare as a script (G21).
TTD and CHD need no Sprinter-specific API: the generic ones work (docs only).

## 4. Prioritized gaps and proposals

Effort: S = under a day, M = a few days, L = a week or more. Names follow the existing conventions
(`/state/<device>[/<part>]`, CLI `state <device> <part>`, Lua / Python `<device>_<part>()`, MCP
`inspect_state` aspect `<device>_<part>`); every proposal is one `DeviceState::*` function the five
surfaces only convert.

### P1 - do next

| Gap | Scope | Proposal | Effort | Status |
|:--|:--|:--|:-:|:--|
| G1 Accelerator state | SP | Add `accelerator` to `DeviceState::Sprinter` exactly as `s5-accelerator-outcome.md:85-96` specifies (`enabled`, `mode`, `mode_name`, `length`, `function`, `blocked`, `int_suspend`, `alt`, `xcnt`, `aagr`, `operations`, `last_extra_clocks`, `buffer_crc32`, `buffer_head`; `available:false` while the PLD loads). Update OpenAPI text, CI row, MCP summary line, `sprinter-accelerator.md:76-83` | S | **done**: `accelerator` block as specified, plus `armed`, `dir`, `length_register`; OpenAPI, CI row, MCP summary line, recipe |
| G2 Stale texts | SP | MCP resource "Known limitations" (`mcp-resources.cpp:342-344`: IDE, accelerator, TTD all done; mention CHD, `sprinter-hdd.md`, `sprinter-ttd.md`); `sprinter.md` "What works" rows for accelerator and TTD; OpenAPI `ide (a placeholder ...)` (`openapi_state.inc:220-221`); CI row 738 field list (add ide, sound after S6) | S | **done**: MCP resource rewritten (BIOS API, IDE, accelerator, sound, TTD, video aspects), recipe "What works" (TTD, DSS 1.71 from HDD), OpenAPI `ide` text, CI rows, AGENTS.md note |
| G3 Per-square mode map | SP | `GET /state/sprinter/video` (`?page=0\|1`, default RGMOD's; `?all=1` = 56 x 40 instead of the 40 x 32 picture): `mode_page`, `squares[b][a]` = `{kind, m0, m1, m2, palette, source_column, source_row, low_res, quarter, int}` plus a compact `map` of one letter per square (`G` 320, `g` 640, `T` text 40, `t` text 80, `B` border, `.` blank, `*` INT) for agents; CLI `state sprinter video [page=]`, Lua / Python `sprinter_video{page=}`, MCP aspect `sprinter_video`. Move `ClassifySquare` into one shared function and make `DescribeScreenState` use it (fixes the two classifiers) | M | **done** as proposed (`/state/sprinter/video?page=&all=&squares=`; `squares=0` = map only, used by the MCP aspect); one classifier `SprinterSquare` (renderer header) for the summary, the map, the text, `DescribeScreenState` and the OCR / `video_text` fallback. The right half of an 80-column square now follows Line2's own Mode0 (the renderer's rule) |
| G4 Palettes | SP | `GET /state/sprinter/palette?k=0-7` (pens `n`, `rgb` "#RRGGBB", the VRAM address of the red byte); default = the palettes the picture uses. Same names on the other surfaces (`sprinter_palette`) | S | **done** (`k=0-7|all|used`; per pen `n`, `rgb`, `vram`, plus `rgb_row` and `role`) |
| G5 Video RAM access | SP (generic shape) | A named memory space in the existing page API: `GET/POST /memory/page/vram/{0-15}` (16 x 16 KB) and `GET /memory/vram/{offset}?length=` for the Sprinter (later: FT812 `RAM_G` on TSL-VDAC2, the same idea); writes go through `SprinterVideoRam::Write` (palette refresh, INT positions). `DeviceState::VideoAddressIn` accepts `space=vram` (`devicestatevideo.cpp:347-362`) so `video_address_in("vram", off)` maps a byte to its pixels. Save: `memory save vram <file>` on CLI / `memory_save` Lua / Python. Document `cache` = fast RAM | M | **done**, generic: `IDeviceMemoryRegion` + `DeviceMemory` (`emulator/memory/devicememory.h`), `PortDecoder::CollectMemoryRegions`; `GET /memory/regions`, `GET/POST /memory/region/{name}` (hex / data / sparse / binary, save / load) and `/memory/page/vram/{0-15}`; CLI `memory region`, Lua / Python `region_*`, MCP `memory_region`; `video_address_in("vram", ..)`. Deviation: `/memory/region/vram?offset=` instead of `/memory/vram/{offset}` (one route for every future region); `cache` = fast RAM documented |
| G6 Video change log | gen | Expose `VideoWriteLog` as `GET /video/changes?frames=1-2` (current and previous frame: `frame`, `t`, `line`, `t_in_line`, the latch diff by name) on every surface (CLI `video changes`, `video_changes()`, MCP aspect `video_changes`). Extend `VideoLatches` with a small per-family block (Sprinter: `rgMod`, `hold`, `frameLines`, `portY`, `allMode`, `border`; ATM / TS-Conf / Profi as they need) and call `NoteVideoWrite()` from the Sprinter cells and from `SprinterVideoRam::Write` for the mode-table and palette columns (as counters + first / last T per frame, not one entry per byte). Until then: document the port-trace filter on `RgMod` / `Hold` / `Frame320` / `Frame312` / `Border` / `PortY` / `AllMode` in `sprinter.md` | M-L | **done**: `VideoLatches` + `rgMod`, `hold`, `portY`, `allMode`, `frameLines`; entries carry the PC (`PortDecoder::IoPc`); table writes counted per frame (`VideoTable::ModeTable` / `Palette`: count, first / last T, address, PC) from the Sprinter VRAM, ATM710 / ATM3 / ATM450 / Profi palettes and TS-Conf CRAM (FM window; DMA writes not, see TODO); `DeviceState::VideoChanges`, `/video/changes`, CLI `video changes`, `video_changes()`, MCP `video_changes`. A/B in the commit (`BM_SprinterVideoRamWrite_*`) |
| G7 Digest blind to native screens | SP in gen | `mode=active` on the Sprinter: hash the video RAM (or only the picture squares' sources + mode table + used palettes) and report `active_surface: {video_mode:"Sprinter", vram:true}`; default mode too, since pages 5/7 say nothing about a DSS screen. Move the digest logic from `state_screen_api.cpp` into a `DeviceState` function on the way (parity) | S-M | **done**: digest logic moved to the core (`ScreenDigestCompute`, `DeviceState::ScreenDigestReport`; WebAPI / CLI / Lua / Python use it, their output shapes kept); `Screen::DigestSurface`: the Sprinter hashes the whole video RAM + RGMOD page, HOLD, frame height in the default and active modes (`active_surface.memory = "vram"`); the CLI / Lua / Python 128K test unified to `Screen::HasShadowScreen` |

### P2

| Gap | Scope | Proposal | Effort | Status |
|:--|:--|:--|:-:|:--|
| G8 Generic text / OCR dispatch | SP in gen | `DeviceState::VideoText` and `screen_ocr` fall back to `SprinterText` on the Sprinter, so an agent that does not know the machine still reads BIOS / DSS screens | S | **done** without a mapper text layer (it would have marked the graphics layer as text): `DeviceState::VideoText` and `ScreenOCR` fall back to `SprinterText`. `video_text` while any text square shows, the OCR while most of the picture is text (`picture_is_text`); Spectrum mode (ALL_MODE bit 0 = 0, ZX screen drawn by text squares) stays a ZX screen |
| G9 Sound docs and summaries (after S6 merges) | SP | CI rows for `state sprinter` sound fields and the `audio covox` machine DAC; MCP summary line `[sprinter] sound: CBL 16-bit stereo 31 250 Hz, ring play #40 / write #C0, INT pending`; a recipe `machines/sprinter-sound.md` (WAVPLAY from a DSS floppy, audio capture RMS > threshold, the ring counters); fix `video-recording.md:115`; `sound.ay.stereo` from the AY's config | S | **done**: CI rows, MCP sound summary line, `sound.ay.stereo` from the AY's config, `video-recording.md` line; recipe `sprinter-sound.md` existed (S6) and gained the per-source capture, ring and mixer (verified) |
| G10 Covox-Blaster ring bytes | SP | `GET /state/sprinter/sound/ring` (256 words, play / write index marked) | S | **done** (`/state/sprinter/sound/ring`: `rows` with `[ ]` / `< >` marks and `words[256]`) |
| G11 BIOS / start options at runtime | SP | Create body `{"model":"SPRINTER","rom":"sp2k-3.06-hf2.rom","fast_start":true}` (or variants `SPRINTER-306`, `SPRINTER-307B1` in `machinevariants.cpp`, the TSL-VDAC2 pattern); settings `sprinter_fast_start`, `sprinter_accel_int_suspend` (restart-required flag as other settings) | M | **done**: create option `"sprinter": {"bios", "fast_start", "accel_int_suspend"}` (WebAPI create / start, CLI `--sprinter-bios`, MCP `sprinter_bios` / `sprinter_fast_start`); runtime `POST /sprinter/bios` (CLI `state sprinter bios <name>`, Lua / Python `sprinter_bios_select`), loaded at the next reset (`Emulator::RequestRomReload`, `reset: true` now); report `/state/sprinter/bios` (loaded image by CRC-32). Deviations: no variants (firmware is not a board) and no `/settings` keys (the options live with the BIOS selection, one place, Sprinter-only) |
| G12 Screen-mode parity + GUI indication | gen / SP | Move `framebuffer` / `raster` / `sprinter_modes` from the WebAPI handler into `DeviceState::ScreenMode`; show `DescribeScreenState().videoMode` in the GUI status bar (all machines that have one: Sprinter, TS-Conf) | S | **done**: `framebuffer` / `raster` / `sprinter_modes` / `video_mode_brief` built by `DeviceState::ScreenMode`; GUI status bar label from `ScreenState::videoModeBrief` (Sprinter; tooltip = the full description) |
| G13 Mixer per device | gen | `GET/PUT /audio/mixer[/{source}]` `{muted, gain_db}` for beeper, AY, Covox (= the Sprinter DAC), GS, MoonSound; `/audio/capture` `{"source":"covox"}` for one source; rebuild `/state/audio/channels` from a `DeviceState` report | M | **done**: `AudioMixer` keys over the SoundManager registry; `GET /audio/mixer`, `PUT/POST /audio/mixer/{source}` (`muted`, `solo`, `volume`, `gain_db`), `/audio/capture {source}` (the device's own buffer; the analyzer tap follows the armed capture), `/state/audio/channels` = `DeviceState::AudioChannels` (beeper peak / activity real now, mixer devices); CLI `mixer`, `audiocapture start <s> [source]`, Lua / Python `audio_mixer[_set]`, `audio_capture_start(s, source)`, MCP `audio_mixer`, `capture_media audio_capture source` |
| G14 Raw framebuffer (PLAN #26) | gen | `GET /capture/framebuffer?format=rgba\|index` with `X-Width/X-Height` headers, Python `framebuffer()` -> numpy; lets tests compare Sprinter pictures without PNG decode | M | **done**: `FramebufferExport` - `rgba` (the presented frame) on every machine, `index` (u16 pens) where `Screen::IndexedFrame` exists (the Sprinter); `GET /capture/framebuffer?format=&encoding=binary|base64`, CLI `capture framebuffer`, Lua / Python `framebuffer()` (numpy `array` when installed), MCP `capture_media framebuffer` |
| G15 command-interface.md generic gaps | gen | rows for `video layout / pixel / address / text / temporal` (they exist on all surfaces but are only in webapi/lua/python docs); `media export --compression --parent` and CHD | S | **done**: CI rows for `video layout / pixel / address / text / changes`, `media save / export --compression --parent` and CHD |

### P3

| Gap | Scope | Proposal | Effort | Status |
|:--|:--|:--|:-:|:--|
| G16 Wait states | SP | `clock.waits`: rule in force, extra T of the last instruction, per-frame totals by kind (fetch / memory / I/O / Z84C15 port) | S | **partial** (owner priority 3): `clock.waits` = the rule, `active`, `windows_waiting`, taken clocks; per-frame totals and the last instruction's extra T need counters on the wait path (hot) - left for P3 with an A/B |
| G17 Z84C15 detail | SP | `z84c15.daisy_chain[]` (order, IEI / IEO, under service), `watchdog.count`, `wait_generator` (M1 counter, after-ED) | S | **partial** (owner priority 3): `z84c15.wait_generator` (WCR / MWBR decoded), `daisy_chain` (priority order, IP / IUS per source), `watchdog.deadline_clock`; the power-on M1 counter and the after-ED flag live in the library's internal header (`z84cpu-internal.h`) - not exposed without a library API change |
| G18 Keyboard raw | SP + ZX-Evo | `POST /keyboard/scancode {"bytes":["E0","75"]}` into the PS/2 stream (journaled as `PcKey`); `z84c15.keyboard.typematic` (held key, next repeat T) | S-M | not done (P3) |
| G19 Serial mouse | SP | `z84c15.mouse`: packet in flight, byte index, last sample | S | not done (P3) |
| G20 Accelerator trace | SP | analyzer `sprinter_accel` (`/analyzer/sprinter_accel/events`): mode select, operation (kind, length, address, page, PORT_Y), frame / T, PC | M | not done (P3) |
| G21 MAME compare | SP | turn the inline Python in `sprinter-mame-compare.md:59-80` into `tools/machines/sprinter/mame-capture/compare.py` | S | not done (P3) |

Totals: 21 gaps - 7 P1, 8 P2, 6 P3; 6 generic (G6, G12 part, G13, G14, G15, and the digest / text
dispatch shape of G7 / G8), 15 Sprinter-specific.
