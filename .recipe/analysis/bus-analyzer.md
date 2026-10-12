# Recipe: Bus analyzer (SPI traces, protocol events, driver checks, export)

Goal: see what a program does on a peripheral bus - every chip select and byte, the SD commands with their R1 and
the blocks read and written - and let the emulator check the driver for races a real board punishes. Covers the
SPI buses of every machine and card: TS-Conf / ZX-Evo Z-Controller (`zc.spi`: the SD card, and on the VDAC2 build
the FT812 in slot 1), ZX Next (`next.spi`, cards 0 and 1), the DivMMC card (`divmmc.spi`), NeoGS (`neogs.sd`,
`neogs.mp3.sci`, `neogs.mp3.sdi`), and the IDE channels of every IDE board (`ide0`, `ide1`: ATA registers, commands,
data words - Nemo, ATM, TS-Conf, Sprinter / DSS). For an SD driver's whole workflow (hardening with slow cards) see
[sd-driver-debugging.md](sd-driver-debugging.md). Design: `docs/inprogress/2026-10-11-bus-analyzer/`.

Off by default: switching it on observes only (no machine change, allowed while another client holds control).

> **How to use the sections:** [MCP](#mcp-preferred) is preferred. Use [WebAPI](#webapi) inside host-side
> pipelines (policy: [_common/transports.md](../_common/transports.md)).

## MCP (preferred)

```text
bus_analyzer {"action":"list"}                                   # the buses, devices, trace / checks state
bus_analyzer {"action":"enable","bus":"zc.spi"}                  # trace + checks (bus omitted: every bus)
bus_analyzer {"action":"enable","bus":"zc.spi","protocol_only":true}   # no single bytes / data words / status reads: a whole boot fits
... run the program (control_execution run_frames / step) ...
bus_analyzer {"action":"findings","bus":"zc.spi"}                # what the checks found
bus_analyzer {"action":"events","bus":"zc.spi","kinds":"sd","limit":40}      # the SD protocol
bus_analyzer {"action":"events","bus":"zc.spi","kinds":"spi","since":1200}   # bytes after seq 1200
bus_analyzer {"action":"export","bus":"zc.spi","path":"<repo>/scratch/bus.vcd"}   # PulseView / GTKWave
bus_analyzer {"action":"disable"}
```

Reading the text (one line per item, every field):

```text
zc.spi (spi): Z-Controller SPI (#57 data, #77 chip selects): SD card, VDAC2 FT812
  trace on size=1534/65536 evicted=0 rewound=0 last_seq=1534; checks on findings=1 (dma_collision=1); byte_time_ns=607.143
  slot 0: sd sd present=yes type=SDHC state=ready timing=ideal seed=1 blocks_read=12 blocks_written=0
#812 f1520 t31002 pc=0x8A3C cpu slot=0 command CMD17 arg=0x00000800 r1=0x00
#813 f1520 t31002 pc=0x8A3C cpu slot=0 read block=2048
#814 f1520 t31040 pc=0x8A41 cpu slot=0 byte mosi=0xFF miso=0x00 (read)
#2 error dma_collision f1521 t120 pc=0x6012 cpu slot=0 value=0xFF event=#1530 - the CPU accessed the SPI ports while ...
```

- `seq` grows forever; pass `since` for incremental reads. `slot=none`: a byte with no device selected.
- `source`: `cpu`, `dma` (TS-Conf SPI DMA), `card_cpu` / `card_dma` (NeoGS). `pc` is the instruction's (the card
  CPU's on NeoGS).
- Kinds: `select`, `deselect`, `byte`, `early` (an access before the byte finished), `truncated` (a byte cut off by
  a restart), `command` (SD command + argument + R1; `r1=none` = ignored), `read` / `write` (a block). Groups:
  `spi` (the first five), `sd` (the last three).
- `evicted`: the ring dropped the oldest (raise `capacity` on enable). `rewound`: events dropped after a TTD seek
  went back in time (nothing is recorded while TTD replays).

### Rules (findings)

| Rule | Severity | Meaning on the real hardware |
|:--|:--|:--|
| `early_access` | error | a read / write before the byte finished (Next: 16 CPU clocks per byte; NeoGS: restart of a master) |
| `select_mid_byte` | error | chip select changed while a byte was shifting |
| `deselect_mid_command` | warning | card deselected inside a 6-byte SD command |
| `command_while_busy` | error | a command while the card held the line busy after a write: a real card ignores it |
| `command_drops_answer` | warning | a command before the data block of the previous one was read out |
| `crc_error` | warning | the card answered with the CRC error bit |
| `dma_collision` | error | TS-Conf: the CPU on `#57` / `#77` while an SPI DMA runs on the same master |
| `ata_data_without_drq` | error | ATA: the data register accessed while DRQ was clear (status not checked, an error ignored) |
| `ata_command_drops_data` | warning | ATA: a command written while DRQ was set (a sector left unread or half written) |
| `ata_write_while_busy` | error | ATA: a task-file register written while BSY (e.g. right after a software reset, without the wait) |

ATA kinds: `reg_read` / `reg_write` (`register` status / sector_count / lba_low ..., `value`), `ata_command` (`READ
SECTORS (#20)`, `IDENTIFY DEVICE (#EC)` ...), `data` (one word; status polls and data are many events: filter with
`kinds:"ata_command"` first). Group `ata`.

The emulator's ideal card forgives what the rules report: a finding means "works here, fails on a board".
On the Evo / TS-Conf a CPU-only early access is physically impossible (a byte takes 17 fclk, 0.61 us; two Z80 port
accesses are at least 12 T apart), so that rule never fires there - the real races are DMA and protocol ones.

## WebAPI

```bash
BASE=http://localhost:8090/api/v1; EMU=0
curl -s "$BASE/emulator/$EMU/bus"
curl -s -X POST "$BASE/emulator/$EMU/bus/all/trace-journal" -H 'Content-Type: application/json' -d '{"enabled":true,"checks":true}'
curl -s "$BASE/emulator/$EMU/bus/zc.spi/events?kinds=sd&limit=40"
curl -s "$BASE/emulator/$EMU/bus/zc.spi/findings?rules=dma_collision"
curl -s "$BASE/emulator/$EMU/bus/zc.spi/export?format=vcd" > scratch/bus.vcd     # or &path=<file on the emulator host>
curl -s "$BASE/emulator/$EMU/bus/zc.spi/export?format=jsonl&since=1200" > scratch/bus.jsonl
```

Other surfaces, same names and output: CLI `bus`, `bus zc.spi on`, `bus zc.spi events kinds=sd`, `bus zc.spi findings`,
`bus zc.spi export vcd <file>`; Lua `bus_list()`, `bus_events("zc.spi", {kinds="sd"})`; Python
`emu.bus_events("zc.spi", kinds="sd")`, `emu.bus_control("all", enabled=True, checks=True)`.

## Export

VCD: timescale 1 ns, `sck`, `mosi`, `miso` and an active-low `cs_n_<device>` per device, wires synthesized from the
bytes at the bus's byte time (mode 0). In PulseView add the SPI decoder (CS# active low) and stack "SD card (SPI
mode)" on it. Protocol events have no wires; JSON lines carries every field.
