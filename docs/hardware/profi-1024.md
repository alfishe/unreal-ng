# ZX Profi (models `PROFI` = v5, `PROFI3` = v3)

The Profi came as two board families, and both are creatable on this build:

| Model | Board | RAM | Default ROM | Config |
|:--|:--|:--|:--|:--|
| `PROFI` (alias `PROFI5`) | v5.0x (Kondor, 1993-94) | 512K, **1024K** | `data/rom/profi.rom` (Bios 2.0 + TR-DOS 6.08 + STS monitor) | `data/configs/profi/unreal.ini` |
| `PROFI3` | v3.x (TOO "Profi" / JV Kramis, 1990) | **512K**, 1024K | `data/rom/profi/kramis-v02.rom` (factory BIOS V0.2 + TR-DOS 5.03) | `data/configs/profi3/unreal.ini` |

Create one with `emulator_manage action=create model=PROFI3` (or `PROFI`), CLI `create PROFI3`, or
`POST /api/v1/emulator/start {"model": "PROFI3"}`. The factory system ROMs of both boards are in `data/rom/profi/`
([README-ROMS.md](../../data/rom/README-ROMS.md)).

What differs between the boards (`ProfiBoard`, `core/src/emulator/ports/models/profiboard.h`):

| | v3 | v5 |
|:--|:--|:--|
| 512x240 hi-res | monochrome: ink = border colour, paper = its inverse | 16 colours per 8 pixels from a 256-colour palette (`OUT #xx7E`) |
| `#FE` read bit 7 | 1 | GX0 from the palette in DS80 |
| Extended port map (CP/M + ROM14) | none: the port decoder PROM takes ADR15 instead of ROM14 | FDC `#83..#E3`, system `#3F`, 8255 / Covox `#A7` / `#C7`, IDE, RTC |
| RTC, IDE | none | yes |
| Frame and INT (default `[PROFI] SyncProm=`) | 69888 T, INT 12580 T before paper (the PROM of a 3.2 board) | 69888 T, INT 14368 T before paper (the PROM of a Kondor 5.04 board) |

`[PROFI] SyncProm=` selects another sync PROM: `0a1d`, `samx6`, `fb0579b6` (71680 T, INT 48 T before paper) or
`v503`. The evidence for every difference, and the PROM decodes, are in
[docs/inprogress/2026-10-01-profi-v3-v5](../inprogress/2026-10-01-profi-v3-v5/README.md); the original Profi design
is [technical-design.md](../inprogress/2026-09-21-profi/technical-design.md).

## ROM layout (4 pages of 16 KB)

| Page | Content |
|:--|:--|
| 0 | SYS / menu ROM (BIOS), selected after reset |
| 1 | TR-DOS |
| 2 | 128K (the 128 editor in the factory images, the STS monitor in `profi.rom`) |
| 3 | 48K BASIC |

Automation surfaces (`/state/paging`, `/state/memory/rom`, CLI `state rom`, Lua/Python `paging_state()`) report these
as "SYS/Menu ROM", "TR-DOS ROM", "128K ROM" and "48K BASIC ROM". TR-DOS is entered through the
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

## RTC / CMOS

| Port | Dir | Meaning | Condition |
|:--|:--|:--|:--|
| `#BF`, `#FF` | W | RTC/CMOS register address latch | EXT mode (`cpm && rom14`) |
| `#9F`, `#DF` | R/W | RTC/CMOS register data | EXT mode (`cpm && rom14`) |

All four ports decode as one chip select (`(port & 0x9F) == 0x9F`); bit 5 of the port splits address (set) from
data (clear). Behind them sits the shared MC146818 / DS12887 chip (`core/src/emulator/io/rtc/ds12887.h`, 256 cells),
the same class the ATM3 / ZX-Evo and the Scorpion SMUC use. UnrealSpeccy and ZXMAK2 (`CmosProfi.cs`, a DS12885)
model it the same way. The karabas-pro clone differs: its "RTC" is a 256-byte RAM the board's AVR refreshes from
its own clock, so flag and UIP timing there follow the AVR firmware, not the datasheet.

- Time: the host's local time plus whatever offset the guest set by writing the time registers (the SET bit holds
  the clock while the guest writes). While a TTD session records, the clock runs on emulated time instead, so a
  replay reads the same time the recording read.
- Registers A-D: A bits 6-0 stored, UIP in the last 244 us before each second; B fully stored (binary / BCD,
  12 / 24 hour, SET); C flags UF and AF cleared by the read; D always `#80`.
- Battery-backed cells: `[PROFI] NvramFile=` (A, B, the alarms and cells `#0E-#FF`), read at power-on and written
  when the machine is destroyed; empty keeps them for the session only.

EXT mode here is UnrealSpeccy's own definition (`cpm && rom14`) - not Karabas's wider `dosAct && !rom14` variant,
which would additionally expose RTC/IDE to the SYS ROM. That's a clone extension, unproven by UnrealSpeccy
sources, so it is intentionally not implemented.

`#BF`/`#FF` alias to the Beta128 system port outside EXT mode (see "Paging latches" above); the RTC/CMOS decode is
checked first in `PortDecoder_Profi` so it wins whenever EXT mode is active.

## Video and timing

| Mode name | Trigger | Visible area | Framebuffer |
|:--|:--|:--|:--|
| `PROFI` | DS80 = 0 | 256x192 | 352x288 |
| `PROFIHR` | DS80 = 1 (`#DFFD` bit 7) | 512x240 | 608x288 |

Both modes use the same beam: 312 lines of 224 T (69888 T per frame). The INT position comes from the board's sync
PROM, `[PROFI] SyncProm=`: 14368 T before the paper on v5 (`v503`), 12580 T on v3 (`0a1d`), and two more PROMs to
choose from ([design](../inprogress/2026-10-01-profi-v3-v5/design.md) section 5). `/state/screen/mode`, CLI
`state video`, Lua/Python `screen_video_state()` and MCP `inspect_state aspects:["video"]` report the mode name and
resolution.

## CPU clock, wait states, floating bus

The v5 CP/M switch holds `#DFFD` at `#00` while it is on (`[PROFI] CpmSwitch=`, CLI `switch cpm on`), and
`[PROFI] DffdDecode=emulators|v50|v506` picks the board's `#DFFD` decode (the 5.0x boards decode A13 and A1 only).

The TURBO switch on the front panel runs the CPU at 7 MHz (`[PROFI] Turbo=`, CLI `switch turbo on`, WebAPI
`/switches`, Qt Machine > TURBO Switch; TTD records it). On v3 a loaded floppy head (the WD1793's HLD) holds 3.5 MHz.

| Board, clock | CPU waits (feature `contention`) |
|:--|:--|
| v5, 3.5 MHz | RAM accesses: 1 T on every other T of the paper fetch window, none in the border; `[PROFI] WaitPhase=0..3`, `WaitConfig=profi\|pentagon` (jumper SB8), `RomWait=0\|1` |
| v5, 7 MHz | approximation: RAM 1 in the border, 2 in the paper; ROM reads 1 |
| v3, 3.5 MHz | none |
| v3, 7 MHz | RAM accesses: 2 clocks from an even 7 MHz clock, 3 from an odd one; ROM none |

The v3 board has a floating bus: an `IN` with A0 = 1 that no device answers reads the pixel byte the video latch
holds (no attribute bytes), `#FF` in the border. The v5 reads `#FF`. Sources and worked examples:
[research-profi-v5-wait.md](../inprogress/2026-10-01-profi-v3-v5/research-profi-v5-wait.md),
[research-profi-v3-turbo-floatbus.md](../inprogress/2026-10-01-profi-v3-v5/research-profi-v3-turbo-floatbus.md).

## Time-travel debugging

The `#DFFD` latch, the 16-entry palette and the TURBO switch are persisted as `PeripheralId::ProfiPaging` (id 9); a
flip of the switch is a journaled input like a key. The RTC (v5) (cells,
address latch, time base) as `PeripheralId::Ds12887` (id 18); see
[time-travel-debugging-tdd.md](../emulator/design/debugger/time-travel-debug/time-travel-debugging-tdd.md).

## Known limitations

- Hi-res (DS80) runs its own timing: the CPU at 3 MHz (v3) or ZQ3 / 4 (v5, `[PROFI] ZQ3MHz`, 5 MHz by default),
  the frame and INT from the sync PROM's upper half, the AY at 1.5 MHz, model waits on v5 and the hi-res floating
  bus on v3 ([design-hires.md](../inprogress/2026-10-01-profi-v3-v5/design-hires.md)).
- The BIOS drive probe polls the WD1793 for BUSY after every command, so the FDC must show authentic timing while the
  SYS ROM runs: fast disk loading is disarmed there, and a Type II command on a not-ready drive keeps BUSY set for
  64 T-states before ending. With both in place the BIOS reaches its main menu with or without a disk (menu entries:
  CP/M, TR-DOS 48K/128K, Sinclair 48/128, test menu). TR-DOS and Sinclair 48 / 128 start on both boards (tests).
- Hi-res frame timing is not verified against real hardware.

An MCP client gets a condensed version of this page from the resource `unreal://machine/profi`.
