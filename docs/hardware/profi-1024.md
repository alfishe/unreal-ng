# ZX Profi 1024 (model `PROFI`)

The Profi 1024 is creatable on this build (`emulator_manage action=create model=PROFI`, CLI `create PROFI`,
`POST /api/v1/emulator/start {"model": "PROFI"}`). Configuration lives in `data/configs/profi/unreal.ini`; the
default ROM image is `data/rom/profi/romain_2.91_ramdisk_d.rom` (karabas-pro's ROMain firmware, TR-DOS RAM disk on
drive D). Alternative 64K ROM images (Micco Software menu ROM, other ROMain builds, DivMMC, Gluk+DFFD) live under
`data/rom/profi/`, see its `README.md`. Design notes and open work:
[technical-design.md](../inprogress/2026-09-21-profi/technical-design.md).

## ROM layout (4 pages of 16 KB)

| Page | Content |
|:--|:--|
| 0 | SYS / menu ROM (BIOS), selected after reset |
| 1 | TR-DOS |
| 2 | 128K editor + STS monitor |
| 3 | 48K BASIC |

Automation surfaces (`/state/paging`, `/state/memory/rom`, CLI `state rom`, Lua/Python `paging_state()`) report these
as "SYS/Menu ROM", "TR-DOS ROM", "128K Editor + STS Monitor ROM" and "48K BASIC ROM". TR-DOS is entered through the
DOS latch (the `CF_TRDOS` flag, set by the usual `$3Dxx` M1 trap).

## Paging latches

| Port | Address decode | Bits |
|:--|:--|:--|
| `#7FFD` | A15 = 0 and A1 = 0 (mask `0x8002`, match `0x0000`) | 2:0 RAM page low bits at `#C000`, 3 screen select, 4 ROM14, 5 paging lock |
| `#DFFD` | A15 = 1, A13 = 0, A1 = 0 (mask `0xA002`, match `0x8000`) | 2:0 RAM page high bits (up to 1024 KB), 3 SCO, 4 WOROM, 5 CPM, 6 SCR, 7 DS80 (512x240 hi-res) |
| `OUT #xx7E` | A7 = 0, A0 = 0, only while DS80 is set | palette write: entry index comes from the previous `#FE` value, colour from A15:A8 |

`/state/paging` decodes `pDFFD` into `extended_ram_bank` (bits 2:0), `sco`, `worom`, `cpm`, `scr` and
`video_512x240` (bit 7). `/state/memory` adds `port_dffd` and `dffd_flags` for the same latch.

The Beta128 (WD1793) ports `#1F/#3F/#5F/#7F` and the system port `#FF` answer only while the disk interface is on
the bus: DOS latch set or CP/M mode (`CF_DOSPORTS`). In CP/M mode the system port moves to `#BF`; with ROM14 and CP/M
both set the "modified" ports `#83/#A3/#C3/#E3` and `#3F` apply. The AY ports `#FFFD/#BFFD` and `#FE` are always on
the bus.

`#3F`/`#5F` are shared with the Covox DAC below: which device answers depends on whether the disk interface is on
the bus.

## Sound

| Port | Dir | Meaning | Condition |
|:--|:--|:--|:--|
| `#5F` | W | Covox/SoundRive DAC, Left channel | disk interface off the bus (`!CF_DOSPORTS`) |
| `#3F` | W | Covox/SoundRive DAC, Right channel | disk interface off the bus (`!CF_DOSPORTS`) |

Profi's own Covox ports are `#5F`/`#3F`, not the Pentagon/Scorpion Soundrive set (`#F1/#F3/#F9/#FB`). The write is
forwarded from `PortDecoder_Profi::DecodePortOut` into the shared `Covox` device via its canonical Left/Right ports
(`Covox::PORT_LEFT_A`/`PORT_RIGHT_A` - not the `_B` ports, which would arm the device's mono-compatibility
fallback and leak the Right channel into Left whenever Left passed through silence), so the device's own
decoding/mixing and every other model stay unchanged.
Enabled the same way as Pentagon/Scorpion Covox, via `[SOUND] CovoxFB=1` in the machine's `unreal.ini`.

## Video and timing

| Mode name | Trigger | Visible area | Framebuffer |
|:--|:--|:--|:--|
| `PROFI` | DS80 = 0 | 256x192 | 352x288 |
| `PROFIHR` | DS80 = 1 (`#DFFD` bit 7) | 512x240 | 608x288 |

Both modes use the same beam: 312 lines of 224 T (69888 T per frame), INT-to-paper 12580 T. `/state/screen/mode`,
CLI `state video`, Lua/Python `screen_video_state()` and MCP `inspect_state aspects:["video"]` report the mode name
and resolution.

## Time-travel debugging

The `#DFFD` latch and the 16-entry palette are persisted as `PeripheralId::ProfiPaging` (id 9); see
[time-travel-debugging-tdd.md](../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md).

## Known limitations

- RTC (DS12885), IDE, the Kempston joystick and the Covox extended-mode aliases (`#87/#A7/#C7/#E7`) are not
  implemented yet. Covox/SoundRive itself (`#5F`/`#3F`, NORMAL mode) is implemented - see "Sound" above.
- The BIOS drive probe polls the WD1793 for BUSY after every command, so the FDC must show authentic timing while the
  SYS ROM runs: fast disk loading is disarmed there, and a Type II command on a not-ready drive keeps BUSY set for
  64 T-states before ending. With both in place the BIOS reaches its main menu with or without a disk (menu entries:
  CP/M, TR-DOS 48K/128K, Sinclair 48/128, test menu). Booting the selected entries has not been verified yet.
- Hi-res frame timing is not verified against real hardware.

An MCP client gets a condensed version of this page from the resource `unreal://machine/profi`.
