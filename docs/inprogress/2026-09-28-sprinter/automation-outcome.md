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
