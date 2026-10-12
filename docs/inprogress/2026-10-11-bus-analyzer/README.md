# Bus analyzer: one recorder, protocol decoders and checks for every peripheral bus

**Date:** 2026-10-11 · owner idea 2026-10-11 · PLAN.md row **#110** · status in [TODO.md](TODO.md)

## Why

A user asked: "does the emulator catch SPI races on TS-Conf?". It does not. Today every SPI host keeps its
own rules and its own diagnostics:

| SPI master | Machines | Byte timing | Early access | Protocol log |
|:--|:--|:--|:--|:--|
| `ZControllerSpi` (`core/src/emulator/io/spi/`) | TS-Conf, ATM3 / BaseConf (ZX-Evo) | instant | not modelled | none |
| `PortDecoder_Next` SPI (`portdecoder_next.cpp:902-960`) | ZX Next | 16 CPU clocks | dropped, counted (`_spiTooFast`) | `NextSdJournal` |
| `DivMmcPaging` (`core/src/emulator/io/divmmc/`) | any machine with the DivMMC card | instant | not modelled | none |
| `NeoGSSpi` (`core/src/emulator/sound/chips/neogs/`) | the NeoGS card (SD, VS10xx SCI, SDI) | 16 or 8 x div + 2 card clocks | restart, `truncatedByte()` | none |

All four drive the same `SdCardSpi`, and the card already produces protocol events (`SdTraceEvent`) that
only the Next listens to. Beyond SPI the emulator has I2C (Next RTC), UARTs (16550, 8251, Next UART + ESP,
ZiFi, Hayes modem), PS/2 (Evo AVR), ATA, FDC command buses, the ZX-Bus and the Sprinter ISA bus, and seven
hand-rolled event rings (`NextSdJournal`, `NextIntJournal`, `NextRegJournal`, `NextDivMmcJournal`,
`PortDiagRecorder`, `GsPortTrace`, `MultiSoundBusTrace`) with their own time stamps, queries and endpoints.

The fix is the one the media manager made for disks: **one subsystem that knows every bus**, with one
event model, one set of protocol decoders and checks, and one surface on every automation plane.

## What it is

- **BusManager**, one per emulator (beside `MediaManager`): every bus registers itself with a stable id
  (`zc.spi`, `next.spi`, `divmmc.spi`, `neogs.sd`, ...), its protocol and the devices on it.
- **Taps** in the bus masters record transactions (chip select, byte exchanged, early access, truncated
  byte), not wires; one pointer test when nobody listens.
- **Protocol events** come from the devices that already decode them (the SD card: command + R1, block
  read, block written, data response).
- **Checks** turn rule violations into findings with PC and time: an access before the byte finished, a
  chip-select change mid-byte or mid-command, a command while the card is busy or still answering, the CPU
  on the SPI ports while an SPI DMA runs.
- **SD card timing profiles** (ideal, realistic, slow) so a driver that only works with the emulator's
  instant card fails here and not on the real board.
- **Export** to VCD (GTKWave / PulseView) with wires synthesized from transactions, so a trace from the
  emulator sits next to a trace from a logic analyzer on the board.

## Documents

| File | Topic |
|:--|:--|
| [use-cases.md](use-cases.md) | **Start here.** Who uses it, for what, what it changes for them |
| [requirements.md](requirements.md) | Functional and non-functional requirements, automation planes, acceptance |
| [technical-design.md](technical-design.md) | Event model, components, per-file changes, API on every plane, checks, timing profiles, export, TTD, threading, cost |
| [agent-recipes.md](agent-recipes.md) | The `.recipe/` files agents get (MCP first) and what each covers |
| [mutation-tests.md](mutation-tests.md) | Proof that the checks catch driver bugs: a reference driver, its mutants, forks per platform, real software (NedoOS, Wild Commander, UnoDOS, NextZXOS, DSS) |
| [TODO.md](TODO.md) | Phases B1-B7, status |
