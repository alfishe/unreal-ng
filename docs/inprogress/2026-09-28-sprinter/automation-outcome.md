# Automation outcome: the Sprinter on every automation surface (2026-10-02)

Branch `sprinter-automation`. The automation of [tdd-integration.md](tdd-integration.md) §3: the
Sprinter's own state, its port table, its paging and its screen text on the WebAPI, MCP, CLI, Lua and
Python, all from one core source, plus the recipes.

## What an agent can do now

| Question | WebAPI | CLI | Lua / Python | MCP |
|:--|:--|:--|:--|:--|
| The machine: PLD configuration and module, CNF map / DOS / PN5, the 4 windows (physical page + kind), registers and cells `#C0-#FF`, turbo, frame length, mode-table video summary, Z84C15 (WCR / MWBR / CSBR / MCR, watchdog, CTC, SIO with the keyboard FIFO, PIO), the floppy density latch, CMOS / IDE links, the BIOS images | `GET /state/sprinter` (also `/emulator/state/sprinter`) | `state sprinter` | `sprinter_state()` | aspect `sprinter` |
| The decoded port table for a map / DOS / PN5 / direction | `GET /state/sprinter/ports?map=&dos=&pn5=&rw=` | `state sprinter ports map=0 dos=1 rw=w` | `sprinter_ports{...}` / `sprinter_ports(map=, dos=, pn5=, rw=)` | aspect `sprinter_ports` (current state) |
| One port: index into page `#40`, code, name (or the Z84C15) | `GET /state/sprinter/ports/lookup?port=21BC&rw=w` | `state sprinter port 21BC rw=w` | `sprinter_port(0x21BC, ...)` | `invoke_api` |
| The screen text (80 x 32 text squares: BIOS, SETUP, DSS) | `GET /state/sprinter/text` | `state sprinter text` | `sprinter_text()` | aspect `sprinter_text` |
| Paging | `/state/paging`, `/state/memory`, `/state/memory/ram`: the windows with their kind | `state memory`, `state ram` | `paging_state()` (`sprinter` block, banks by kind) | aspect `paging` |
| ROM pages | `/state/memory/rom`: 16 pages with their roles | `state rom` | - | aspect `rom` |
| Ports overview | `/ports`: BIOS 3.04 map-0 rows, fixed decodes, Z84C15 ports, `live.sprinter_port_table` | - | `ports_map()` | aspect `ports` |
| CMOS | `/state/rtc` (already there; the state block links it) | `state rtc` | `rtc_state()` | aspect `rtc` |
| Keys | `/keyboard/tap`, `/type`, `/combo` (already there: PC keys reach the SIO, S4) | `key ...` | `key_*` | `type_input` |

One source: `DeviceState::Sprinter`, `SprinterPaging`, `SprinterPortTable`, `SprinterPortLookup`,
`SprinterText` and the parameter parsers `SprinterPortQueryFromStrings` / `SprinterPortFromString`
([sprinterdevicestate.cpp](../../../core/src/emulator/ports/models/sprinter/sprinterdevicestate.cpp)),
declared in `devicestate.h`. Every interface only converts the tree. The port-table index formula is
one header ([sprinterporttable.h](../../../core/src/emulator/ports/models/sprinter/sprinterporttable.h)),
used by the decoder's `LookupIndex` and the views; the code names are the decoder's
`GetPortTraceCodeTable()`, the same names the port trace prints. The table rows are the greedy
cube cover of `tools/machines/sprinter/dcp-table/dcp-table.py`, restricted to the 9 address bits.

Other changes:

- `GET /state/memory/rom` and CLI `state rom`: 16 ROM pages; `ROM::GetROMPageRole` names the
  Sprinter's pages (disk drivers + SETUP, BIOS EXP, PLD loader + bitstream).
- `/state/screen/mode`: `M_SPRINTER` gets the framebuffer / raster fields other modes have.
- `PortDecoder::getPortMapEntries`: a `MM_SPRINTER` case (no Beta-128 rows: the Sprinter reaches its
  WD1793 through the table).
- `UNREAL_MCP_PORT` moves the MCP listener (as `UNREAL_WEBAPI_PORT` / `UNREAL_CLI_PORT` move theirs):
  a second instance serves MCP beside one that owns 8092.
- Disk uploads (`load_software` from the agent host) take images up to 4 MB: 1.44 MB PC floppies
  (the DSS images) were refused at 1 MB.
- MCP resource `unreal://machine/sprinter`.
- `PortDecoder_Sprinter`: one read-only accessor (`CblControl()`) and `LookupIndex` through the shared
  formula; nothing else in the decoder changed.

## Placeholders for parallel branches

- **IDE (branch `sprinter-s3b`)**: the state block's `ide` object (`emulated: false`, channel, data
  latch, "S3b-IDE" comment in `sprinterdevicestate.cpp`) is where the adapter's state goes; the
  general `/state/ide` report (`DeviceState::Ide`) will show the drives once `IdeAdapterSprinter` is
  registered. The recipe's IDE rows say "not yet (phase S3b)".
- **Accelerator (S5), Covox-Blaster (S6)**: `registers.scale` and `registers.cbl_control` show the
  stored values only.

## Verified against a running build

The worktree's `unreal-qt` ran offscreen on ports 8190 / 8191 / 8192 (`UNREAL_*_PORT`); every
command in the recipes was run there and the outputs pasted (trimmed):

- [.recipe/machines/sprinter.md](../../../.recipe/machines/sprinter.md): create, the BIOS images
  and the switch to 3.07 BETA 1 by `[ROM] SPRINTER=` (with `FastStart=1`), the full start, DSS 1.62
  from floppy B with F4 at both IDE waits, `type_input` (`dir`, Ctrl+Alt+Del), Spectrum mode through
  `SPECTRUM.EXE PENT128.ZX` (both floppies in A and B before the boot), TR-DOS `LIST` of a TRD in A,
  the state block, paging, the port table and lookups, the port trace with codes, screenshots - on
  MCP, WebAPI (the WebAPI block run verbatim as a script), CLI, Lua and Python (a second build with
  `ENABLE_PYTHON_AUTOMATION=ON`: Python is off in the default build).
- [.recipe/analysis/sprinter-mame-compare.md](../../../.recipe/analysis/sprinter-mame-compare.md):
  the boot trace from power-on against MAME's `ports.csv`: **all 9 989 post-loader accesses match
  in port, value, PC and code.**
- [.recipe/analysis/port-trace.md](../../../.recipe/analysis/port-trace.md) (Sprinter codes, the
  TR-DOS FDC accesses filtered by code), [.recipe/media/insert-disk.md](../../../.recipe/media/insert-disk.md)
  (the density latch after the DSS boot and in TR-DOS), [.recipe/_common/setup.md](../../../.recipe/_common/setup.md)
  (the second-instance ports).

## Tests

| What | Test |
|:--|:--|
| The index formula = the decoder's lookup; the state block (PLD, decoder, registers, frame, cells, Z84C15, CMOS / IDE / BIOS); the clock; the windows (port table, ROM, graphics, RAM, fast RAM, vROM, system RAM) and the paging view; the BIOS 3.04 table decoded (`#2B` at `001x xxxx 101x x100`, the border, the WD1793 only with DOS on); lookups (`#21BC` index `#043C`, `#7FFD` both ways, a Z84C15 port, the `#FB` fixed decode); the parameter parsers; the screen text; "Not a Sprinter machine" elsewhere | `SprinterDeviceState_Test` (9), `SprinterDeviceStateOther_Test` (1) |
| CLI arguments, the subcommands' text, errors | `CliSprinterFormat_Test`, `CliSprinterMachine_Test`, `CliSprinterOther_Test` |
| MCP aspects `sprinter` / `sprinter_ports` / `sprinter_text` (endpoints, summaries, 404 = unavailable), the machine resource | `McpTools_Test.InspectState_Sprinter*` (2), `McpDispatcher_Test.ResourcesRead_MachineSprinter_*`, `ResourcesList_ContainsNineResources` |
| `type_input` on DSS: BIOS 3.04, F4 at both IDE waits, DSS 1.62 from floppy B, `dir` typed through `DebugKeyboardManager::TypeText` lists the floppy, read back with `SprinterText` | `SprinterInputBoot_Test.Dss162_TypeInputReachesTheShell` (boot-bound, ~3 s) |

The WebAPI handlers, the Lua and the Python bindings have no unit tests in the repo (no harness for
them); they convert the tested trees and were run against the build as above.

## Findings and gaps

- **DSS 1.71** (`dss-1.71u.img` from the owner's MAME pack): the BIOS loads it from floppy B, then
  "Fatal error! Press RESET to restart." Not investigated here (core, not automation).
- **Flex Navigator** (DSS 1.62's `fn` at the end of `SYSTEM.BAT`) draws its logo and hangs; the
  recipe boots a copy of the floppy without that line.
- **A floppy inserted into A after DSS started** was not seen by `SPECTRUM.EXE` (it waited); both
  floppies inserted before the boot work. Not investigated (DSS disk-change handling or the drive's
  ready / change signals).
- **One unexplained exit** of the offscreen `unreal-qt` during the first Spectrum-mode session
  (inserting a TRD into A, ENTER into TR-DOS, enabling the port trace); the same steps later ran
  without a problem. No crash report was written.
- **OpenAPI coverage** (`tools/verification/webapi/verify_openapi_coverage.py`): the new paths are
  documented; four older "active emulator" paths are not (`/emulator/state/audio/moonsound[/{part}]`,
  `/emulator/state/ide`, `/emulator/state/tsconf`).
- **MCP `emulator_manage create`** prints `(model )` with an empty model name (all models).
- **Build**: the post-build `data/rom` copies of `unreal-qt` and `unreal-videowall` race under
  parallel ninja ("Error copying directory"); a second `ninja` run passes.

## Audit round (2026-10-02, branch `sprinter-automation`)

The P1 and P2 gaps of [automation-audit-2026-10-02.md](automation-audit-2026-10-02.md) (status per gap in its
§4). Each feature is one core function; the five surfaces only convert it.

| Question | WebAPI | CLI | Lua / Python | MCP |
|:--|:--|:--|:--|:--|
| Which mode does each 8 x 8 square show? | `GET /state/sprinter/video` | `state sprinter video` | `sprinter_video{}` / `sprinter_video()` | aspect `sprinter_video` |
| What colors are the pens? | `GET /state/sprinter/palette?k=` | `state sprinter palette [k]` | `sprinter_palette(k)` | aspect `sprinter_palette` |
| Read / write / dump the video RAM | `GET/POST /memory/region/vram`, `/memory/page/vram/{n}` | `memory region ...` | `region_read` / `region_write` / `region_save` / `region_load` | aspect `memory_region`; `invoke_api` POST |
| What changed in the video this frame, when, from where? (every machine) | `GET /video/changes` | `video changes` | `video_changes()` | aspect `video_changes` |
| Did the picture change? | `GET /state/screen/digest` (video RAM surface) | `digest` | `screen_digest()` | aspect `screen_digest` |
| The picture as pixels / pens | `GET /capture/framebuffer?format=rgba\|index` | `capture framebuffer` | `framebuffer()` | `capture_media` framebuffer |
| Read the screen text without knowing the machine | `/video/text`, `/capture/ocr` | `video text`, `capture ocr` | `video_text()`, `capture_ocr()` (Python) | `video_text`, `screen_ocr` |
| Accelerator, waits, Z84C15 detail | `GET /state/sprinter` (`accelerator`, `clock.waits`, `z84c15.wait_generator` / `daisy_chain`) | `state sprinter` | `sprinter_state()` | aspect `sprinter` |
| Which BIOS runs; switch it | `GET /state/sprinter/bios`, `POST /sprinter/bios`, create `"sprinter": {...}` | `state sprinter bios [<name>]`, `create SPRINTER --sprinter-bios` | `sprinter_bios()`, `sprinter_bios_select{}` | aspect `sprinter_bios`, `emulator_manage create sprinter_bios` |
| The Covox-Blaster ring | `GET /state/sprinter/sound/ring` | `state sprinter ring` | `sprinter_sound_ring()` | aspect `sprinter_sound_ring` |
| Mute / solo / volume one sound device; record only it | `GET /audio/mixer`, `PUT /audio/mixer/{source}`, `/audio/capture {source}` | `mixer`, `audiocapture start <s> <source>` | `audio_mixer[_set]`, `audio_capture_start(s, source)` | aspect `audio_mixer`, `capture_media audio_capture source` |

**Verified against a running unreal-qt** (this branch, `UNREAL_WEBAPI_PORT` / `UNREAL_CLI_PORT` /
`UNREAL_MCP_PORT` moved; WebAPI, CLI over the socket, MCP over HTTP, Lua through `/lua/exec`):

- create with `"sprinter":{"bios":"3.07"}` (and MCP `sprinter_bios: "3.07"`) → `loaded: sp2k-3.07-beta1.rom`;
  DSS 1.71.57 boots from the MAME pack's `sp_hdd_sys.chd` to Flex Navigator 1.15; a bad name is a 400 listing
  the images;
- FN: map all `g` (1 280 graphics 640 squares), the background pixel = pen 9 = `#000080` from the palette, the
  video map and the region read alike; `POST /memory/region/vram {"offset":"0x27E0","hex":"800000"}` turned the
  panels red on the next screenshot, the change log counted 2 palette writes at `0x027E0`, the digest changed;
  `video_changes` showed FN's PORT_Y steps (`0xC0 -> 0x80` at line 289, PC `0x0B3D`); the accelerator block
  reported 40 499 operations; `framebuffer?format=index` 736 x 288, pixel (300, 150) = 9;
- `POST /sprinter/bios {"bios":"3.06","fast_start":false,"reset":true}` on the running machine → 3.06 loaded,
  full start, WAVPLAY of a 440 Hz WAV from a copy of the system disk: control `#9B`, `/audio/capture
  {"source":"covox"}` 436.5 Hz (440 x 21 875 / 22 050), `source: ay1` silent, `moonsound_fm` refused (not
  fitted), the ring with its marks, `PUT /audio/mixer/covox {"gain_db":-6}` → volume 0.501;
- BIOS 3.04 boot screen without disks: `sprinter_text`, `/video/text` (`layer: sprinter_text`) and
  `/capture/ocr` print the same lines ("Model name: Sprinter ... Sprinter BIOS: ver 3.04.253");
- CLI: `state sprinter video / palette 4 / ring / bios`, `memory region read vram`, `mixer`, `video changes`,
  `digest` ("Mode: Sprinter, surface: vram"); MCP `inspect_state` with every new aspect; Lua every new function.
  Python is compile-checked (`-DENABLE_PYTHON_AUTOMATION=ON`, `libautomation_python.a`): the default build has
  no Python interpreter.

**Tests** (core-tests, each well under 50 ms unless noted): `SprinterDeviceState_Test` (video map per kind,
shared classifier, palette RGB order, accelerator + waits + Z84C15 detail, ring, change log, `video_text` /
screen mode), `SprinterBios_Test` / `SprinterBiosReport_Test` / `SprinterBiosReload_Test` (a reset loads the
selection, 9 ms), `DeviceMemory_Test` (regions, write path, save / load, CLI, `vram` space),
`VideoWriteLog_Test` (family latches, table writes), `ScreenSprinter_Test.DigestHashesTheVideoRam`,
`ScreenDigestQuery_Test`, `ScreenOCRTextMode_Test.SprinterTextSquaresAreRead`, `FramebufferExport*_Test`,
`AudioMixer*_Test` (keys, apply, channels, the capture tap), `CliSprinterMachine_Test` (video / palette / ring /
bios), `McpTools_Test` (Sprinter aspects, region / changes / mixer / bios summaries), `McpDispatcher_Test`
(the resource text).

**Deviations from the audit's proposals** (why): video RAM by `/memory/region/{name}` rather than
`/memory/vram/{offset}` (one generic route for every future device memory - the FT812 `RAM_G` next); BIOS
start options with the BIOS selection, not in `/settings` (Sprinter-only, applied at the reset like the
image); no machine variants for BIOS versions (firmware, not a board); the text fallback in `VideoText` /
`ScreenOCR` instead of a mapper text layer (a text layer would mark the mixed graphics layer as text); the
OCR uses the Sprinter text only while most of the picture is text and never in Spectrum mode (its ZX screen
is drawn with text squares whose "font" is the bitmap).

**A/B of the video RAM hook** (performance-guidelines.md §4; A = `bba3d545c` with the new benchmark file minus the
listener, B = this branch; rounds A, B x 3 then B, A x 2; load 80-140 - the shared machine never got quiet, so
the per-round noise is several percent; `cpu_time` in µs, B vs A per round):

| Benchmark | A min | B min | Rounds | Mean |
|:--|--:|--:|:--|--:|
| `BM_SprinterVideoRamWrite_Screen` (64 K changing screen bytes, columns #000-#2FF) | 87.5 | 86.4 | -1.0 -7.5 -12.1 -0.3 -2.5 | -4.7 % |
| `BM_SprinterVideoRamWrite_Tables` (64 K changing mode-table / palette bytes) | 119.3 | 174.9 | +46.5 +46.6 +49.0 +58.5 +47.4 | +49.6 % |
| `BM_SprinterFrame_Logo` (BIOS logo frame, CPU + catch-up rendering) | 4054.0 | 4005.3 | -1.3 +3.7 +0.5 +0.9 -0.3 | +0.7 % |
| `BM_HostFrame_Sprinter_Fast` | 3146.4 | 3133.7 | -0.6 +1.0 -3.0 -2.4 +1.8 | -0.7 % |

The common path (picture bytes) costs nothing: one column compare as before, the table work moved out of line.
A changed mode-table or palette byte now also calls the change-log listener (a `std::function`, ~0.9 ns a
byte here): +50 % on a loop of nothing but table bytes, invisible in whole frames (a palette load is 768
bytes). Other machines: the palette hooks run in port handlers that already changed a palette (cold).
